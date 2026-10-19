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

#include <string>
#include <peel/Dex/Future.h>
#include <peel/Gom/Driver.h>
#include <peel/RefPtr.h>

namespace mnn
{
    /* Path of the sqlite database file, under the XDG data dir. */
    std::string database_path();

    /* Open (creating if necessary) the local sqlite database. Returns null and
     * logs on failure. */
    peel::RefPtr<peel::Gom::Driver> open_database();

    /* Build the registry of mnn entities and create (migrating as needed) a
     * repository on the driver. The future resolves to a Gom::Repository. */
    peel::RefPtr<peel::Dex::Future> create_repository(peel::Gom::Driver *driver);

} // namespace mnn
