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

#include "cooldown.hpp"
#include "http_client.hpp"
#include "peeler/coro.hpp"
#include "position.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <peel/Gio/Cancellable.h>
#include <peel/GObject/Object.h>
#include <peel/RefPtr.h>
#include <peel/Shumate/Coordinate.h>
#include <peel/Soup/Message.h>
#include <peel/class.h>

namespace mnn
{
    /* A US postal address as callook reports it, e.g.
     *   line1 "528 EASY ST APT C", line2 "MOUNTAIN VIEW, CA 94043". */
    struct PostalAddress
    {
        std::string line1;
        std::string line2;
        std::string attn;
    };

    /* Address -> coordinates via OpenStreetMap's Nominatim. Tries the street
     * address, then falls back to the ZIP code's centroid.
     *
     * Nominatim's usage policy allows at most one request per second; every
     * request goes through a Cooldown so concurrent geocodes queue up, one in
     * flight at a time, `cooldown` apart. Results must be cached by the caller
     * (PositionLookup does). Data (c) OpenStreetMap contributors, ODbL. */
    class NominatimGeocoder final : public peel::GObject::Object
    {
        PEEL_SIMPLE_CLASS(NominatimGeocoder, peel::GObject::Object);

        struct Members {
            peel::RefPtr<HttpClient> http;
            std::optional<Cooldown>  cooldown;
        } m;

        void init(Class *) { new (&m) Members; }
        void vfunc_dispose();

        /* Holds one Cooldown ticket for exactly one request. */
        peeler::future<HttpReply>
        send_limited(peel::RefPtr<peel::Soup::Message> msg, peel::RefPtr<peel::Gio::Cancellable> cancel);

        /* One /search query; rejects with no_coordinates on an empty result. */
        peeler::future<peel::Shumate::Coordinate>
        search(std::string url, peel::RefPtr<peel::Gio::Cancellable> cancel);

    public:
        static constexpr auto default_cooldown = std::chrono::milliseconds { 1500 };

        static peel::RefPtr<NominatimGeocoder>
        create(peel::RefPtr<HttpClient> http, std::chrono::milliseconds cooldown = default_cooldown);

        /* A street match is Precision::precise, a ZIP centroid approximate.
         * Rejects with mnn::error: no_coordinates when neither the street nor
         * the ZIP matched, lookup_unavailable on a non-200 (including 429),
         * network_error, bad_response, or cancelled. */
        peeler::future<Position>
        geocode(PostalAddress address, peel::RefPtr<peel::Gio::Cancellable> cancel = {});
    };

} // namespace mnn
