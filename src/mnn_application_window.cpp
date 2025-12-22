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

#include "mnn.hpp"
#include "mnn_application_window.hpp"
#include "station.hpp"
#include <glib/gi18n.h>
#include <peel/widget-template.h>
#include <ranges>

namespace mnn
{
    using namespace peel;
    using namespace std::literals;

    PEEL_CLASS_IMPL (ApplicationWindow, "MNNApplicationWindow", Adw::ApplicationWindow)

    void
    ApplicationWindow::vfunc_dispose()
    {
        // Popovers need to be unparented
        m.date_entry_popover->unparent();
        m.date_entry_popover = nullptr;

        dispose_template(Type::of<ApplicationWindow> ());
        parent_vfunc_dispose<ApplicationWindow> ();
    }

    void
    ApplicationWindow::Class::init ()
    {
        override_vfunc_dispose<ApplicationWindow>();
        /*
        install_action ("win.new-tab", nullptr, [] (Gtk::Widget *widget, const char *action_name, GLib::Variant *parameter)
        {
            (void) action_name;
            (void) parameter;
            widget->cast<Window> ()->open_new_tab ();
        });

        add_binding_action (GDK_KEY_T, Gdk::ModifierType::CONTROL_MASK, "win.new-tab", nullptr);
        */
        set_template_from_resource("/radio/ki6kvz/MondayNightNet/mnn-app-window.ui");

        PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.date_entry, "date-entry");
        PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.date_entry_popover, "date-entry-popover");
        PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.date_entry_calendar, "date-entry-calendar");
        PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.frequency_entry, "frequency-entry");
        PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.columns_flowbox, "columns-flowbox");
        PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.toast_overlay, "toast-overlay");
        PEEL_WIDGET_TEMPLATE_BIND_CALLBACK (ApplicationWindow, on_calendar_day_selected);
        PEEL_WIDGET_TEMPLATE_BIND_CALLBACK (ApplicationWindow, on_date_entry_icon_pressed);
    }

    void
    ApplicationWindow::init(Class *)
    {
        new (&m) Members;
        init_template();
        m.settings = Gio::Settings::create("radio.ki6kvz.MondayNightNet.State");
        m.settings->bind("width", this, "default-width", Gio::Settings::BindFlags::DEFAULT);
        m.settings->bind("height", this, "default-height", Gio::Settings::BindFlags::DEFAULT);
        m.settings->bind("is-maximized", this, "maximized", Gio::Settings::BindFlags::DEFAULT);
        m.settings->bind("is-fullscreen", this, "fullscreened", Gio::Settings::BindFlags::DEFAULT);

        auto net = m.settings->get_string("current-net");
        if (!net || net == "default-net.json") {
            auto json = mnn::get_default_net();
            setup_net(json);
            on_calendar_day_selected(m.date_entry_calendar);
        }
    }

    void
    ApplicationWindow::on_date_entry_icon_pressed(Gtk::Entry*, Gtk::Entry::IconPosition pos)
    {
        if (Gtk::Entry::IconPosition::SECONDARY == pos) {
            m.date_entry_popover->popup();
        }
    }

    void
    ApplicationWindow::on_calendar_day_selected(Gtk::Calendar* cal)
    {
        auto date = cal->get_date();
        auto str = date->format("%F");
        m.date_entry->get_buffer()->set_text(str, -1);
    }

    void
    ApplicationWindow::setup_net(const nlohmann::json& j)
    {
        if (!j.contains("meta") || !j["meta"].contains("version") || 1 != j["meta"]["version"])
        {
            auto toast = Adw::Toast::create(_("Invalid Net Definition provided. no meta or version invalid"));
            m.toast_overlay->add_toast(toast);
            return;
        }
        if (!j.contains("columns")) {
            auto toast = Adw::Toast::create(_("Invalid Net Definition provided: No columns defined"));
            m.toast_overlay->add_toast(toast);
            return;
        }
        auto valid_col = [](const auto& j) { return j.contains("begin") && j.contains("end"); };
        auto vol_view = std::views::filter(j["columns"], valid_col);
        if (0UZ == std::ranges::distance(vol_view)) {
            auto toast = Adw::Toast::create(_("Invalid Net Definition provided: No valid columns (need begin and end)"));
            m.toast_overlay->add_toast(toast);
            return;
        }
        auto store = Gio::ListStore::create(Type::of<Station>());
        for (const auto& s : j["stations"]) {
            auto station = Station::create(s);
            store->append(station);
        }
        auto callsign_factory = Gtk::BuilderListItemFactory::create_from_resource(nullptr, "/radio/ki6kvz/MondayNightNet/mnn-callsign-list-item-factory.ui");

        for (const auto& col : vol_view) {
            auto filter = Gtk::CustomFilter::create([begin = col["begin"].get<std::string>(), end = col["end"].get<std::string>()]
                                                    (Object* o) -> bool {
                auto station = reinterpret_cast<Station*>(o);
                auto suf = station->get_property(Station::prop_suffix());
                return suf && *suf >= begin.front() && *suf <= end.front();
            });
            auto filter_model = Gtk::FilterListModel::create(store, filter);
            auto selection_model = Gtk::NoSelection::create(filter_model);
            auto view = Gtk::ColumnView::create(selection_model);

            auto callsign_column = Gtk::ColumnView::Column::create(_("Callsign"), callsign_factory);
            view->append_column(callsign_column);
            m.columns_flowbox->append(view);
        }


    }

    ApplicationWindow*
    ApplicationWindow::create (Adw::Application *app)
    {
        return Object::create<ApplicationWindow>(prop_application(), app);
    }

} // namespace mnn
