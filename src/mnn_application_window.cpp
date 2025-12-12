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


#include "mnn_application_window.hpp"

namespace mnn
{
    using namespace peel;

    PEEL_CLASS_IMPL (ApplicationWindow, "MNNApplicationWindow", Adw::ApplicationWindow)

    void
    ApplicationWindow::Class::init ()
    {
        /*
        install_action ("win.new-tab", nullptr, [] (Gtk::Widget *widget, const char *action_name, GLib::Variant *parameter)
        {
            (void) action_name;
            (void) parameter;
            widget->cast<Window> ()->open_new_tab ();
        });

        add_binding_action (GDK_KEY_T, Gdk::ModifierType::CONTROL_MASK, "win.new-tab", nullptr);
        */
    }

    void ApplicationWindow::init(Class *)
    {
        set_title ("Demo Adwaita app, written with peel");
        set_default_size (700, 500);
    }

    ApplicationWindow*
    ApplicationWindow::create (Adw::Application *app)
    {
        return Object::create<ApplicationWindow>(prop_application(), app);
    }

} // namespace mnn
