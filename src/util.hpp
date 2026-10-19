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

#include <string_view>
#include <peel/String.h>

/* Global scope, as in milfeulle: test/peeler/test_loop.hpp calls ::to_sview. */

inline std::string_view to_sview(const peel::String& s) noexcept
{
    auto p = s.c_str();
    return p ? std::string_view{p} : std::string_view{};
}

inline std::string_view to_sview(const char *p) noexcept
{
    return p ? std::string_view{p} : std::string_view{};
}
