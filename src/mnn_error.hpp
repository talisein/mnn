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
    enum class error : int
    {
        missing_name,
        missing_callsign,
        invalid_callsign,
    };

    class error_category : public std::error_category {
    public:
        const char* name() const noexcept override;
        std::string message(int ev) const override;
    };

    const std::error_category& get_error_category() noexcept;

} // namespace mnn

namespace std {
    template <>
    struct is_error_code_enum<mnn::error> : public true_type {};
    std::error_code make_error_code(mnn::error e) noexcept;
}
