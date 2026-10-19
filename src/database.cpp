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

#include "database.hpp"
#include "entities.hpp"
#include "util.hpp"
#include <format>
#include <peel/GLib/Error.h>
#include <peel/GLib/GLib.h>
#include <peel/Gom/EntityMigrator.h>
#include <peel/Gom/Registry.h>
#include <peel/Gom/RegistryBuilder.h>
#include <peel/Gom/Repository.h>
#include <peel/UniquePtr.h>

using namespace peel;

namespace mnn
{
    static std::string
    data_dir()
    {
        return std::format("{}/monday-night-net", to_sview(GLib::get_user_data_dir()));
    }

    std::string
    database_path()
    {
        return std::format("{}/mnn.db", data_dir());
    }

    RefPtr<Gom::Driver>
    open_database()
    {
        auto dir = data_dir();
        if (GLib::mkdir_with_parents(dir.c_str(), 0700) != 0) {
            g_warning("failed to create data dir %s", dir.c_str());
            return nullptr;
        }

        auto path = database_path();
        UniquePtr<GLib::Error> error;
        auto uri = GLib::filename_to_uri(path.c_str(), nullptr, &error);
        if (error) {
            g_warning("failed to build database uri for %s: %s", path.c_str(), error->message);
            return nullptr;
        }

        auto driver = Gom::Driver::open(uri.c_str(), &error);
        if (error) {
            g_warning("failed to open database %s: %s", path.c_str(), error->message);
            return nullptr;
        }
        return driver;
    }

    RefPtr<Dex::Future>
    create_repository(Gom::Driver *driver)
    {
        auto builder = Gom::RegistryBuilder::create();
        register_entities(builder);
        auto registry = builder->build();
        auto migrator = Gom::EntityMigrator::create(registry);
        return Gom::Repository::create(driver, registry, migrator);
    }

} // namespace mnn
