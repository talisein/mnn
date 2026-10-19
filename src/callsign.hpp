/*
    monday-night-net: An amateur radio net monitoring utility in gtk4
    Copyright (C) 2026  Andrew Potter

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

#include "mnn_error.hpp"
#include <algorithm>
#include <expected>
#include <ranges>
#include <string>
#include <string_view>

namespace mnn
{
    namespace callsign_detail
    {
        constexpr bool is_ascii_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
        constexpr bool is_ascii_upper(char c) { return c >= 'A' && c <= 'Z'; }
        constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
        constexpr char to_ascii_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }
    }

    /* Trim and upper-case. Accepts 3-10 characters of [A-Z0-9] with at
     * least one letter and one digit. */
    inline std::expected<std::string, error>
    normalize_callsign(std::string_view raw)
    {
        using namespace callsign_detail;
        auto call = raw
            | std::views::drop_while(is_ascii_space)
            | std::views::reverse
            | std::views::drop_while(is_ascii_space)
            | std::views::reverse
            | std::views::transform(to_ascii_upper)
            | std::ranges::to<std::string>();

        if (call.size() < 3 || call.size() > 10)
            return std::unexpected(error::invalid_callsign);
        if (!std::ranges::all_of(call, [](char c) { return is_ascii_upper(c) || is_ascii_digit(c); }))
            return std::unexpected(error::invalid_callsign);
        if (!std::ranges::any_of(call, is_ascii_upper) || !std::ranges::any_of(call, is_ascii_digit))
            return std::unexpected(error::invalid_callsign);
        return call;
    }

} // namespace mnn
