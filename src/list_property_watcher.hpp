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

#include <string>
#include <vector>
#include <peel/GObject/Object.h>
#include <peel/GObject/ParamFlags.h>
#include <peel/Gio/Gio.h>
#include <peel/class.h>
#include <peel/signal.h>
#include <peel/enum.h>
#include <peel/String.h>
#include <peel/GLib/GLib.h>

namespace mnn
{

  /* Emits "changed" whenever the membership of `list` changes, or whenever
   * the property named `prop-name` changes on any item currently in `list`. */
  class ListPropertyWatcher final : public peel::Object
  {
    PEEL_SIMPLE_CLASS (ListPropertyWatcher, Object)
    void init(Class*);
    void vfunc_dispose();

    struct ItemWatch {
      peel::RefPtr<peel::Object> item;
      peel::SignalConnection conn;
    };

    struct Members {
      peel::RefPtr<peel::Gio::ListModel> list;
      std::string prop_name;
      peel::SignalConnection items_changed_conn;
      std::vector<ItemWatch> items; // parallel to positions in list
    } m;

    ItemWatch watch_item(peel::RefPtr<peel::Object> item) noexcept;
    void on_items_changed(peel::Gio::ListModel*, unsigned position, unsigned removed, unsigned added) noexcept;

    public:
    PEEL_PROPERTY(peel::Gio::ListModel, list, "list");
    PEEL_PROPERTY(const char*, prop_name, "prop-name");

    peel::RefPtr<peel::Gio::ListModel> get_list() const noexcept { return m.list; };
    void set_list(peel::RefPtr<peel::Gio::ListModel> list) noexcept;
    const char* get_prop_name() const noexcept { return m.prop_name.c_str(); }

    PEEL_SIGNAL_CONNECT_METHOD(changed, sig_changed)

    static peel::RefPtr<ListPropertyWatcher> create(peel::RefPtr<peel::Gio::ListModel> list, const char* prop_name)
    {
      return peel::Object::create<ListPropertyWatcher>(prop_prop_name(), prop_name,
                                                       prop_list(), std::move(list));
    }

  private:
    // Construct-only
    void set_prop_name(const char* prop_name) noexcept { m.prop_name = prop_name ? prop_name : ""; };

    template<typename F>
    static void
    define_properties (F &f)
    {
      f.prop(prop_list())
        .get(&ListPropertyWatcher::get_list)
        .set(&ListPropertyWatcher::set_list);
      f.prop(prop_prop_name(), nullptr)
        .get(&ListPropertyWatcher::get_prop_name)
        .set(&ListPropertyWatcher::set_prop_name)
        .flags(peel::GObject::ParamFlags::CONSTRUCT_ONLY);
    }

    static peel::Signal<ListPropertyWatcher, void (void)> sig_changed;
  };

}
