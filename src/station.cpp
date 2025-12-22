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

#include <functional>
#include "station.hpp"
#include "mnn_error.hpp"

PEEL_ENUM_IMPL(mnn::StationStatus, "StationStatus",
               PEEL_ENUM_VALUE(mnn::StationStatus::PENDING, "pending"),
               PEEL_ENUM_VALUE(mnn::StationStatus::HEARD_DIRECT, "heard-direct"),
               PEEL_ENUM_VALUE(mnn::StationStatus::HEARD_RELAY, "heard-relay"))

namespace mnn
{
using namespace peel;

PEEL_CLASS_IMPL (Station, "Station", Object)

void
Station::init_type(peel::Type tp)
{
    PEEL_IMPLEMENT_INTERFACE(tp, Shumate::Location);
}

void
Station::init_interface(Shumate::Location::Iface* iface)
{
    iface->override_vfunc_get_latitude<Station>();
    iface->override_vfunc_get_longitude<Station>();
    iface->override_vfunc_set_location<Station>();
}

void
Station::Class::init()
{

}

void
Station::init(Class*)
{
    new (&m) Members;
    m.status = StationStatus::PENDING;
}

std::string
Station::get_name() const
{
    return m.name;
}

std::string
Station::get_callsign() const
{
    return m.callsign;
}

StationStatus
Station::get_status() const
{
    return m.status;
}

void
Station::set_name(std::string_view str)
{
    m.name.assign(str);
    notify(prop_name());
}

void
Station::set_callsign(std::string_view str)
{
    m.callsign.assign(str);
    freeze_notify();
    update_prefix_suffix();
    notify(prop_callsign());
    thaw_notify();
}

void
Station::set_status(StationStatus s)
{
    m.status = s;
    notify(prop_status());
}

bool
Station::is_assistant_emergency_coordinator() const
{
    return m.is_assistant_emergency_coordinator;
}

void
Station::set_is_assistant_emergency_coordinator(bool is_assistant_emergency_coordinator)
{
    m.is_assistant_emergency_coordinator = is_assistant_emergency_coordinator;
    notify(prop_is_assistant_emergency_coordinator());
}

bool
Station::is_acknowledged() const
{
    return m.is_acknowledged;
}

void
Station::set_is_acknowledged(bool is_ack)
{
    freeze_notify();
    if (StationStatus::PENDING == m.status) {
        set_status(StationStatus::HEARD_RELAY);
    }
    m.is_acknowledged = is_ack;
    notify(prop_is_acknowledged());
    thaw_notify();
}

void
Station::update_prefix_suffix()
{
    auto pos = m.callsign.find_first_of("0123456789");
    if (std::string::npos == pos) {
        m.prefix.clear();
        m.suffix.clear();
    } else {
        m.prefix = m.callsign.substr(0, pos + 1);
        m.suffix = m.callsign.substr(pos + 1);
    }
    notify(prop_prefix());
    notify(prop_suffix());
}

double
Station::vfunc_get_latitude() const noexcept
{
    return m.location.transform([](const auto& l){ return l.latitude; }).value_or(SHUMATE_MIN_LATITUDE);
}

double
Station::vfunc_get_longitude() const noexcept
{
    return m.location.transform([](const auto& l){ return l.longitude; }).value_or(SHUMATE_MIN_LONGITUDE);
}


void
Station::vfunc_set_location(double latitude, double longitude) noexcept
{
    if (latitude != SHUMATE_MIN_LATITUDE || longitude != SHUMATE_MIN_LONGITUDE) {
        m.location.emplace(latitude, longitude);
    } else {
        m.location.reset();
    }
    freeze_notify();
    notify(prop_has_location());
    notify(prop_latitude());
    notify(prop_longitude());
    thaw_notify();
}

bool
Station::has_location() const
{
    return m.location.has_value();
}

const char*
Station::get_name_cstr()
{
    if (m.name.empty()) return nullptr;
    return m.name.c_str();
}

const char*
Station::get_callsign_cstr()
{
    if (m.callsign.empty()) return nullptr;
    return m.callsign.c_str();
}

const char*
Station::get_prefix_cstr()
{
    if (m.prefix.empty()) return nullptr;
    return m.prefix.c_str();
}

const char*
Station::get_suffix_cstr()
{
    if (m.suffix.empty()) return nullptr;
    return m.suffix.c_str();
}

void
Station::set_callsign_cstr(const char* str)
{
    if (str) {
        m.callsign.assign(str);
    } else {
        m.callsign.clear();
    }

    freeze_notify();
    update_prefix_suffix();
    notify(prop_callsign());
    thaw_notify();
}

void
Station::set_name_cstr(const char* str)
{
    if (str) {
        m.name.assign(str);
    } else {
        m.name.clear();
    }

    notify(prop_name());
}

RefPtr<Station>
Station::create(const nlohmann::json& j)
{
    if (!j.contains("name")) {
        throw std::system_error(std::make_error_code(mnn::error::missing_name), std::format("Station record missing 'name' field: {}", j.dump()));
    }
    if (!j.contains("callsign")) {
        throw std::system_error(std::make_error_code(mnn::error::missing_callsign), std::format("Station record missing 'callsign' field: {}", j.dump()));
    }

    auto name = j["name"].get<std::string>();
    auto callsign = j["callsign"].get<std::string>();
    auto is_aem = j.contains("assistant_emergency_coordinator") && true == j["assistant_emergency_coordinator"].get<bool>();
    auto res = Object::create<mnn::Station>(mnn::Station::prop_name(), name.c_str(),
                                            mnn::Station::prop_callsign(), callsign.c_str(),
                                            mnn::Station::prop_is_assistant_emergency_coordinator(), is_aem);
    if (j.contains("lat") && j.contains("long"))
    {
        res->set_location(j["lat"].get<double>(), j["long"].get<double>());
    }
    return res;
}

} // namespace mnn
