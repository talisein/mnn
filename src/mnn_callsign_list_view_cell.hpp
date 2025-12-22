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
#include <peel/Strv.h>
#include "station.hpp"

namespace mnn
{
    class CallsignListViewCell final : public peel::Gtk::Box
    {
        PEEL_SIMPLE_CLASS(CallsignListViewCell, peel::Gtk::Box);

        void init(Class *);

        peel::Strv get_css_classes(Station*, bool is_acknowledged, StationStatus status);

    protected:
        void vfunc_dispose();

    public:
//        PEEL_PROPERTY(peel::Type::of<Station>(), station, "station");

    private:
    };

} // namespace mnn
