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
#include "database.hpp"
#include "http_client.hpp"
#include "config.hpp"
#include <print>
#include <peel/Gdk/Gdk.h>

namespace mnn
{
    using namespace peel;

    PEEL_CLASS_IMPL (Application, "MNNApplication", Adw::Application)

    void
    Application::init(Class *)
    {
        new (&m) Members;
        RefPtr<Gio::SimpleAction> action = Gio::SimpleAction::create ("quit", nullptr);
        action->connect_activate (this, &Application::action_quit);
        cast<Gio::ActionMap> ()->add_action (action);
        set_accels_for_action ("app.quit", (const char *[]) { "<Ctrl>Q", nullptr });
    }

    void
    Application::Class::init()
    {
        override_vfunc_startup<Application>();
        override_vfunc_shutdown<Application>();
        override_vfunc_activate<Application>();
    }

    void
    Application::vfunc_startup ()
    {
        parent_vfunc_startup<Application> ();
        start_position_lookup();
    }

    void
    Application::vfunc_shutdown ()
    {
        m.position_lookup = nullptr;
        parent_vfunc_shutdown<Application> ();
    }

    peeler::future<bool>
    Application::start_position_lookup()
    {
        RefPtr<Application> keep { this };

        auto driver = open_database();
        if (!driver)
            co_return false; // open_database logged why

        auto repo = co_await peeler::into_future<Gom::Repository>(create_repository(driver));
        if (!repo) {
            g_warning("failed to create position repository: %s", repo.error()->message);
            co_return false;
        }

        m.position_lookup = PositionLookup::create(std::move(*repo), HttpClient::create());
        notify(prop_position_lookup());
        co_return true;
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
