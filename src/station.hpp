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
#include <string_view>
#include <optional>
#include <peel/GObject/Object.h>
#include <peel/class.h>
#include <peel/enum.h>
#include <peel/String.h>
#include <peel/GLib/GLib.h>
#include <peel/Shumate/Location.h>
#include <nlohmann/json.hpp>

namespace mnn
{
    enum class StationStatus
    {
        PENDING,
        HEARD_DIRECT,
        HEARD_RELAY
    };
}
PEEL_ENUM(mnn::StationStatus)

namespace mnn
{
    class Station final : public peel::Shumate::Location
    {
        PEEL_SIMPLE_CLASS (Station, Object)
        friend class peel::Shumate::Location;

        static void init_type(peel::Type tp);
        static void init_interface(peel::Shumate::Location::Iface*);
        void init (Class*);

        struct Location {
            double latitude;
            double longitude;
        };

        struct Members {
            std::string name;
            std::string callsign;
            std::string prefix;
            std::string suffix;
            bool is_assistant_emergency_coordinator;
            bool is_acknowledged;
            StationStatus status;
            std::optional<Location> location;
        } m;

    public:
        PEEL_PROPERTY(const char *, name, "name");
        PEEL_PROPERTY(const char *, callsign, "callsign");
        PEEL_PROPERTY(const char *, prefix, "prefix");
        PEEL_PROPERTY(const char *, suffix, "suffix");
        PEEL_PROPERTY(bool, is_acknowledged, "is-acknowledged");
        PEEL_PROPERTY(bool, is_assistant_emergency_coordinator, "is-assistant-emergency-coordinator");
        PEEL_PROPERTY(StationStatus, status, "status");
        PEEL_PROPERTY(bool, has_location, "has-location");

        void set_name(std::string_view str);
        std::string get_name() const;
        void set_callsign(std::string_view str);
        std::string get_callsign() const;
        bool is_assistant_emergency_coordinator() const;
        void set_is_assistant_emergency_coordinator(bool);
        bool is_acknowledged() const;
        void set_is_acknowledged(bool);
        bool has_location() const;
        StationStatus get_status() const;
        void set_status(StationStatus status);

        static peel::RefPtr<Station> create(const nlohmann::json&);

    protected:
        double vfunc_get_latitude() const noexcept;
        double vfunc_get_longitude() const noexcept;
        void vfunc_set_location(double, double) noexcept;

    private:
        void update_prefix_suffix();
        const char* get_name_cstr();
        const char* get_callsign_cstr();
        const char* get_prefix_cstr();
        const char* get_suffix_cstr();
        void set_name_cstr(const char* str);
        void set_callsign_cstr(const char* str);

        template<typename F>
        static void
        define_properties (F &f)
        {
            f.prop(prop_name(), nullptr)
                .get(&Station::get_name_cstr)
                .set(&Station::set_name_cstr);
            f.prop(prop_callsign(), nullptr)
                .get(&Station::get_callsign_cstr)
                .set(&Station::set_callsign_cstr);
            f.prop(prop_prefix(), nullptr)
                .get(&Station::get_prefix_cstr);
            f.prop(prop_suffix(), nullptr)
                .get(&Station::get_suffix_cstr);
            f.prop(prop_is_acknowledged(), false)
                .get(&Station::is_acknowledged)
                .set(&Station::set_is_acknowledged);
            f.prop(prop_is_assistant_emergency_coordinator(), false)
                .get(&Station::is_assistant_emergency_coordinator)
                .set(&Station::set_is_assistant_emergency_coordinator);
            f.prop(prop_has_location(), false)
                .get(&Station::has_location);
            f.prop(prop_status(), StationStatus::PENDING)
                .get(&Station::get_status)
                .set(&Station::set_status);
            f.override_prop(peel::Shumate::Location::prop_latitude())
                .get(&Station::vfunc_get_latitude);
            f.override_prop(peel::Shumate::Location::prop_longitude())
                .get(&Station::vfunc_get_longitude);
        }

    };

} // namespace mnn
