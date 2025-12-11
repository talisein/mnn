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

#include <iostream>
#include <peel/Gtk/Gtk.h>
#include <peel/GLib/GLib.h>
#include <peel/Gio/Gio.h>

#include "net_definition.hpp"
extern "C" {
#include "monday-night-net-resources.h"
}
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace peel;

int
main ()
{
  Gtk::init ();

  Gtk::Window *window = Gtk::Window::create ();
  window->set_title ("Hello, world!");
  window->present ();

  RefPtr<NetDefinition> my_instance = Object::create<NetDefinition> ();
  my_instance->say_hello ();

  auto net_resource = RefPtr<Gio::Resource>::adopt_ref(reinterpret_cast<Gio::Resource*>(monday_night_net_resources_get_resource()));
  auto net_bytes    = net_resource->lookup_data("/net/talinet/MondayNightNet/default-net.json", Gio::Resource::LookupFlags::NONE, nullptr);
  auto net_json     = json::parse(net_bytes->get_data());
  std::cout << net_json.dump(2);

  // Spin the event loop.
  GLib::MainContext *context = GLib::MainContext::default_ ();
  while (true)
    context->iteration (true);
}
