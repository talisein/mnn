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

#include <memory>
#include "mnn_button_list_view_cell.hpp"
#include "station.hpp"
#include "config.hpp"
#include <peel/widget-template.h>
#include <peel/GLib/GLib.h>
#include <peel/GObject/Binding.h>

using namespace peel;

namespace mnn
{

  PEEL_CLASS_IMPL (ButtonListViewCell, "MNNButtonListViewCell", Gtk::Box);

  void
  ButtonListViewCell::vfunc_dispose()
  {
    dispose_template(Type::of<ButtonListViewCell> ());
    parent_vfunc_dispose<ButtonListViewCell> ();
  }

  void
  ButtonListViewCell::Class::init()
  {
    override_vfunc_dispose<ButtonListViewCell>();
    set_template_from_resource(RESOURCE_BASEPATH "/mnn-button-list-view-cell.ui");

    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ButtonListViewCell, m.direct_button, "direct-button");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ButtonListViewCell, m.relay_button, "relay-button");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ButtonListViewCell, m.acknowledged_button, "acknowledged-button");
  }

  void
  ButtonListViewCell::init(Class*)
  {
    init_template();
  }

  FloatPtr<ButtonListViewCell>
  ButtonListViewCell::create() noexcept
  {
    auto res = Object::create<ButtonListViewCell>();

    return res;
  }

  RefPtr<Gtk::SignalListItemFactory>
  ButtonListViewCell::make_factory() noexcept
  {
    RefPtr<Gtk::SignalListItemFactory> factory = Gtk::SignalListItemFactory::create();
    factory->connect_setup (
      [] (Gtk::SignalListItemFactory *factory, Object *obj)
      {
        Gtk::ListItem *item = obj->cast<Gtk::ListItem> ();
        item->set_child (ButtonListViewCell::create());
      });

    struct Bindings {
      RefPtr<GObject::Binding> bind_ack;
      RefPtr<GObject::Binding> bind_direct;
      RefPtr<GObject::Binding> bind_relay;

      ~Bindings() {
        if (bind_ack) bind_ack->unbind();
        if (bind_direct) bind_direct->unbind();
        if (bind_relay) bind_relay->unbind();
      }
    };

    factory->connect_bind (
      [] (Gtk::SignalListItemFactory *factory, Object *obj)
      {
        Gtk::ListItem *item = obj->cast<Gtk::ListItem>();
        ButtonListViewCell *button_cell = item->get_child()->cast<ButtonListViewCell>();
        Station *station = item->get_item()->cast<Station>();
        auto binds = new Bindings{};

        binds->bind_ack = Object::bind_property(station,
                                                station->prop_is_acknowledged(),
                                                button_cell->m.acknowledged_button,
                                                button_cell->m.acknowledged_button->prop_active(),
                                                GObject::BindingFlags::BIDIRECTIONAL | GObject::BindingFlags::SYNC_CREATE
          );

        binds->bind_direct = Object::bind_property(station,
                                                   station->prop_status(),
                                                   button_cell->m.direct_button,
                                                   button_cell->m.direct_button->prop_active(),
                                                   GObject::BindingFlags::BIDIRECTIONAL | GObject::BindingFlags::SYNC_CREATE,
                                                   [] (StationStatus status) -> bool {
                                                     if (status == StationStatus::HEARD_DIRECT) {
                                                       return true;
                                                     } else {
                                                       return false;
                                                     }
                                                   },
                                                   [] (GObject::Binding* binding, const GObject::Value* from_value, GObject::Value* to_value) -> bool {
                                                     auto station = binding->get_source()->cast<Station>();
                                                     if (!station) return false;
                                                     const bool is_active = from_value->template get<bool>();
                                                     if (is_active) {
                                                       to_value->template set<StationStatus>(StationStatus::HEARD_DIRECT);
                                                     } else { // not direct...
                                                       if (station->is_acknowledged()) {
                                                         // If its acknowledged but not direct, it must be relay
                                                         to_value->set<StationStatus>(StationStatus::HEARD_RELAY);
                                                       } else {
                                                         // Unacknowledged, not direct. If its relay, leave it alone. Otherside set pending.
                                                         if (station->get_status() != StationStatus::HEARD_RELAY) {
                                                           to_value->set<StationStatus>(StationStatus::PENDING);
                                                         } else {
                                                           // Its already set on the station, but let's not leave the GValue unset
                                                           to_value->set<StationStatus>(StationStatus::HEARD_RELAY);
                                                         }
                                                       }
                                                     }
                                                     return true;
                                                   }
                                   );

        binds->bind_relay = Object::bind_property(station,
                                                  station->prop_status(),
                                                  button_cell->m.relay_button,
                                                  button_cell->m.relay_button->prop_active(),
                                                  GObject::BindingFlags::BIDIRECTIONAL | GObject::BindingFlags::SYNC_CREATE,
                                                  [] (StationStatus status) -> bool {
                                                    if (status == StationStatus::HEARD_RELAY) {
                                                      return true;
                                                    } else {
                                                      return false;
                                                    }
                                                  },
                                                  [] (GObject::Binding* binding, const GObject::Value* from_value, GObject::Value* to_value) -> bool {
                                                    auto station = binding->get_source()->cast<Station>();
                                                    if (!station) return false;
                                                    const bool is_active = from_value->template get<bool>();
                                                    if (is_active) {
                                                      to_value->template set<StationStatus>(StationStatus::HEARD_RELAY);
                                                    } else { // not relay...
                                                      if (station->is_acknowledged()) {
                                                        // If its acknowledged but not relay, it must be direct
                                                        to_value->template set<StationStatus>(StationStatus::HEARD_DIRECT);
                                                      } else {
                                                        // Unacknowledged, not relay. If its direct, leave it alone. Otherside set pending.
                                                        if (station->get_status() != StationStatus::HEARD_DIRECT) {
                                                          to_value->template set<StationStatus>(StationStatus::PENDING);
                                                        } else {
                                                          // Its already set on the station, but let's not leave the GValue unset
                                                          to_value->template set<StationStatus>(StationStatus::HEARD_DIRECT);
                                                        }
                                                      }
                                                    }
                                                    return true;
                                                  }
          );

        item->set_data("button-cell-bindings", binds, [](gpointer b) { delete static_cast<Bindings*>(b); });

      });

    factory->connect_unbind(
      [] (Gtk::SignalListItemFactory *factory, Object *obj)
      {
        Gtk::ListItem *item = obj->cast<Gtk::ListItem>();

        auto binds = static_cast<Bindings*>(item->steal_data("button-cell-bindings"));
        delete binds;
      });
    return factory;
  }

} // namespace mnn
