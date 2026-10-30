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

using namespace std::literals;

namespace mnn
{
  using namespace peel;
  namespace GObject = peel::GObject;

  PEEL_CLASS_IMPL (ApplicationWindow, "MNNApplicationWindow", Adw::ApplicationWindow)

  void
  ApplicationWindow::vfunc_dispose()
  {
    m.columns.clear();
    m.city_totals.clear();
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
    install_action ("win.save-net", nullptr, [] (Gtk::Widget *widget, const char *, GLib::Variant *)
    {
      widget->cast<ApplicationWindow> ()->save_net ();
    });
    add_binding_action (GDK_KEY_s, Gdk::ModifierType::CONTROL_MASK, "win.save-net", nullptr);
    install_action ("win.open-net", nullptr, [] (Gtk::Widget *widget, const char *, GLib::Variant *)
    {
      widget->cast<ApplicationWindow> ()->open_net ();
    });
    add_binding_action (GDK_KEY_o, Gdk::ModifierType::CONTROL_MASK, "win.open-net", nullptr);

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
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.city_totals_section, "city-totals-section");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD (ApplicationWindow, m.city_totals_flowbox, "city-totals-flowbox");    PEEL_WIDGET_TEMPLATE_BIND_CALLBACK (ApplicationWindow, on_calendar_day_selected);
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
  ApplicationWindow::on_date_entry_icon_pressed(Gtk::Entry* entry, Gtk::Entry::IconPosition pos)
  {
    if (Gtk::Entry::IconPosition::SECONDARY == pos) {
      // The entry fills the popover's parent box, so entry coords == box coords
      Gdk::Rectangle area;
      entry->get_icon_area(pos, &area);
      m.date_entry_popover->set_pointing_to(&area);
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
    if (build_net(j["columns"], j.value("stations", nlohmann::json::array())))
      build_city_totals(j.value("totals", nlohmann::json::array()), nlohmann::json::object());
  }

  bool
  ApplicationWindow::build_net(const nlohmann::json& columns, const nlohmann::json& stations)
  {
    auto valid_bound = [](const nlohmann::json& b) { return b.is_string() && !b.get_ref<const std::string&>().empty(); };
    auto valid_col = [valid_bound](const nlohmann::json& j) {
      return j.is_object() && j.contains("begin") && j.contains("end") && valid_bound(j["begin"]) && valid_bound(j["end"]);
    };
    if (!columns.is_array() || 0 == std::ranges::count_if(columns, valid_col)) {
      toast(_("Invalid Net Definition provided: No valid columns (need begin and end)"));
      return false;
    }
    auto col_view = std::views::filter(columns, valid_col);

    // Build every station before touching the current net, so a bad record leaves it as it was.
    auto store = Gio::ListStore::create(Type::of<Station>());
    try {
      if (!stations.is_array())
        throw std::invalid_argument(_("'stations' is not a list"));
      for (const auto& s : stations) {
        auto station = Station::create(s);
        store->append(station);
      }
    } catch (const std::exception& e) {
      std::string what { e.what() };
      toast(std::vformat(_("Invalid Net Definition provided: {}"), std::make_format_args(what)).c_str());
      return false;
    }

    m.columns.clear();
    m.columns_flowbox->remove_all();
    m.column_defs = columns;

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
    return true;
  }

  void
  ApplicationWindow::toast(const char* text)
  {
    m.toast_overlay->add_toast(Adw::Toast::create(text));
  }

  nlohmann::json
  ApplicationWindow::net_to_json() const
  {
    auto stations = nlohmann::json::array();
    auto n = m.store ? m.store->get_n_items() : 0u;
    for (unsigned i = 0; i < n; ++i) {
      RefPtr<Object> item = m.store->get_object(i);
      stations.push_back(item->cast<Station>()->to_json());
    }
    auto totals = nlohmann::json::array();
    auto total_counts = nlohmann::json::object();
    for (const auto& city : m.city_totals) {
      totals.push_back(city.name);
      total_counts[city.name] = city.spin->get_value_as_int();
    }
    return {
      {"version", 1},
      {"date", std::string { to_sview(m.date_entry->get_buffer()->get_text()) }},
      {"frequency", std::string { to_sview(m.frequency_entry->get_buffer()->get_text()) }},
      {"ss_callsign", std::string { to_sview(m.callsign_entry_buffer->get_text()) }},
      {"ss_name", std::string { to_sview(m.name_entry_buffer->get_text()) }},
      {"columns", m.column_defs},
      {"map", m.map_view->view_to_json()},
      {"totals", std::move(totals)},
      {"total_counts", std::move(total_counts)},
      {"stations", std::move(stations)},
    };
  }

  namespace
  {
    RefPtr<Gtk::FileDialog>
    make_net_file_dialog(const char* title)
    {
      auto filter = Gtk::FileFilter::create();
      filter->set_name(_("Net files (JSON)"));
      filter->add_suffix("json");
      auto filters = Gio::ListStore::create(Type::of<Gtk::FileFilter>());
      filters->append(filter);

      auto dialog = Gtk::FileDialog::create();
      dialog->set_title(title);
      dialog->set_filters(filters);
      dialog->set_default_filter(filter);
      return dialog;
    }

    bool
    is_dismissed(const UniquePtr<GLib::Error>& error)
    {
      return error->domain == GTK_DIALOG_ERROR && error->code == GTK_DIALOG_ERROR_DISMISSED;
    }
  }

  void
  ApplicationWindow::save_net()
  {
    if (!m.store) return;

    auto dialog = make_net_file_dialog(_("Save Net"));
    auto date = std::string { to_sview(m.date_entry->get_buffer()->get_text()) };
    auto initial = date.empty() ? "net.json"s : std::format("net-{}.json", date);
    dialog->set_initial_name(initial.c_str());

    RefPtr<ApplicationWindow> self { this };
    dialog->save(this, nullptr, [self, dialog] (GObject::Object*, Gio::AsyncResult* res) {
      UniquePtr<GLib::Error> error;
      RefPtr<Gio::File> file = dialog->save_finish(res, &error);
      if (!file) {
        if (error && !is_dismissed(error))
          self->toast(error->message);
        return;
      }

      auto text = self->net_to_json().dump(2) + "\n";
      ArrayRef<const uint8_t> contents { reinterpret_cast<const uint8_t*>(text.data()), text.size() };
      if (!file->replace_contents(contents, nullptr, false, Gio::File::CreateFlags::NONE, nullptr, nullptr, &error)) {
        self->toast(error->message);
        return;
      }
      auto name = std::string { to_sview(file->get_basename()) };
      self->toast(std::vformat(_("Saved to {}"), std::make_format_args(name)).c_str());
    });
  }

  void
  ApplicationWindow::open_net()
  {
    auto dialog = make_net_file_dialog(_("Open Net"));
    RefPtr<ApplicationWindow> self { this };
    dialog->open(this, nullptr, [self, dialog] (GObject::Object*, Gio::AsyncResult* res) {
      UniquePtr<GLib::Error> error;
      RefPtr<Gio::File> file = dialog->open_finish(res, &error);
      if (!file) {
        if (error && !is_dismissed(error))
          self->toast(error->message);
        return;
      }
      self->load_net(file);
    });
  }

  void
  ApplicationWindow::load_net(Gio::File* file)
  {
    UniquePtr<uint8_t[]> contents;
    UniquePtr<GLib::Error> error;
    if (!file->load_contents(nullptr, &contents, nullptr, &error)) {
      toast(error->message);
      return;
    }

    auto name = std::string { to_sview(file->get_basename()) };
    auto json = nlohmann::json::parse(contents.data(), contents.data() + contents.size(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
      toast(std::vformat(_("{} is not a valid JSON file"), std::make_format_args(name)).c_str());
      return;
    }
    if (!json.contains("version") || 1 != json["version"]) {
      toast(std::vformat(_("{} is not a saved net, or is from a newer version"), std::make_format_args(name)).c_str());
      return;
    }
    if (!json.contains("columns")) {
      toast(_("Invalid Net Definition provided: No columns defined"));
      return;
    }
    if (!build_net(json["columns"], json.value("stations", nlohmann::json::array())))
      return;
    build_city_totals(json.value("totals", nlohmann::json::array()),
                      json.value("total_counts", nlohmann::json::object()));

    auto text = [&json] (const char* key) {
      auto it = json.find(key);
      return it != json.end() && it->is_string() ? it->get<std::string>() : std::string {};
    };
    m.date_entry->get_buffer()->set_text(text("date").c_str(), -1);
    m.frequency_entry->get_buffer()->set_text(text("frequency").c_str(), -1);
    m.callsign_entry_buffer->set_text(text("ss_callsign").c_str(), -1);
    m.name_entry_buffer->set_text(text("ss_name").c_str(), -1);

    toast(std::vformat(_("Loaded {}"), std::make_format_args(name)).c_str());
  }

  void
  ApplicationWindow::update_grand_total() noexcept
  {
    auto totals = m.columns | std::views::transform([] (const ColumnWatch& c) { return c.column->get_total(); });
    auto sum = std::ranges::fold_left(totals, 0u, std::plus{});
    m.grand_total_label->set_label(std::format("{}", sum).c_str());
  }

  void
  ApplicationWindow::build_city_totals(const nlohmann::json& names, const nlohmann::json& counts)
  {
    m.city_totals.clear();
    m.city_totals_flowbox->remove_all();

    if (names.is_array()) {
      for (const auto& n : names) {
        if (!n.is_string())
          continue;
        auto name = n.get<std::string>();
        if (name.empty() || std::ranges::contains(m.city_totals, name, &CityTotal::name))
          continue;

        auto box = Gtk::Box::create(Gtk::Orientation::HORIZONTAL, 6);
        auto label = Gtk::Label::create(name.c_str());
        label->set_xalign(0);
        label->set_hexpand(true);
        box->append(std::move(label));

        auto spin = Gtk::SpinButton::create_with_range(0, 999, 1);
        spin->set_numeric(true);
        if (counts.is_object() && counts.contains(name) && counts[name].is_number_unsigned())
          spin->set_value(counts[name].get<unsigned>());
        Gtk::SpinButton* spin_ptr = spin;
        box->append(std::move(spin));

        m.city_totals_flowbox->append(box);
        m.city_totals.push_back({std::move(name), spin_ptr});
      }
    }
    m.city_totals_section->set_visible(!m.city_totals.empty());
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
    dialog->set_extra_child(std::move(list));

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
    Object::bind_property(window->m.callsign_entry_buffer, Gtk::EntryBuffer::prop_text(),
                          window->m.map_view, StationMapView::prop_home_callsign(),
                          GObject::BindingFlags::SYNC_CREATE);
    return window;
  }

} // namespace mnn
