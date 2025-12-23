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
#include <peel/widget-template.h>
#include <peel/GLib/GLib.h>



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
    set_template_from_resource("/radio/ki6kvz/MondayNightNet/mnn-callsign-list-view-cell.ui");
    PEEL_WIDGET_TEMPLATE_BIND_CALLBACK(CallsignListViewCell, get_css_classes);

}

void
CallsignListViewCell::init(Class*)
{
    init_template();
}


Strv
CallsignListViewCell::get_css_classes(Station*, bool is_acknowledged, StationStatus status)
{
    auto builder = GLib::StrvBuilder::create();
    if (!is_acknowledged && status != StationStatus::PENDING) {
        builder->add("unacknowledged");
    }
    return GLib::StrvBuilder::unref_to_strv(builder);
}


} // namespace mnn


extern "C" {

gchar**
get_css_classes(GObject* gobj, gboolean is_acknowledged, int status, gboolean is_aec)
{
    //auto cell = reinterpret_cast<peel::Gtk::ColumnView::Cell*>(GTK_COLUMN_VIEW_CELL(gobj));
    g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();
    if (!is_acknowledged && status != std::to_underlying(mnn::StationStatus::PENDING)) {
        g_strv_builder_add (builder, "unacknowledged");
    }
    if (is_aec) {
        g_strv_builder_add (builder, "is-aec");
    }
    g_print("Status: %d is_aec: %d\n", status, is_aec);

    return g_strv_builder_end (builder);
}

void
on_heard_direct_clicked(GtkButton* self, gpointer user_data)
{
    auto cell = reinterpret_cast<peel::Gtk::ColumnView::Cell*>(GTK_COLUMN_VIEW_CELL(user_data));
    g_print("user data is %p\n", user_data);
    //auto button = reinterpret_cast<peel::Gtk::Button*>(self);
    auto station = reinterpret_cast<mnn::Station*>(cell->get_item());
    g_print("station is %p\n", user_data);
    station->set_status(mnn::StationStatus::HEARD_DIRECT);
}

}
