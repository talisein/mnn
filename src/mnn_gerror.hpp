/*
    monday-night-net: An amateur radio net monitoring utility in gtk4
    Copyright (C) 2026  Andrew Potter

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

/* Bridge between mnn::error and GError, so mnn failures can travel through
 * peeler::future rejections and come back out as std::error_code. Kept apart
 * from mnn_error.hpp so plain error_code users don't pull in GLib::Error. */

#include "mnn_error.hpp"
#include <format>
#include <string_view>
#include <system_error>
#include <utility>
#include <gio/gio.h>
#include <peel/GLib/Error.h>
#include <peel/UniquePtr.h>

namespace mnn
{
    inline GQuark
    error_quark() noexcept
    {
        return g_quark_from_static_string("mnn-error");
    }

    inline peel::UniquePtr<peel::GLib::Error>
    make_gerror(error e, std::string_view detail = {})
    {
        auto msg = make_error_code(e).message();
        if (!detail.empty())
            msg = std::format("{}: {}", msg, detail);
        return peel::GLib::Error::create_literal(error_quark(), std::to_underlying(e), msg.c_str());
    }

    /* mnn-domain errors map to their code; anything else is unexpected at
     * this boundary (internal code maps errors where they arise). */
    inline std::error_code
    to_error_code(const peel::GLib::Error &err) noexcept
    {
        if (static_cast<GQuark>(err.domain) == error_quark())
            return static_cast<error>(err.code);
        if (err.matches(G_IO_ERROR, G_IO_ERROR_CANCELLED))
            return error::cancelled;
        return error::network_error;
    }

} // namespace mnn
