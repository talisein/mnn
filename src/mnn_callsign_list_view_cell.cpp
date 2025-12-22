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
