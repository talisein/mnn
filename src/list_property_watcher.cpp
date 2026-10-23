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


#include "list_property_watcher.hpp"
#include <ranges>

namespace mnn
{
  using namespace peel;
  namespace GObject = peel::GObject;

  PEEL_CLASS_IMPL (ListPropertyWatcher, "ListPropertyWatcher", Object)

  Signal<ListPropertyWatcher, void (void)> ListPropertyWatcher::sig_changed;

  void
  ListPropertyWatcher::Class::init()
  {
    override_vfunc_dispose<ListPropertyWatcher>();
    sig_changed = Signal<ListPropertyWatcher, void (void)>::create("changed");
  }

  void
  ListPropertyWatcher::init(Class*)
  {
    new (&m) Members;
  }

  void
  ListPropertyWatcher::vfunc_dispose()
  {
    m.items.clear();
    m.items_changed_conn.disconnect();
    m.list = nullptr;
    parent_vfunc_dispose<ListPropertyWatcher>();
  }

  void
  ListPropertyWatcher::set_list(RefPtr<Gio::ListModel> list) noexcept
  {
    if (list == m.list) return;
    m.items.clear();
    m.items_changed_conn.disconnect();
    m.list = std::move(list);
    if (m.list) {
      m.items_changed_conn = m.list->connect_items_changed(this, &ListPropertyWatcher::on_items_changed);
      on_items_changed(m.list, 0, 0, m.list->get_n_items());
    } else {
      sig_changed.emit(this);
    }
    notify(prop_list());
  }

  ListPropertyWatcher::ItemWatch
  ListPropertyWatcher::watch_item(RefPtr<Object> item) noexcept
  {
    SignalConnection conn = item->connect_notify(Property<void>{m.prop_name.c_str()},
                                                 [this] (Object*, GObject::ParamSpec*) { sig_changed.emit(this); });
    return ItemWatch{std::move(item), std::move(conn)};
  }

  void
  ListPropertyWatcher::on_items_changed(Gio::ListModel* list, unsigned position, unsigned removed, unsigned added) noexcept
  {
    auto first = m.items.begin() + position;
    auto added_watches = std::views::iota(position, position + added)
      | std::views::transform([this, list] (unsigned i) { return watch_item(list->get_object(i)); });
    m.items.insert_range(m.items.erase(first, first + removed), added_watches);
    sig_changed.emit(this);
  }

}
