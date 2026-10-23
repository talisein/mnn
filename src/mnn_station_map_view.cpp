/*
  monday-night-net: An amateur radio net monitoring utility in gtk4
  Copyright (C) 2026  Andrew Potter

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "mnn_station_map_view.hpp"
#include "config.hpp"
#include "mnn_error.hpp"
#include "callsign.hpp"
#include "coordinates.hpp"
#include "util.hpp"
#include <glib/gi18n.h>
#include <peel/Shumate/Map.h>
#include <peel/Shumate/MapSourceRegistry.h>
#include <peel/Shumate/Viewport.h>
#include <peel/widget-template.h>
#include <algorithm>
#include <format>
#include <ranges>

namespace mnn
{
  using namespace peel;
  namespace GObject = peel::GObject;

  PEEL_CLASS_IMPL(StationMapView, "MNNStationMapView", Gtk::Box);

  namespace
  {
    // Mountain View, at a zoom that shows a little more than the city.
    constexpr double initial_latitude  = 37.3894;
    constexpr double initial_longitude = -122.0819;
    constexpr double initial_zoom      = 12.0;

    constexpr int pin_size = 24;

    // Wait for typing in the SS callsign entry to pause before looking it up.
    constexpr unsigned home_lookup_delay_ms = 1000;
  }

  void
  StationMapView::vfunc_dispose()
  {
    m.items_changed_conn.disconnect();
    for (auto& watch : m.stations)
      unwatch_station(watch);
    m.stations.clear();
    m.home.cancel->cancel();
    m.home.marker     = nullptr;
    m.home.location   = nullptr;
    m.model           = nullptr;
    m.position_lookup = nullptr;
    m.marker_layer    = nullptr;
    m.distance_marker = nullptr;
    m.distance_layer  = nullptr;
    m.path_layer      = nullptr;
    dispose_template(Type::of<StationMapView>());
    parent_vfunc_dispose<StationMapView>();
  }

  void
  StationMapView::Class::init()
  {
    override_vfunc_dispose<StationMapView>();
    Type::of<Shumate::SimpleMap>().ensure();
    set_template_from_resource(RESOURCE_BASEPATH "/mnn-station-map-view.ui");

    PEEL_WIDGET_TEMPLATE_BIND_CHILD(StationMapView, m.map, "map");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD(StationMapView, m.toast_overlay, "toast-overlay");
  }

  void
  StationMapView::init(Class *)
  {
    new (&m) Members;
    init_template();

    auto registry = Shumate::MapSourceRegistry::create_with_defaults();
    m.map->set_map_source(registry->get_by_id(SHUMATE_MAP_SOURCE_OSM_MAPNIK));

    auto viewport = m.map->get_viewport();
    viewport->set_zoom_level(initial_zoom);
    m.map->get_map()->center_on(initial_latitude, initial_longitude);

    // Bottom to top: the hover line, its distance label, the pins.
    m.path_layer = Shumate::PathLayer::create(viewport);
    Gdk::RGBA stroke { 0.11f, 0.44f, 0.85f, 0.9f };
    m.path_layer->set_stroke_color(&stroke);
    m.path_layer->set_stroke_width(2.5);
    m.map->add_overlay_layer(m.path_layer);

    m.distance_layer = Shumate::MarkerLayer::create(viewport);
    m.map->add_overlay_layer(m.distance_layer);
    auto label = Gtk::Label::create(nullptr);
    label->add_css_class("distance-label");
    m.distance_label = label;
    m.distance_marker = Shumate::Marker::create();
    m.distance_marker->set_child(label);
    m.distance_marker->set_can_target(false);

    m.marker_layer = Shumate::MarkerLayer::create(viewport);
    m.map->add_overlay_layer(m.marker_layer);

    m.home.cancel = Gio::Cancellable::create();
  }

  void
  StationMapView::set_model(RefPtr<Gio::ListModel> model) noexcept
  {
    if (model == m.model) return;
    m.items_changed_conn.disconnect();
    on_items_changed(nullptr, 0, m.stations.size(), 0);
    m.model = std::move(model);
    if (m.model) {
      m.items_changed_conn = m.model->connect_items_changed(this, &StationMapView::on_items_changed);
      on_items_changed(m.model, 0, 0, m.model->get_n_items());
    }
    notify(prop_model());
  }

  void
  StationMapView::set_position_lookup(RefPtr<PositionLookup> lookup) noexcept
  {
    if (lookup == m.position_lookup) return;
    m.position_lookup = std::move(lookup);
    // Stations heard before the lookup was ready can be located now.
    for (auto& watch : m.stations)
      update_station(watch);
    start_home_lookup();
    notify(prop_position_lookup());
  }

  void
  StationMapView::set_home_callsign(const char* callsign) noexcept
  {
    auto call = normalize_callsign(to_sview(callsign)).value_or(std::string {});
    if (call == m.home.callsign) return;

    m.home.cancel->cancel();
    m.home.cancel         = Gio::Cancellable::create();
    auto previous         = std::exchange(m.home.callsign, std::move(call));
    m.home.location       = nullptr;
    m.home.lookup_started = false;
    hide_distance();
    update_home();
    // The previous net control gets its own pin back.
    update_home_stations(previous);
    notify(prop_home_callsign());

    if (!m.home.callsign.empty()) {
      // dispose cancels, so an uncancelled timer still has a live view.
      GLib::timeout_add_once(home_lookup_delay_ms, [this, cancel = m.home.cancel] {
        if (!cancel->is_cancelled())
          start_home_lookup();
      });
    }
  }

  StationMapView::StationWatch
  StationMapView::watch_station(RefPtr<Station> station) noexcept
  {
    auto changed = [this] { return [this] (Object* o, GObject::ParamSpec*) { on_station_changed(o->cast<Station>()); }; };
    StationWatch watch {
      .station        = station,
      .status_conn    = station->connect_notify(Station::prop_status(), changed()),
      .location_conn  = station->connect_notify(Station::prop_has_location(), changed()),
      .precision_conn = station->connect_notify(Station::prop_precision(), changed()),
      .marker         = nullptr,
      .pin            = nullptr,
      .cancel         = Gio::Cancellable::create(),
      .lookup_started = false,
    };
    update_station(watch);
    return watch;
  }

  void
  StationMapView::unwatch_station(StationWatch& watch) noexcept
  {
    watch.cancel->cancel();
    watch.status_conn.disconnect();
    watch.location_conn.disconnect();
    watch.precision_conn.disconnect();
    remove_marker(watch);
  }

  void
  StationMapView::remove_marker(StationWatch& watch) noexcept
  {
    if (watch.marker) {
      if (m.marker_layer)
        m.marker_layer->remove_marker(watch.marker);
      watch.marker = nullptr;
      watch.pin    = nullptr;
    }
  }

  void
  StationMapView::on_items_changed(Gio::ListModel* list, unsigned position, unsigned removed, unsigned added) noexcept
  {
    auto first = m.stations.begin() + position;
    for (auto& watch : std::ranges::subrange(first, first + removed))
      unwatch_station(watch);
    auto added_watches = std::views::iota(position, position + added)
      | std::views::transform([this, list] (unsigned i) {
          RefPtr<Object> item = list->get_object(i);
          return watch_station(item->cast<Station>());
        });
    m.stations.insert_range(m.stations.erase(first, first + removed), added_watches);
  }

  void
  StationMapView::on_station_changed(Station* station) noexcept
  {
    if (auto *watch = find_watch(station))
      update_station(*watch);
  }

  StationMapView::StationWatch*
  StationMapView::find_watch(Station* station) noexcept
  {
    auto it = std::ranges::find(m.stations, station, [] (const StationWatch& w) -> Station* { return w.station; });
    return it != m.stations.end() ? &*it : nullptr;
  }

  peeler::future<bool>
  StationMapView::locate_station(RefPtr<PositionLookup> lookup, RefPtr<Station> station,
                                 RefPtr<Gio::Cancellable> cancel)
  {
    auto callsign = station->get_callsign();
    auto pos = co_await lookup->lookup(callsign, cancel);
    // dispose cancels every lookup, so an uncancelled one still has a live view.
    if (cancel->is_cancelled())
      co_return false;
    if (!pos) {
      g_message("no position for %s: %s", callsign.c_str(), pos.error().message().c_str());
      auto reason = pos.error().message();
      auto text = std::vformat(_("Couldn't locate {}: {}"), std::make_format_args(callsign, reason));
      m.toast_overlay->add_toast(Adw::Toast::create(text.c_str()));
      co_return false;
    }
    station->set_precision(pos->precision);
    // The has-location notify places the pin through update_station.
    auto *location = pos->coordinate->cast<Shumate::Location>();
    station->set_location(location->get_latitude(), location->get_longitude());
    co_return true;
  }

  void
  StationMapView::update_station(StationWatch& watch) noexcept
  {
    if (is_home(watch.station)) {
      // The home pin shows net control's status instead.
      remove_marker(watch);
      update_home();
      return;
    }

    auto status = watch.station->get_status();
    if (StationStatus::PENDING == status) {
      remove_marker(watch);
      return;
    }

    if (!watch.station->has_location()) {
      if (m.position_lookup && !watch.lookup_started) {
        watch.lookup_started = true;
        locate_station(m.position_lookup, watch.station, watch.cancel);
      }
      return;
    }

    if (!watch.marker) {
      auto pin = Gtk::Image::create();
      pin->set_pixel_size(pin_size);
      watch.pin = pin;
      auto label = Gtk::Label::create(watch.station->get_callsign_cstr());
      label->add_css_class("station-pin-label");
      auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
      box->append(std::move(pin));
      box->append(std::move(label));
      watch.marker = Shumate::Marker::create();
      watch.marker->set_child(box);
      watch.marker->add_css_class("station-pin");
      auto tooltip = std::format("{} — {}", watch.station->get_callsign(), watch.station->get_name());
      watch.marker->set_tooltip_text(tooltip.c_str());
      auto motion = Gtk::EventControllerMotion::create();
      motion->connect_enter([this, station = watch.station] (Gtk::EventControllerMotion*, double, double) {
        show_distance(station);
      });
      motion->connect_leave([this] (Gtk::EventControllerMotion*) { hide_distance(); });
      watch.marker->add_controller(std::move(motion));
      m.marker_layer->add_marker(watch.marker);
    }

    watch.marker->cast<Shumate::Location>()->set_location(watch.station->get_latitude(),
                                                          watch.station->get_longitude());
    if (Precision::approximate == watch.station->get_precision()) {
      // A "?" for a ZIP code centroid, anchored at its center.
      watch.pin->set_from_icon_name("dialog-question-symbolic");
      watch.marker->set_hotspot(-1, pin_size / 2.0);
    } else {
      // Anchor the pin's tip, centered horizontally above the label.
      watch.pin->set_from_icon_name("mark-location-symbolic");
      watch.marker->set_hotspot(-1, pin_size);
    }

    bool direct = StationStatus::HEARD_DIRECT == status;
    watch.marker->remove_css_class(direct ? "station-pin-relay" : "station-pin-direct");
    watch.marker->add_css_class(direct ? "station-pin-direct" : "station-pin-relay");
  }

  bool
  StationMapView::is_home(Station* station) const noexcept
  {
    return m.home.location && station->get_callsign() == m.home.callsign;
  }

  void
  StationMapView::update_home_stations(std::string_view previous) noexcept
  {
    for (auto& watch : m.stations) {
      auto call = watch.station->get_callsign();
      if (call == m.home.callsign || call == previous)
        update_station(watch);
    }
  }

  void
  StationMapView::update_home() noexcept
  {
    if (!m.home.location) {
      if (m.home.marker) {
        m.marker_layer->remove_marker(m.home.marker);
        m.home.marker = nullptr;
      }
      return;
    }

    if (!m.home.marker) {
      auto pin = Gtk::Image::create_from_icon_name("go-home-symbolic");
      pin->set_pixel_size(pin_size);
      // Not stretched to the label's width, so the heard tint stays a circle.
      pin->set_halign(Gtk::Align::CENTER);
      auto label = Gtk::Label::create(m.home.callsign.c_str());
      label->add_css_class("station-pin-label");
      auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 0);
      box->append(std::move(pin));
      box->append(std::move(label));
      m.home.marker = Shumate::Marker::create();
      m.home.marker->set_child(box);
      m.home.marker->add_css_class("station-pin");
      m.home.marker->add_css_class("station-pin-home");
      auto tooltip = std::vformat(_("{} — net control"), std::make_format_args(m.home.callsign));
      m.home.marker->set_tooltip_text(tooltip.c_str());
      // Anchored at the house's center, like an approximate pin.
      m.home.marker->set_hotspot(-1, pin_size / 2.0);
      m.marker_layer->add_marker(m.home.marker);
    }

    auto *home = m.home.location->cast<Shumate::Location>();
    m.home.marker->cast<Shumate::Location>()->set_location(home->get_latitude(), home->get_longitude());

    auto it = std::ranges::find(m.stations, m.home.callsign,
                                [] (const StationWatch& w) { return w.station->get_callsign(); });
    auto status = it != m.stations.end() ? it->station->get_status() : StationStatus::PENDING;
    m.home.marker->remove_css_class("station-pin-home-direct");
    m.home.marker->remove_css_class("station-pin-home-relay");
    if (StationStatus::HEARD_DIRECT == status)
      m.home.marker->add_css_class("station-pin-home-direct");
    else if (StationStatus::HEARD_RELAY == status)
      m.home.marker->add_css_class("station-pin-home-relay");
  }

  void
  StationMapView::start_home_lookup() noexcept
  {
    if (!m.position_lookup || m.home.callsign.empty() || m.home.location || m.home.lookup_started)
      return;
    m.home.lookup_started = true;
    locate_home(m.position_lookup, m.home.callsign, m.home.cancel);
  }

  peeler::future<bool>
  StationMapView::locate_home(RefPtr<PositionLookup> lookup, std::string callsign,
                              RefPtr<Gio::Cancellable> cancel)
  {
    auto pos = co_await lookup->lookup(callsign, cancel);
    // dispose and set_home_callsign cancel, so an uncancelled one is still current.
    if (cancel->is_cancelled())
      co_return false;
    if (!pos) {
      g_message("no position for home %s: %s", callsign.c_str(), pos.error().message().c_str());
      auto reason = pos.error().message();
      auto text = std::vformat(_("Couldn't locate net control {}: {}"), std::make_format_args(callsign, reason));
      m.toast_overlay->add_toast(Adw::Toast::create(text.c_str()));
      co_return false;
    }
    m.home.location = pos->coordinate;
    update_home();
    // Net control's own pin gives way to the home pin.
    update_home_stations();
    co_return true;
  }

  void
  StationMapView::show_distance(Station* station) noexcept
  {
    if (!m.home.location) return;
    auto *home = m.home.location->cast<Shumate::Location>();
    m.path_layer->remove_all();
    m.path_layer->add_node(home);
    m.path_layer->add_node(station);

    // The line is straight on the map, so at net distances the plain average
    // sits on its midpoint.
    double lat = (home->get_latitude() + station->get_latitude()) / 2;
    double lon = (home->get_longitude() + station->get_longitude()) / 2;
    m.distance_marker->cast<Shumate::Location>()->set_location(lat, lon);
    auto miles = distance_miles(home->get_latitude(), home->get_longitude(),
                                station->get_latitude(), station->get_longitude());
    auto text = std::vformat(_("{:.1f} mi"), std::make_format_args(miles));
    m.distance_label->set_label(text.c_str());
    if (!m.distance_shown) {
      m.distance_layer->add_marker(m.distance_marker);
      m.distance_shown = true;
    }
  }

  void
  StationMapView::hide_distance() noexcept
  {
    if (m.path_layer)
      m.path_layer->remove_all();
    if (m.distance_shown) {
      m.distance_layer->remove_marker(m.distance_marker);
      m.distance_shown = false;
    }
  }

  nlohmann::json
  StationMapView::view_to_json() const
  {
    auto *viewport = m.map->get_viewport();
    auto *center = viewport->cast<Shumate::Location>();
    nlohmann::json view = {
      {"center", {{"lat", center->get_latitude()}, {"long", center->get_longitude()}}},
      {"zoom", viewport->get_zoom_level()},
      {"rotation", viewport->get_rotation()}, // radians
    };
    if (auto *source = m.map->get_map_source())
      view["source"] = source->get_id();

    // Corners rather than two opposite ones: with rotation, any corner can be
    // the extreme.
    auto *map = m.map->get_map();
    double w = map->get_width(), h = map->get_height();
    if (w > 0 && h > 0) {
      double north = -90, south = 90, east = -180, west = 180;
      for (auto [x, y] : { std::pair { 0.0, 0.0 }, { w, 0.0 }, { 0.0, h }, { w, h } }) {
        double lat, lon;
        viewport->widget_coords_to_location(map, x, y, &lat, &lon);
        north = std::max(north, lat);
        south = std::min(south, lat);
        east  = std::max(east, lon);
        west  = std::min(west, lon);
      }
      view["bounds"] = { {"north", north}, {"south", south}, {"east", east}, {"west", west} };
    }
    return view;
  }

} // namespace mnn
