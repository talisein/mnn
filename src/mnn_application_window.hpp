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

#include <peel/Adw/Adw.h>
#include <peel/class.h>

namespace mnn
{
    class ApplicationWindow final : public peel::Adw::ApplicationWindow
    {
        PEEL_SIMPLE_CLASS (ApplicationWindow, peel::Adw::ApplicationWindow);

        void init (Class *);

    public:
        [[nodiscard]] static ApplicationWindow* create(peel::Adw::Application *);
    };

} // namespace mnn
