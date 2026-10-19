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

#include "mnn_station_column_view.hpp"
#include "mnn_callsign_list_view_cell.hpp"
#include "mnn_button_list_view_cell.hpp"
#include "config.hpp"
#include <glib/gi18n.h>
#include <peel/widget-template.h>
#include <cctype>
#include <format>
#include <ranges>

namespace mnn
{
  using namespace peel;

  PEEL_CLASS_IMPL(StationColumnView, "MNNStationColumnView", Gtk::Box);

  void StationColumnView::vfunc_dispose() {
    m.ack_watcher      = nullptr;
    dispose_template(Type::of<StationColumnView>());
    m.button_factory   = nullptr;
    m.callsign_factory = nullptr;
    m.filter           = nullptr;
    m.model            = nullptr;
    m.filter_begin     = 'A';
    m.filter_end       = 'Z';
    m.total            = 0;
    parent_vfunc_dispose<StationColumnView>();
  }

  void StationColumnView::Class::init() {
    override_vfunc_dispose<StationColumnView>();

    set_template_from_resource(RESOURCE_BASEPATH "/mnn-station-column-view.ui");

    PEEL_WIDGET_TEMPLATE_BIND_CHILD(StationColumnView, m.view, "view");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD(StationColumnView, m.filter_model, "filter_model");
    PEEL_WIDGET_TEMPLATE_BIND_CHILD(StationColumnView, m.total_label, "total-label");
  }

  void StationColumnView::init(Class*) {
    new (&m) Members;
    m.filter           = Gtk::CustomFilter::create(std::bind_front(&StationColumnView::filter_suffix, this));
    m.callsign_factory = CallsignListViewCell::make_factory();
    m.button_factory   = ButtonListViewCell::make_factory();
    init_template();
    m.ack_watcher      = ListPropertyWatcher::create(m.filter_model->cast<Gio::ListModel>(), "is-acknowledged");
    m.ack_watcher->connect_changed(this, &StationColumnView::update_total);
    update_total(m.ack_watcher);
  }

  void
  StationColumnView::update_total(ListPropertyWatcher*) noexcept
  {
    auto list = m.filter_model->cast<Gio::ListModel>();
    auto is_acknowledged = [list] (unsigned i) {
      RefPtr<Object> station = list->get_object(i);
      return station->cast<Station>()->is_acknowledged();
    };
    unsigned total = std::ranges::count_if(std::views::iota(0u, list->get_n_items()), is_acknowledged);
    m.total_label->set_label(std::format("{}", total).c_str());
    if (total == m.total) return;
    m.total = total;
    notify(prop_total());
  }

  char
  StationColumnView::get_filter_begin() const noexcept
  {
    return m.filter_begin;
  };

  void
  StationColumnView::set_filter_begin(char b) noexcept
  {
    if (!std::isalpha(static_cast<unsigned char>(b))) {
      g_warning("MNNStationColumnView: '%c' is not an alphabetic character.", b);
      return;
    }
    int upper_b = std::toupper(static_cast<unsigned char>(b));
    if (upper_b == m.filter_begin) return;
    m.filter_begin = upper_b;
    notify(prop_filter_begin());
  };

  char
  StationColumnView::get_filter_end() const noexcept
  {
    return m.filter_end;
  };

  void
  StationColumnView::set_filter_end(char e) noexcept
  {
    if (!std::isalpha(static_cast<unsigned char>(e))) {
      g_warning("MNNStationColumnView: '%c' is not an alphabetic character.", e);
      return;
    }
    int upper_e = std::toupper(static_cast<unsigned char>(e));
    if (upper_e == m.filter_end) return;
    m.filter_end = upper_e;
    notify(prop_filter_end());
  };

  bool
  StationColumnView::filter_suffix(peel::Object* o) const noexcept
  {
    auto station = o->cast<Station>();
    auto suf = station->get_property(Station::prop_suffix());
    int upper_suf = suf ? std::toupper(static_cast<unsigned char>(*suf)) : 0;
    return suf && upper_suf >= m.filter_begin && upper_suf <= m.filter_end;
  }

  FloatPtr<StationColumnView>
  StationColumnView::create(RefPtr<Gio::ListModel> store, char begin, char end) noexcept {
    return Object::create<StationColumnView>(prop_model(), std::move(store),
                                             prop_filter_begin(), static_cast<unsigned char>(begin),
                                             prop_filter_end(), static_cast<unsigned char>(end));

  }

} // namespace mnn
