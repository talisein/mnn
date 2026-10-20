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

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include <peel/Adw/Adw.h>
#include <peel/Gtk/Gtk.h>
#include <peel/Gio/Gio.h>
#include <peel/Shumate/Marker.h>
#include <peel/Shumate/MarkerLayer.h>
#include <peel/Shumate/PathLayer.h>
#include <peel/Shumate/SimpleMap.h>
#include <peel/class.h>
#include "station.hpp"
#include "position_lookup.hpp"

namespace mnn
{
  /* A map of the net: a green pin for each station heard direct, a yellow pin
   * for each station heard via relay. Stations without a location are looked
   * up through position-lookup once they are heard.
   *
   * home-callsign (net control) gets a house pin, tinted green or yellow
   * once that station is itself heard. Hovering a station's pin draws a
   * line to it from home, labeled with the distance in miles. */
  class StationMapView final : public peel::Gtk::Box
  {
    PEEL_SIMPLE_CLASS(StationMapView, peel::Gtk::Box);

    void init(Class *);

    struct StationWatch {
      peel::RefPtr<Station> station;
      peel::SignalConnection status_conn;
      peel::SignalConnection location_conn;
      peel::SignalConnection precision_conn;
      peel::RefPtr<peel::Shumate::Marker> marker; // null while no pin is shown
      peel::Gtk::Image* pin;                      // owned by marker
      peel::RefPtr<peel::Gio::Cancellable> cancel;
      bool lookup_started;
    };

    struct Home {
      std::string callsign;                          // normalized, or empty
      peel::RefPtr<peel::Shumate::Coordinate> location; // null until located
      peel::RefPtr<peel::Shumate::Marker> marker;    // null while no pin is shown
      peel::RefPtr<peel::Gio::Cancellable> cancel;   // for the pending lookup
      bool lookup_started;
    };

    struct Members {
      peel::RefPtr<peel::Gio::ListModel> model;
      peel::RefPtr<PositionLookup> position_lookup;
      peel::RefPtr<peel::Shumate::MarkerLayer> marker_layer;
      peel::RefPtr<peel::Shumate::PathLayer> path_layer;
      peel::RefPtr<peel::Shumate::MarkerLayer> distance_layer;
      peel::RefPtr<peel::Shumate::Marker> distance_marker;
      peel::Gtk::Label* distance_label;              // owned by distance_marker
      bool distance_shown;
      Home home;
      peel::SignalConnection items_changed_conn;
      std::vector<StationWatch> stations; // parallel to positions in model
      peel::Shumate::SimpleMap* map;
      peel::Adw::ToastOverlay* toast_overlay;
    } m;

    void vfunc_dispose();

    StationWatch watch_station(peel::RefPtr<Station> station) noexcept;
    void unwatch_station(StationWatch& watch) noexcept;
    void on_items_changed(peel::Gio::ListModel*, unsigned position, unsigned removed, unsigned added) noexcept;
    StationWatch* find_watch(Station* station) noexcept;
    void on_station_changed(Station* station) noexcept;
    void update_station(StationWatch& watch) noexcept;
    void remove_marker(StationWatch& watch) noexcept;
    /* Whether station is net control with a known location, so the home pin
     * stands in for its own. */
    bool is_home(Station* station) const noexcept;
    void update_home() noexcept;
    /* Re-places the pins of net control and of the previous net control. */
    void update_home_stations(std::string_view previous = {}) noexcept;
    void start_home_lookup() noexcept;
    peeler::future<bool> locate_home(peel::RefPtr<PositionLookup> lookup, std::string callsign,
                                     peel::RefPtr<peel::Gio::Cancellable> cancel);
    void show_distance(Station* station) noexcept;
    void hide_distance() noexcept;
    /* Looks up station's position and stores it on the station; toasts if
     * there is none. */
    peeler::future<bool> locate_station(peel::RefPtr<PositionLookup> lookup, peel::RefPtr<Station> station,
                                        peel::RefPtr<peel::Gio::Cancellable> cancel);

  public:
    PEEL_PROPERTY(peel::Gio::ListModel, model, "model");
    PEEL_PROPERTY(PositionLookup, position_lookup, "position-lookup");
    PEEL_PROPERTY(const char *, home_callsign, "home-callsign");

    peel::RefPtr<peel::Gio::ListModel> get_model() const noexcept { return m.model; }
    void set_model(peel::RefPtr<peel::Gio::ListModel> model) noexcept;
    peel::RefPtr<PositionLookup> get_position_lookup() const noexcept { return m.position_lookup; }
    void set_position_lookup(peel::RefPtr<PositionLookup> lookup) noexcept;
    const char* get_home_callsign() const noexcept { return m.home.callsign.empty() ? nullptr : m.home.callsign.c_str(); }
    /* Normalized; an invalid callsign clears home. Looked up after a short
     * pause, so typing doesn't query every prefix. */
    void set_home_callsign(const char* callsign) noexcept;
    /* The visible map: center, zoom, rotation, source, and the bounding box
     * of what is on screen, so a differently sized window can be fit to the
     * same area. Saved with the net. */
    nlohmann::json view_to_json() const;

  private:
    template<typename F>
    static void
    define_properties (F &f)
    {
      f.prop(prop_model())
        .get(&StationMapView::get_model)
        .set(&StationMapView::set_model);
      f.prop(prop_position_lookup())
        .get(&StationMapView::get_position_lookup)
        .set(&StationMapView::set_position_lookup);
      f.prop(prop_home_callsign(), nullptr)
        .get(&StationMapView::get_home_callsign)
        .set(&StationMapView::set_home_callsign);
    }
  };
}
