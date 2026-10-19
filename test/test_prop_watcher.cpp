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

#include <boost/ut.hpp>
#include <nlohmann/json.hpp>
#include "list_property_watcher.hpp"
#include "station.hpp"
#include <peel/Gio/Gio.h>

int main() {
    using namespace boost::ut;
    using namespace std::literals;
    using namespace nlohmann::literals;
    using namespace peel;

    Type::of<mnn::ListPropertyWatcher>().ensure();
    Type::of<mnn::Station>().ensure();

    "basic"_test = [] {
      auto store = Gio::ListStore::create(Type::of<mnn::Station>());
      auto kvz = mnn::Station::create(R"({ "callsign": "KI6KVZ", "name": "Andrew" })"_json);
      auto amm = mnn::Station::create(R"({ "callsign": "KJ6AMM", "name": "Paul" })"_json);
      store->append(kvz);

      auto watcher = mnn::ListPropertyWatcher::create(store, "is-acknowledged");
      expect(eq("is-acknowledged"sv, std::string_view(watcher->get_prop_name())));

      int changed = 0;
      SignalConnection conn = watcher->connect_changed([&changed](mnn::ListPropertyWatcher*) { ++changed; });

      kvz->set_is_acknowledged(true);
      expect(eq(1, changed));
      kvz->set_is_acknowledged(false);
      expect(eq(2, changed));

      // Other properties are ignored
      kvz->set_name("Not Andrew");
      kvz->set_status(mnn::StationStatus::HEARD_DIRECT);
      expect(eq(2, changed));

      // Membership changes emit, and added items are watched
      store->append(amm);
      expect(eq(3, changed));
      amm->set_is_acknowledged(true);
      expect(eq(4, changed));

      // Removed items are no longer watched
      store->remove(0);
      expect(eq(5, changed));
      kvz->set_is_acknowledged(true);
      expect(eq(5, changed));
      amm->set_is_acknowledged(false);
      expect(eq(6, changed));
    };

}
