/*
  monday-night-net: An amateur radio net monitoring utility in gtk4
  Copyright (C) 2025  Andrew Potter

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

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include "station.hpp"

namespace mnn
{
  class StationColumnView final : public peel::Gtk::Box
  {
    PEEL_SIMPLE_CLASS(StationColumnView, peel::Gtk::Box);

    void init(Class *);

    struct Members {
      peel::RefPtr<peel::Gio::ListModel> model;
      peel::RefPtr<peel::Gtk::CustomFilter> filter;
      peel::RefPtr<peel::Gtk::ListItemFactory> callsign_factory;
      peel::RefPtr<peel::Gtk::ListItemFactory> button_factory;
      peel::Gtk::ColumnView* view;
      peel::Gtk::Label* total_label;
      int filter_begin;
      int filter_end;
    } m;

    void vfunc_dispose();

    bool filter_suffix(peel::Object*) const noexcept;

  public:
    PEEL_PROPERTY(peel::Gio::ListModel, model, "model");
    PEEL_PROPERTY(peel::Gtk::CustomFilter, filter, "filter");
    PEEL_PROPERTY(peel::Gtk::ListItemFactory, callsign_factory, "callsign-factory");
    PEEL_PROPERTY(peel::Gtk::ListItemFactory, button_factory, "button-factory");
    PEEL_PROPERTY(unsigned char, filter_begin, "filter-begin");
    PEEL_PROPERTY(unsigned char, filter_end, "filter-end");

    peel::RefPtr<peel::Gio::ListModel> get_model() const noexcept { return m.model; };
    void set_model(peel::RefPtr<peel::Gio::ListModel> model) noexcept { m.model = std::move(model); notify(prop_model()); };
    peel::RefPtr<peel::Gtk::CustomFilter> get_filter() const noexcept { return m.filter; };
    peel::RefPtr<peel::Gtk::ListItemFactory> get_callsign_factory() const noexcept { return m.callsign_factory; };
    peel::RefPtr<peel::Gtk::ListItemFactory> get_button_factory() const noexcept { return m.button_factory; };

    char get_filter_begin() const noexcept;
    void set_filter_begin(char b) noexcept;
    char get_filter_end() const noexcept;
    void set_filter_end(char e) noexcept;

    static peel::FloatPtr<StationColumnView>
    create(peel::RefPtr<peel::Gio::ListModel> store, char begin, char end) noexcept;

  private:
    template<typename F>
    static void
    define_properties (F &f)
    {
      f.prop(prop_model())
        .get(&StationColumnView::get_model)
        .set(&StationColumnView::set_model);
      f.prop(prop_filter())
        .get(&StationColumnView::get_filter);
      f.prop(prop_callsign_factory())
        .get(&StationColumnView::get_callsign_factory);
      f.prop(prop_button_factory())
        .get(&StationColumnView::get_button_factory);
      f.prop(prop_filter_begin(), 'A', 'z', 'A')
        .get(&StationColumnView::get_filter_begin)
        .set(&StationColumnView::set_filter_begin);
      f.prop(prop_filter_end(), 'A', 'z', 'Z')
        .get(&StationColumnView::get_filter_end)
        .set(&StationColumnView::set_filter_end);
    }
  };
}
