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


#include "mnn_application.hpp"
#include "mnn_application_window.hpp"
#include "mnn.hpp"
#include "config.hpp"
#include <print>

namespace mnn
{
    using namespace peel;

    PEEL_CLASS_IMPL (Application, "MNNApplication", Adw::Application)

    void
    Application::init(Class *)
    {
        RefPtr<Gio::SimpleAction> action = Gio::SimpleAction::create ("quit", nullptr);
        action->connect_activate (this, &Application::action_quit);
        cast<Gio::ActionMap> ()->add_action (action);
        set_accels_for_action ("app.quit", (const char *[]) { "<Ctrl>Q", nullptr });
    }

    void
    Application::Class::init()
    {
        override_vfunc_activate<Application>();
    }

    void
    Application::vfunc_activate ()
    {
        parent_vfunc_activate<Application> ();

        ApplicationWindow *window = ApplicationWindow::create (this);
        window->present ();
    }

    void
    Application::action_quit(Gio::SimpleAction *, GLib::Variant *)
    {
        quit();
    }

    RefPtr<Application>
    Application::create()
    {
        return Object::create<Application>(prop_application_id(),
                                           APP_ID,
                                           prop_flags(),
                                           Gio::Application::Flags::DEFAULT_FLAGS);
    }

} // namespace mnn
