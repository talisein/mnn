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

  PEEL_CLASS_IMPL(StationMapView, "MNNStationMapView", Gtk::Box);

  namespace
  {
    // Mountain View, at a zoom that shows a little more than the city.
    constexpr double initial_latitude  = 37.3894;
    constexpr double initial_longitude = -122.0819;
    constexpr double initial_zoom      = 12.0;

    constexpr int pin_size = 24;
  }

  void
  StationMapView::vfunc_dispose()
  {
    m.items_changed_conn.disconnect();
    for (auto& watch : m.stations)
      unwatch_station(watch);
    m.stations.clear();
    m.model           = nullptr;
    m.position_lookup = nullptr;
    m.marker_layer    = nullptr;
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

    m.marker_layer = Shumate::MarkerLayer::create(viewport);
    m.map->add_overlay_layer(m.marker_layer);
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
    notify(prop_position_lookup());
  }

  StationMapView::StationWatch
  StationMapView::watch_station(RefPtr<Station> station) noexcept
  {
    auto changed = [this] { return [this] (Object* o, GObject::ParamSpec*) { on_station_changed(o->cast<Station>()); }; };
    StationWatch watch {
      .station        = station,
      .status_conn    = station->connect_notify(Station::prop_status(), changed()),
      .location_conn  = station->connect_notify(Station::prop_has_location(), changed()),
      .marker         = nullptr,
      .pin            = nullptr,
      .precision      = Precision::precise, // the net definition's coordinates
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
    if (auto *watch = find_watch(station))
      watch->precision = pos->precision;
    // The has-location notify places the pin through update_station.
    auto *location = pos->coordinate->cast<Shumate::Location>();
    station->set_location(location->get_latitude(), location->get_longitude());
    co_return true;
  }

  void
  StationMapView::update_station(StationWatch& watch) noexcept
  {
    auto status = watch.station->get_status();
    if (StationStatus::PENDING == status) {
      if (watch.marker) {
        m.marker_layer->remove_marker(watch.marker);
        watch.marker = nullptr;
        watch.pin    = nullptr;
      }
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
      m.marker_layer->add_marker(watch.marker);
    }

    watch.marker->cast<Shumate::Location>()->set_location(watch.station->get_latitude(),
                                                          watch.station->get_longitude());
    if (Precision::approximate == watch.precision) {
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

} // namespace mnn
