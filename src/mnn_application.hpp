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

#include <peel/Adw/Adw.h>
#include <peel/Gio/Gio.h>
#include <peel/GLib/GLib.h>
#include <peel/class.h>

namespace mnn
{
    class Application final : public peel::Adw::Application
    {
        PEEL_SIMPLE_CLASS(Application, peel::Adw::Application);
        friend class peel::Gio::Application;

        void init(Class *);
        void vfunc_activate();
        void action_quit(peel::Gio::SimpleAction *, peel::GLib::Variant *);

    public:
        [[nodiscard]] static peel::RefPtr<Application> create();
    };

} // namespace mnn
