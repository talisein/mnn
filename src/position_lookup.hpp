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

#include "entities.hpp"
#include "http_client.hpp"
#include "nominatim.hpp"
#include "peeler/coro.hpp"
#include "position.hpp"
#include "result_future.hpp"
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <peel/Dex/Future.h>
#include <peel/Gio/Cancellable.h>
#include <peel/GObject/Object.h>
#include <peel/Gom/Repository.h>
#include <peel/RefPtr.h>
#include <peel/Shumate/Coordinate.h>
#include <peel/Shumate/Location.h>
#include <peel/class.h>

namespace mnn
{
    /* Callsign -> position, backed by a gom cache in front of callook.info.
     *
     *   auto pos = co_await lookup->lookup("KI6KVZ", cancel);
     *   // std::expected<mnn::Position, std::error_code>
     *   // pos->coordinate, and pos->precision: approximate for a ZIP centroid
     *
     * Positions younger than refresh_after come straight from the cache. Older
     * or missing ones are fetched; if that fetch fails, a stale position is
     * still returned. "callook has nothing" results are remembered for
     * miss_refresh_after so the same call isn't re-queried on every lookup. */
    class PositionLookup final : public peel::GObject::Object
    {
        PEEL_SIMPLE_CLASS(PositionLookup, peel::GObject::Object);

    public:
        using Clock = std::move_only_function<std::chrono::sys_seconds()>;

        using PostalAddress = mnn::PostalAddress;

        static constexpr auto refresh_after      = std::chrono::days { 90 };
        static constexpr auto miss_refresh_after = std::chrono::days { 7 };

    private:
        struct Members {
            peel::RefPtr<peel::Gom::Repository> repo;
            peel::RefPtr<HttpClient>            http;
            peel::RefPtr<NominatimGeocoder>     geocoder;
            Clock                               now;
            /* Normalized callsign -> the lookup already running for it. */
            std::unordered_map<std::string, peel::RefPtr<peel::Dex::Future>> in_flight;
        } m;

        void init(Class *) { new (&m) Members; }
        void vfunc_dispose();

        peeler::future<Position>
        lookup_impl(std::string callsign, peel::RefPtr<peel::Gio::Cancellable> cancel);

        /* Query callook and resolve its answer to a position. Records misses. */
        peeler::future<Position>
        fetch_position(std::string callsign, peel::RefPtr<CallsignMiss> miss,
                       std::chrono::sys_seconds now,
                       peel::RefPtr<peel::Gio::Cancellable> cancel);

        /* callook had a postal address but no coordinates: geocode it. */
        peeler::future<Position>
        position_for_address(PostalAddress address, peel::RefPtr<peel::Gio::Cancellable> cancel);

        peeler::future<bool>
        store_position(std::string callsign, peel::RefPtr<CallsignPosition> row,
                       peel::RefPtr<CallsignMiss> miss, double latitude, double longitude,
                       Precision precision, std::chrono::sys_seconds now);

        peeler::future<bool>
        record_miss(std::string callsign, peel::RefPtr<CallsignMiss> miss, error reason,
                    std::chrono::sys_seconds now);

    public:
        /* now defaults to the system clock and geocoder to a NominatimGeocoder
         * on http; tests inject their own. */
        static peel::RefPtr<PositionLookup>
        create(peel::RefPtr<peel::Gom::Repository> repo, peel::RefPtr<HttpClient> http,
               Clock now = {}, peel::RefPtr<NominatimGeocoder> geocoder = {});

        /* Concurrent lookups of the same callsign share one request. A caller
         * that joins one already running shares its outcome, including the
         * first caller's cancellation; its own cancel is not consulted. */
        result_future<Position>
        lookup(std::string_view callsign, peel::RefPtr<peel::Gio::Cancellable> cancel = {});
    };

} // namespace mnn
