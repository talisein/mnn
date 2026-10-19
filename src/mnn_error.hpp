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

#include <system_error>
#include <format>

namespace mnn
{
    /* Explicit values: CallsignMiss persists these in the database. */
    enum class error : int
    {
        missing_name = 0,
        missing_callsign = 1,
        invalid_callsign = 2,
        callsign_not_found = 3,
        lookup_unavailable = 4,
        no_coordinates = 5,
        bad_response = 6,
        network_error = 7,
        database_error = 8,
        cancelled = 9,
    };

    class error_category : public std::error_category {
    public:
        const char* name() const noexcept override;
        std::string message(int ev) const override;
    };

    const std::error_category& get_error_category() noexcept;

    /* In mnn, not std, so std::error_code's converting constructor finds it
     * by ADL. */
    std::error_code make_error_code(error e) noexcept;

} // namespace mnn

namespace std {
    template <>
    struct is_error_code_enum<mnn::error> : public true_type {};
}
