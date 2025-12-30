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
#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include <nlohmann/json.hpp>

namespace mnn
{
    class ApplicationWindow final : public peel::Adw::ApplicationWindow
    {
        PEEL_SIMPLE_CLASS (ApplicationWindow, peel::Adw::ApplicationWindow);

        void init (Class *);

        struct Members {
            peel::RefPtr<peel::Gio::Settings> settings;
            peel::Gtk::Entry* date_entry;
            peel::Gtk::Popover* date_entry_popover;
            peel::Gtk::Calendar* date_entry_calendar;
            peel::Gtk::Entry* frequency_entry;
            peel::Gtk::EntryBuffer* callsign_entry_buffer;
            peel::Gtk::EntryBuffer* name_entry_buffer;
            peel::Gtk::FlowBox* columns_flowbox;
            peel::Adw::ToastOverlay* toast_overlay;
        } m;

        void setup_net(const nlohmann::json&);
        void on_calendar_day_selected(peel::Gtk::Calendar*);
        void on_date_entry_icon_pressed(peel::Gtk::Entry*, peel::Gtk::Entry::IconPosition);
    protected:
        void vfunc_dispose();

    public:
        [[nodiscard]] static ApplicationWindow* create(peel::Adw::Application *);
    };

} // namespace mnn
