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

#include <vector>
#include <peel/Adw/Adw.h>
#include <peel/Gtk/Gtk.h>
#include <peel/Gio/Gio.h>
#include <peel/Shumate/Marker.h>
#include <peel/Shumate/MarkerLayer.h>
#include <peel/Shumate/SimpleMap.h>
#include <peel/class.h>
#include "station.hpp"
#include "position_lookup.hpp"

namespace mnn
{
  /* A map of the net: a green pin for each station heard direct, a yellow pin
   * for each station heard via relay. Stations without a location are looked
   * up through position-lookup once they are heard. */
  class StationMapView final : public peel::Gtk::Box
  {
    PEEL_SIMPLE_CLASS(StationMapView, peel::Gtk::Box);

    void init(Class *);

    struct StationWatch {
      peel::RefPtr<Station> station;
      peel::SignalConnection status_conn;
      peel::SignalConnection location_conn;
      peel::RefPtr<peel::Shumate::Marker> marker; // null while no pin is shown
      peel::Gtk::Image* pin;                      // owned by marker
      Precision precision;                        // of the station's location
      peel::RefPtr<peel::Gio::Cancellable> cancel;
      bool lookup_started;
    };

    struct Members {
      peel::RefPtr<peel::Gio::ListModel> model;
      peel::RefPtr<PositionLookup> position_lookup;
      peel::RefPtr<peel::Shumate::MarkerLayer> marker_layer;
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
    /* Looks up station's position and stores it on the station; toasts if
     * there is none. */
    peeler::future<bool> locate_station(peel::RefPtr<PositionLookup> lookup, peel::RefPtr<Station> station,
                                        peel::RefPtr<peel::Gio::Cancellable> cancel);

  public:
    PEEL_PROPERTY(peel::Gio::ListModel, model, "model");
    PEEL_PROPERTY(PositionLookup, position_lookup, "position-lookup");

    peel::RefPtr<peel::Gio::ListModel> get_model() const noexcept { return m.model; }
    void set_model(peel::RefPtr<peel::Gio::ListModel> model) noexcept;
    peel::RefPtr<PositionLookup> get_position_lookup() const noexcept { return m.position_lookup; }
    void set_position_lookup(peel::RefPtr<PositionLookup> lookup) noexcept;

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
    }
  };
}
