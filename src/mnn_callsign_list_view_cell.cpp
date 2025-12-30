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

#include "mnn_callsign_list_view_cell.hpp"
#include "config.hpp"
#include <peel/widget-template.h>
#include <peel/GLib/GLib.h>
#include <peel/GObject/Binding.h>

namespace mnn
{
using namespace peel;

PEEL_CLASS_IMPL (CallsignListViewCell, "MNNCallsignListViewCell", Gtk::Box);

void
CallsignListViewCell::vfunc_dispose()
{
    dispose_template(Type::of<CallsignListViewCell> ());
    parent_vfunc_dispose<CallsignListViewCell> ();
}

void
CallsignListViewCell::Class::init()
{
    override_vfunc_dispose<CallsignListViewCell>();
    set_template_from_resource(RESOURCE_BASEPATH "/mnn-callsign-list-view-cell.ui");

    PEEL_WIDGET_TEMPLATE_BIND_CHILD (CallsignListViewCell, m.prefix_label, "prefix-label");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (CallsignListViewCell, m.suffix_label, "suffix-label");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (CallsignListViewCell, m.name_label, "name-label");
}

void
CallsignListViewCell::init(Class*)
{
    init_template();
}

FloatPtr<CallsignListViewCell>
CallsignListViewCell::create() noexcept
{
  return Object::create<CallsignListViewCell>();
}

RefPtr<Gtk::SignalListItemFactory>
CallsignListViewCell::make_factory() noexcept
{
    RefPtr<Gtk::SignalListItemFactory> factory = Gtk::SignalListItemFactory::create();
    factory->connect_setup (
        [] (Gtk::SignalListItemFactory *factory, Object *obj)
        {
            Gtk::ListItem *item = obj->cast<Gtk::ListItem> ();
            item->set_child (CallsignListViewCell::create());
        });

    struct Bindings {
        SignalConnection status_conn;
        SignalConnection is_aec_conn;
        SignalConnection is_ack_conn;
    };

    factory->connect_bind (
        [] (Gtk::SignalListItemFactory *factory, Object *obj)
        {
            Gtk::ListItem *item = obj->cast<Gtk::ListItem>();
            CallsignListViewCell *callsign_cell = item->get_child()->cast<CallsignListViewCell>();
            Station *station = item->get_item()->cast<Station>();
            auto binds = new Bindings{};

            callsign_cell->m.prefix_label->set_text(station->get_prefix_cstr());
            callsign_cell->m.suffix_label->set_text(station->get_suffix_cstr());
            callsign_cell->m.name_label->set_text(station->get_name_cstr());

            callsign_cell->update_css_classes(station);

            auto css_lambda = [callsign_cell] (Object *s, GObject::ParamSpec*) {
                auto station = s->cast<Station>();
                callsign_cell->update_css_classes(station);
            };
            binds->status_conn = station->connect_notify(Station::prop_status(), decltype(css_lambda){css_lambda});
            binds->is_aec_conn = station->connect_notify(Station::prop_is_assistant_emergency_coordinator(), decltype(css_lambda){css_lambda});
            binds->is_ack_conn = station->connect_notify(Station::prop_is_acknowledged(), decltype(css_lambda){css_lambda});

            item->set_data("callsign-cell-bindings", binds, [](gpointer b) { delete (Bindings*)b; });
        });

    factory->connect_unbind(
        [] (Gtk::SignalListItemFactory *factory, Object *obj)
        {
            Gtk::ListItem *item = obj->cast<Gtk::ListItem>();

            auto binds = static_cast<Bindings*>(item->steal_data("callsign-cell-bindings"));
            delete binds;
        });
  return factory;
}

void
CallsignListViewCell::update_css_classes(Station* station) noexcept
{
    const auto is_unack = !station->is_acknowledged() && station->get_status() != mnn::StationStatus::PENDING;
    const auto is_aec   = station->is_assistant_emergency_coordinator();

    if (is_unack && is_aec) {
        set_css_classes((const char*[]){
                "unacknowledged",
                "is-aec",
                nullptr
            });
    } else if (is_unack && !is_aec) {
        set_css_classes((const char*[]){
                "unacknowledged",
                nullptr
            });
    } else if (!is_unack && is_aec) {
        set_css_classes((const char*[]){
                "is-aec",
                nullptr
            });
    } else if (!is_unack && !is_aec) {
        set_css_classes((const char*[]){nullptr});
    }
}

} // namespace mnn
