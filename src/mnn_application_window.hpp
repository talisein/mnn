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
#include <string>
#include <string_view>
#include <vector>
#include "mnn_station_column_view.hpp"
#include "mnn_station_map_view.hpp"

namespace mnn
{
    class Application;

    class ApplicationWindow final : public peel::Adw::ApplicationWindow
    {
        PEEL_SIMPLE_CLASS (ApplicationWindow, peel::Adw::ApplicationWindow);

        void init (Class *);

        struct ColumnWatch {
            StationColumnView* column; // owned by columns_flowbox
            peel::SignalConnection conn;
        };

        struct CityTotal {
            std::string name;
            peel::Gtk::SpinButton* spin; // owned by city_totals_flowbox
        };

        struct Members {
            peel::RefPtr<peel::Gio::Settings> settings;
            peel::Gtk::Entry* date_entry;
            peel::Gtk::Popover* date_entry_popover;
            peel::Gtk::Calendar* date_entry_calendar;
            peel::Gtk::Entry* frequency_entry;
            peel::Gtk::EntryBuffer* callsign_entry_buffer;
            peel::Gtk::EntryBuffer* name_entry_buffer;
            peel::Gtk::FlowBox* columns_flowbox;
            StationMapView* map_view;
            peel::Adw::ToastOverlay* toast_overlay;
            peel::Gtk::Label* grand_total_label;
            peel::Gtk::Frame* city_totals_section;
            peel::Gtk::FlowBox* city_totals_flowbox;
            peel::RefPtr<peel::Gio::ListStore> store;
            std::vector<ColumnWatch> columns;
            std::vector<CityTotal> city_totals;
            nlohmann::json column_defs; // the "columns" the net was built from
        } m;

        void setup_net(const nlohmann::json&);
        /* Replaces the current net. Leaves it untouched and toasts if columns
         * or stations are invalid. */
        bool build_net(const nlohmann::json& columns, const nlohmann::json& stations);
        void toast(const char* text);
        nlohmann::json net_to_json() const;
        void save_net();
        void open_net();
        void load_net(peel::Gio::File* file);
        void update_grand_total() noexcept;
        /* Replaces the city total spin buttons with one per name in names,
         * prefilled from counts ({name: count}) where present. */
        void build_city_totals(const nlohmann::json& names, const nlohmann::json& counts);
        void show_add_station_dialog();
        /* Why callsign can't be added, or empty if it can. callsign is normalized. */
        std::string check_new_callsign(std::string_view raw) const;
        void add_station(std::string_view callsign, std::string_view name, StationStatus heard);
        void on_calendar_day_selected(peel::Gtk::Calendar*);
        void on_date_entry_icon_pressed(peel::Gtk::Entry*, peel::Gtk::Entry::IconPosition);
    protected:
        void vfunc_dispose();

    public:
        [[nodiscard]] static ApplicationWindow* create(Application *);
    };

} // namespace mnn
