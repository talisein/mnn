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

#include "mnn_error.hpp"
#include <utility>

namespace mnn
{
    const char *
    error_category::name() const noexcept
    {
        return "Monday Night Net Error";
    }

    std::string
    error_category::message(int ev) const
    {
        using namespace std::literals;
        switch (static_cast<error>(ev)) {
            case error::missing_name:
                return "Missing name in JSON station record"s;
            case error::missing_callsign:
                return "Missing callsign in JSON station record"s;
            case error::invalid_callsign:
                return "Invalid callsign"s;
            case error::callsign_not_found:
                return "Callsign not found by callook.info"s;
            case error::lookup_unavailable:
                return "Callsign lookup service unavailable"s;
            case error::no_coordinates:
                return "No coordinates known for callsign"s;
            case error::bad_response:
                return "Unexpected response from callsign lookup service"s;
            case error::network_error:
                return "Network error during callsign lookup"s;
            case error::database_error:
                return "Database error"s;
            case error::cancelled:
                return "Operation cancelled"s;
        }
        return std::format("Unknown Monday Night Net error code {}", ev);
    }

    const std::error_category&
    get_error_category() noexcept
    {
        static const error_category instance;
        return instance;
    }

    std::error_code
    make_error_code(error e) noexcept
    {
        return { std::to_underlying(e), get_error_category() };
    }

} // namespace mnn
