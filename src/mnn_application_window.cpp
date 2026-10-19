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
#include "mnn_callsign_list_view_cell.hpp"
#include "mnn_button_list_view_cell.hpp"
#include "mnn_station_column_view.hpp"
#include "mnn_station_map_view.hpp"
#include "mnn_application.hpp"
#include "station.hpp"
#include "callsign.hpp"
#include "util.hpp"
#include "config.hpp"
#include <glib/gi18n.h>
#include <peel/widget-template.h>
#include <peel/Gdk/Gdk.h>
#include <cctype>
#include <format>
#include <functional>
#include <ranges>

using namespace peel;
using namespace std::literals;

namespace mnn
{

  PEEL_CLASS_IMPL (ApplicationWindow, "MNNApplicationWindow", Adw::ApplicationWindow)

  void
  ApplicationWindow::vfunc_dispose()
  {
    // Popovers need to be unparented
    m.date_entry_popover->unparent();
    m.date_entry_popover = nullptr;

    m.columns.clear();
    m.store = nullptr;

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
    install_action ("win.add-station", nullptr, [] (Gtk::Widget *widget, const char *, GLib::Variant *)
    {
      widget->cast<ApplicationWindow> ()->show_add_station_dialog ();
    });
    add_binding_action (GDK_KEY_n, Gdk::ModifierType::CONTROL_MASK, "win.add-station", nullptr);

    Type::of<StationMapView>().ensure();
    set_template_from_resource(RESOURCE_BASEPATH "/mnn-app-window.ui");

    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.date_entry, "date-entry");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.date_entry_popover, "date-entry-popover");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.date_entry_calendar, "date-entry-calendar");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.frequency_entry, "frequency-entry");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.callsign_entry_buffer, "callsign-entry-buffer");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.name_entry_buffer, "name-entry-buffer");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.columns_flowbox, "columns-flowbox");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.map_view, "map-view");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.toast_overlay, "toast-overlay");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.grand_total_label, "grand-total-label");
    PEEL_WIDGET_TEMPLATE_BIND_CALLBACK (ApplicationWindow, on_calendar_day_selected);
    PEEL_WIDGET_TEMPLATE_BIND_CALLBACK (ApplicationWindow, on_date_entry_icon_pressed);
  }

  void
  ApplicationWindow::init(Class *)
  {
    new (&m) Members;
    init_template();
    m.settings = Gio::Settings::create(SCHEMA_ID);
    m.settings->bind("width", this, "default-width", Gio::Settings::BindFlags::DEFAULT);
    m.settings->bind("height", this, "default-height", Gio::Settings::BindFlags::DEFAULT);
    m.settings->bind("is-maximized", this, "maximized", Gio::Settings::BindFlags::DEFAULT);
    m.settings->bind("is-fullscreen", this, "fullscreened", Gio::Settings::BindFlags::DEFAULT);
    m.settings->bind("callsign", m.callsign_entry_buffer, "text", Gio::Settings::BindFlags::DEFAULT);
    m.settings->bind("name", m.name_entry_buffer, "text", Gio::Settings::BindFlags::DEFAULT);

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
    auto col_view = std::views::filter(j["columns"], valid_col);
    if (0UZ == std::ranges::distance(col_view)) {
      auto toast = Adw::Toast::create(_("Invalid Net Definition provided: No valid columns (need begin and end)"));
      m.toast_overlay->add_toast(toast);
      return;
    }
    auto store = Gio::ListStore::create(Type::of<Station>());
    for (const auto& s : j["stations"]) {
      auto station = Station::create(s);
      store->append(station);
    }

    auto callsign_factory = CallsignListViewCell::make_factory();
    auto button_factory = ButtonListViewCell::make_factory();

    // Keep every column on one row; the map takes the remaining width.
    auto n_columns = static_cast<unsigned>(std::ranges::distance(col_view));
    m.columns_flowbox->set_min_children_per_line(n_columns);
    m.columns_flowbox->set_max_children_per_line(n_columns);

    for (const auto& col : col_view) {
      auto station_col = StationColumnView::create(store,
                                                   col["begin"].get<std::string>().front(),
                                                   col["end"].get<std::string>().front());
      StationColumnView* column = station_col;
      m.columns_flowbox->append(station_col);
      SignalConnection conn = column->connect_notify(StationColumnView::prop_total(),
                                                     [this] (Object*, GObject::ParamSpec*) { update_grand_total(); });
      m.columns.push_back({column, std::move(conn)});
      /*
      auto filter = Gtk::CustomFilter::create([begin = col["begin"].get<std::string>(), end = col["end"].get<std::string>()]
                                              (Object* o) -> bool {
        auto station = o->cast<Station>();
        auto suf = station->get_property(Station::prop_suffix());
        return suf && *suf >= begin.front() && *suf <= end.front();
      });

      auto box = Gtk::Box::create(Gtk::Orientation::VERTICAL, 2);
      auto filter_model = Gtk::FilterListModel::create(store, filter);
      auto selection_model = Gtk::NoSelection::create(filter_model);
      auto view = Gtk::ColumnView::create(selection_model);
      auto column_total_box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 2);
      column_total_box->append(Gtk::Label::create(_("Column total:")));
      auto column_total_label = Gtk::Label::create("?");
      column_total_box->append(std::move(column_total_label));

      auto callsign_column = Gtk::ColumnView::Column::create(_("Callsign"), callsign_factory);
      auto button_column = Gtk::ColumnView::Column::create(_("Actions"), button_factory);
      view->append_column(callsign_column);
      view->append_column(button_column);
      box->append(std::move(view));
      box->append(std::move(column_total_box));
      m.columns_flowbox->append(box);
      */
    }
    m.map_view->set_model(store);
    m.store = std::move(store);
    update_grand_total();
  }

  void
  ApplicationWindow::update_grand_total() noexcept
  {
    auto totals = m.columns | std::views::transform([] (const ColumnWatch& c) { return c.column->get_total(); });
    auto sum = std::ranges::fold_left(totals, 0u, std::plus{});
    m.grand_total_label->set_label(std::format("{}", sum).c_str());
  }

  void
  ApplicationWindow::show_add_station_dialog()
  {
    if (!m.store) return;

    auto dialog = Adw::AlertDialog::create(_("Add Station"), _("A station that isn't on the roster, as it was heard."));
    dialog->add_response("cancel", _("_Cancel"));
    dialog->add_response("relay", _("_Relay"));
    dialog->add_response("direct", _("_Direct"));
    dialog->set_response_appearance("direct", Adw::ResponseAppearance::SUGGESTED);
    dialog->set_response_enabled("relay", false);
    dialog->set_response_enabled("direct", false);
    dialog->set_default_response("direct");
    dialog->set_close_response("cancel");

    auto callsign_row = Adw::EntryRow::create();
    callsign_row->set_title(_("Callsign"));
    callsign_row->set_input_hints(Gtk::InputHints::UPPERCASE_CHARS | Gtk::InputHints::NO_SPELLCHECK);
    callsign_row->set_activates_default(true);
    auto name_row = Adw::EntryRow::create();
    name_row->set_title(_("Name"));
    name_row->set_input_purpose(Gtk::InputPurpose::NAME);
    name_row->set_activates_default(true);

    // Owned by the dialog, which outlives every handler below.
    Adw::AlertDialog* dlg = dialog;
    Adw::EntryRow* callsign = callsign_row;
    Adw::EntryRow* name = name_row;

    callsign_row->cast<Gtk::Editable>()->connect_changed([this, dlg] (Gtk::Editable* editable) {
      auto raw = to_sview(editable->get_text());
      auto problem = raw.empty() ? std::string{} : check_new_callsign(raw);
      bool ok = !raw.empty() && problem.empty();
      dlg->set_response_enabled("relay", ok);
      dlg->set_response_enabled("direct", ok);
      if (problem.empty()) {
        editable->cast<Gtk::Widget>()->remove_css_class("error");
        dlg->set_body(_("A station that isn't on the roster, as it was heard."));
      } else {
        editable->cast<Gtk::Widget>()->add_css_class("error");
        dlg->set_body(problem.c_str());
      }
    });
    dialog->connect_response([this, callsign, name] (Adw::AlertDialog*, const char* response) {
      auto heard = to_sview(response);
      if (heard != "direct" && heard != "relay")
        return;
      auto call = normalize_callsign(to_sview(callsign->cast<Gtk::Editable>()->get_text()));
      if (call && check_new_callsign(*call).empty())
        add_station(*call, to_sview(name->cast<Gtk::Editable>()->get_text()),
                    heard == "direct" ? StationStatus::HEARD_DIRECT : StationStatus::HEARD_RELAY);
    });

    auto list = Gtk::ListBox::create();
    list->set_selection_mode(Gtk::SelectionMode::NONE);
    list->add_css_class("boxed-list");
    list->append(callsign_row);
    list->append(name_row);
    dialog->set_extra_child(list);

    dialog->present(this);
    callsign->grab_focus();
  }

  std::string
  ApplicationWindow::check_new_callsign(std::string_view raw) const
  {
    auto call = normalize_callsign(raw);
    if (!call)
      return _("Not a valid callsign.");
    // Columns sort by the letters after the digit, so there must be some.
    if (!std::isalpha(static_cast<unsigned char>(call->back())))
      return _("A callsign ends with a letter.");
    auto n = m.store->get_n_items();
    for (unsigned i = 0; i < n; ++i) {
      RefPtr<Object> item = m.store->get_object(i);
      if (item->cast<Station>()->get_callsign() == *call)
        return std::vformat(_("{} is already on the roster."), std::make_format_args(*call));
    }
    return {};
  }

  void
  ApplicationWindow::add_station(std::string_view callsign, std::string_view name, StationStatus heard)
  {
    std::string call { callsign };
    std::string nm { name };
    RefPtr<Station> station = Object::create<Station>(Station::prop_callsign(), call.c_str(),
                                                      Station::prop_name(), nm.empty() ? nullptr : nm.c_str(),
                                                      Station::prop_status(), heard);

    // Keep the roster in suffix order, as the net definition lists it.
    std::string suffix { to_sview(station->get_suffix_cstr()) };
    auto n = m.store->get_n_items();
    unsigned pos = 0;
    for (; pos < n; ++pos) {
      RefPtr<Object> item = m.store->get_object(pos);
      if (to_sview(item->cast<Station>()->get_suffix_cstr()) > suffix)
        break;
    }
    m.store->insert(pos, station);

    auto text = std::vformat(_("Added {}"), std::make_format_args(call));
    m.toast_overlay->add_toast(Adw::Toast::create(text.c_str()));
  }

  ApplicationWindow*
  ApplicationWindow::create (Application *app)
  {
    auto window = Object::create<ApplicationWindow>(prop_application(), app);
    Object::bind_property(app, Application::prop_position_lookup(),
                          window->m.map_view, StationMapView::prop_position_lookup(),
                          GObject::BindingFlags::SYNC_CREATE);
    return window;
  }

} // namespace mnn
