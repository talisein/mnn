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

#include <charconv>
#include <cmath>
#include <optional>
#include <string_view>
#include <system_error>

namespace mnn
{
    /* Parses a decimal-degrees string ("37.403684") as both callook and
     * Nominatim send it. Rejects trailing junk, non-finite values, and
     * anything beyond +/- limit. */
    inline std::optional<double>
    parse_degrees(std::string_view s, double limit) noexcept
    {
        if (s.empty())
            return std::nullopt;
        double v;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc {} || ptr != s.data() + s.size() || !std::isfinite(v) || std::abs(v) > limit)
            return std::nullopt;
        return v;
    }

} // namespace mnn
