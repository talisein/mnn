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

#pragma once

#include <peel/Gtk/Gtk.h>
#include <peel/class.h>
#include "station.hpp"

namespace mnn
{
  class ButtonListViewCell final : public peel::Gtk::Box
  {
    PEEL_SIMPLE_CLASS(ButtonListViewCell, peel::Gtk::Box);

    void init(Class *);

    struct Members {
      peel::Gtk::ToggleButton* direct_button;
      peel::Gtk::ToggleButton* relay_button;
      peel::Gtk::ToggleButton* acknowledged_button;
    } m;

    void vfunc_dispose();

  public:
    static peel::FloatPtr<ButtonListViewCell> create() noexcept;

    static peel::RefPtr<peel::Gtk::SignalListItemFactory> make_factory() noexcept;

  private:
  };


} // namespace mnn
