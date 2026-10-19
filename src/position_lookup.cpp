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

#include "position_lookup.hpp"
#include "coordinates.hpp"
#include "mnn_gerror.hpp"
#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <ranges>
#include <string_view>
#include <variant>
#include <nlohmann/json.hpp>
#include <peel/GLib/Bytes.h>
#include <peel/GLib/Error.h>
#include <peel/GObject/Type.h>
#include <peel/GObject/Value.h>
#include <peel/Gom/MutationResult.h>
#include <peel/Soup/Message.h>
#include <peel/Soup/Status.h>

PEEL_CLASS_IMPL(mnn::PositionLookup, "MnnPositionLookup", peel::GObject::Object)

namespace mnn
{
    using namespace peel;
    using std::chrono::sys_seconds;

    namespace
    {
        struct LatLon
        {
            double latitude;
            double longitude;
        };

        using CallookResult = std::variant<LatLon, PositionLookup::PostalAddress>;

        constexpr bool is_ascii_space(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
        constexpr bool is_ascii_upper(char c) { return c >= 'A' && c <= 'Z'; }
        constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
        constexpr char to_ascii_upper(char c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }

        /* Trim and upper-case. Accepts 3-10 characters of [A-Z0-9] with at
         * least one letter and one digit. */
        std::expected<std::string, error>
        normalize_callsign(std::string_view raw)
        {
            auto call = raw
                | std::views::drop_while(is_ascii_space)
                | std::views::reverse
                | std::views::drop_while(is_ascii_space)
                | std::views::reverse
                | std::views::transform(to_ascii_upper)
                | std::ranges::to<std::string>();

            if (call.size() < 3 || call.size() > 10)
                return std::unexpected(error::invalid_callsign);
            if (!std::ranges::all_of(call, [](char c) { return is_ascii_upper(c) || is_ascii_digit(c); }))
                return std::unexpected(error::invalid_callsign);
            if (!std::ranges::any_of(call, is_ascii_upper) || !std::ranges::any_of(call, is_ascii_digit))
                return std::unexpected(error::invalid_callsign);
            return call;
        }

        /* The json helpers below never throw: an exception escaping a
         * peeler::future coroutine is std::terminate. */
        std::string_view
        string_field(const nlohmann::json &obj, const char *key)
        {
            if (!obj.is_object())
                return {};
            auto it = obj.find(key);
            if (it == obj.end() || !it->is_string())
                return {};
            return it->get_ref<const std::string &>();
        }

        const nlohmann::json &
        object_field(const nlohmann::json &obj, const char *key)
        {
            static const nlohmann::json empty = nlohmann::json::object();
            if (!obj.is_object())
                return empty;
            auto it = obj.find(key);
            return (it != obj.end() && it->is_object()) ? *it : empty;
        }

        /* callook's answer: coordinates, else a postal address, else an error.
         * Shape: {"status":"VALID", "location":{"latitude":"37.4", ...},
         *         "address":{"line1":..,"line2":..,"attn":..}, ...}
         * Non-VALID responses carry only "status". */
        std::expected<CallookResult, error>
        resolve_from_callook(const nlohmann::json &j)
        {
            auto status = string_field(j, "status");
            if (status == "INVALID")
                return std::unexpected(error::callsign_not_found);
            if (status == "UPDATING")
                return std::unexpected(error::lookup_unavailable);
            if (status != "VALID")
                return std::unexpected(error::bad_response);

            const auto &location = object_field(j, "location");
            auto latitude  = parse_degrees(string_field(location, "latitude"), 90.0);
            auto longitude = parse_degrees(string_field(location, "longitude"), 180.0);
            if (latitude && longitude)
                return LatLon { *latitude, *longitude };

            const auto &address = object_field(j, "address");
            PositionLookup::PostalAddress postal {
                std::string { string_field(address, "line1") },
                std::string { string_field(address, "line2") },
                std::string { string_field(address, "attn") },
            };
            if (!postal.line1.empty() || !postal.line2.empty())
                return postal;

            return std::unexpected(error::no_coordinates);
        }

        RefPtr<Dex::Future>
        find_by_callsign(Gom::Repository *repo, GObject::Type type, const std::string &callsign)
        {
            /* gom builds the query, copying the value, before returning. */
            const char *props[] = { "callsign" };
            GObject::Value value { GObject::Type::of<const char *>() };
            value.set<const char *>(callsign.c_str());
            return repo->find_one_with_properties(type, props, { &value, 1 });
        }

        /* Constraint failures happen when two lookups of one call race to
         * insert; the other lookup stored the row, so that's not news. */
        void
        log_write_failure(std::string_view what, const std::string &callsign, const GLib::Error &err)
        {
            if (err.matches(GOM_ERROR, GOM_ERROR_CONSTRAINT))
                g_debug("%.*s for %s raced another lookup: %s",
                        static_cast<int>(what.size()), what.data(), callsign.c_str(), err.message);
            else
                g_message("%.*s for %s failed: %s",
                          static_cast<int>(what.size()), what.data(), callsign.c_str(), err.message);
        }

        Position
        to_position(const CallsignPosition &row)
        {
            return { Shumate::Coordinate::create_full(row.get_latitude(), row.get_longitude()),
                     row.get_precision() };
        }

    } // namespace

    void
    PositionLookup::Class::init()
    {
        override_vfunc_dispose<PositionLookup>();
    }

    void
    PositionLookup::vfunc_dispose()
    {
        m.repo     = nullptr;
        m.http     = nullptr;
        m.geocoder = nullptr;
        m.in_flight.clear();
        parent_vfunc_dispose<PositionLookup>();
    }

    RefPtr<PositionLookup>
    PositionLookup::create(RefPtr<Gom::Repository> repo, RefPtr<HttpClient> http, Clock now,
                           RefPtr<NominatimGeocoder> geocoder)
    {
        auto self = GObject::Object::create<PositionLookup>();
        self->m.repo     = std::move(repo);
        self->m.geocoder = geocoder ? std::move(geocoder) : NominatimGeocoder::create(http);
        self->m.http     = std::move(http);
        if (now)
            self->m.now = std::move(now);
        else
            self->m.now = [] { return std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()); };
        return self;
    }

    result_future<Position>
    PositionLookup::lookup(std::string_view raw_callsign, RefPtr<Gio::Cancellable> cancel)
    {
        auto callsign = normalize_callsign(raw_callsign);
        if (!callsign)
            return result_future<Position> {
                peeler::reject(make_gerror(callsign.error(), raw_callsign))
                | peeler::into_future<Position>()
            };

        /* Join only a lookup that is still running. Awaiters can be resumed
         * inline when it resolves, before lookup_impl has unwound and erased
         * its entry, so a settled entry may still be here: replace it. */
        auto it = m.in_flight.find(*callsign);
        if (it != m.in_flight.end() && it->second->is_pending())
            return result_future<Position> { peeler::future<Position> { it->second } };

        RefPtr<Dex::Future> running = lookup_impl(*callsign, std::move(cancel));
        if (running->is_pending())
            m.in_flight.insert_or_assign(*callsign, running);
        return result_future<Position> { peeler::future<Position> { std::move(running) } };
    }

    peeler::future<Position>
    PositionLookup::lookup_impl(std::string callsign, RefPtr<Gio::Cancellable> cancel)
    {
        RefPtr<PositionLookup> keep { this };

        /* Leave in_flight however this coroutine ends. By the time this runs
         * our future has settled; a pending entry is a newer lookup that
         * replaced ours, and stays. */
        struct InFlightGuard
        {
            PositionLookup    *self;
            const std::string &callsign;
            ~InFlightGuard()
            {
                auto it = self->m.in_flight.find(callsign);
                if (it != self->m.in_flight.end() && !it->second->is_pending())
                    self->m.in_flight.erase(it);
            }
        } in_flight_guard { this, callsign };

        auto now = m.now();

        auto cached = co_await peeler::into_future<peeler::nullable<CallsignPosition>>(
            find_by_callsign(m.repo, GObject::Type::of<CallsignPosition>(), callsign));
        if (!cached) {
            g_message("position cache read for %s failed: %s", callsign.c_str(), cached.error()->message);
            co_return std::unexpected(make_gerror(error::database_error, cached.error()->message));
        }
        RefPtr<CallsignPosition> row = std::move(*cached);
        if (row && now - row->get_last_updated() < refresh_after)
            co_return to_position(*row);

        auto missed = co_await peeler::into_future<peeler::nullable<CallsignMiss>>(
            find_by_callsign(m.repo, GObject::Type::of<CallsignMiss>(), callsign));
        if (!missed) {
            g_message("miss cache read for %s failed: %s", callsign.c_str(), missed.error()->message);
            co_return std::unexpected(make_gerror(error::database_error, missed.error()->message));
        }
        RefPtr<CallsignMiss> miss = std::move(*missed);
        if (miss && now - miss->get_last_checked() < miss_refresh_after) {
            if (row)
                co_return to_position(*row);
            co_return std::unexpected(make_gerror(static_cast<error>(miss->get_reason()), callsign));
        }

        auto fetched = co_await fetch_position(callsign, miss, now, cancel);
        if (!fetched) {
            /* A stale position beats no position, unless the caller gave up. */
            if (row && to_error_code(*fetched.error()) != error::cancelled) {
                g_debug("refresh of %s failed, using stale position: %s",
                        callsign.c_str(), fetched.error()->message);
                co_return to_position(*row);
            }
            co_return std::unexpected(std::move(fetched).error());
        }

        auto position  = std::move(*fetched);
        auto *location = position.coordinate->cast<Shumate::Location>();
        co_await store_position(callsign, std::move(row), std::move(miss),
                                location->get_latitude(), location->get_longitude(),
                                position.precision, now);
        co_return position;
    }

    peeler::future<Position>
    PositionLookup::fetch_position(std::string callsign, RefPtr<CallsignMiss> miss,
                                   sys_seconds now, RefPtr<Gio::Cancellable> cancel)
    {
        RefPtr<PositionLookup> keep { this };

        auto url   = std::format("https://callook.info/{}/json", callsign);
        auto msg   = Soup::Message::create("GET", url.c_str());
        auto reply = co_await m.http->send_and_read(msg, cancel);
        if (!reply) {
            if (reply.error()->matches(G_IO_ERROR, G_IO_ERROR_CANCELLED))
                co_return std::unexpected(make_gerror(error::cancelled));
            g_debug("callook fetch for %s failed: %s", callsign.c_str(), reply.error()->message);
            co_return std::unexpected(make_gerror(error::network_error, reply.error()->message));
        }
        if (reply->status != Soup::Status::OK) {
            auto status = std::format("HTTP {}", static_cast<int>(reply->status));
            g_debug("callook fetch for %s got %s", callsign.c_str(), status.c_str());
            co_return std::unexpected(make_gerror(error::lookup_unavailable, status));
        }

        auto bytes = reply->body ? reply->body->get_data() : ArrayRef<const uint8_t> {};
        auto json  = nlohmann::json::parse(bytes.data(), bytes.data() + bytes.size(), nullptr, false);
        if (json.is_discarded())
            co_return std::unexpected(make_gerror(error::bad_response, "unparseable JSON"));

        auto result = resolve_from_callook(json);
        if (!result) {
            /* Only definitive "nothing here" answers are remembered; anything
             * transient (UPDATING, bad JSON) is retried next time. */
            if (result.error() == error::callsign_not_found || result.error() == error::no_coordinates)
                co_await record_miss(callsign, std::move(miss), result.error(), now);
            co_return std::unexpected(make_gerror(result.error(), callsign));
        }

        if (auto *address = std::get_if<PostalAddress>(&*result)) {
            auto geocoded = co_await position_for_address(std::move(*address), std::move(cancel));
            if (!geocoded) {
                /* Neither the street nor the ZIP matched: as definitive as
                 * callook having no address at all. */
                if (to_error_code(*geocoded.error()) == error::no_coordinates)
                    co_await record_miss(callsign, std::move(miss), error::no_coordinates, now);
                co_return std::unexpected(std::move(geocoded).error());
            }
            co_return std::move(*geocoded);
        }

        const auto &latlon = std::get<LatLon>(*result);
        co_return Position { Shumate::Coordinate::create_full(latlon.latitude, latlon.longitude),
                             Precision::precise };
    }

    peeler::future<Position>
    PositionLookup::position_for_address(PostalAddress address, RefPtr<Gio::Cancellable> cancel)
    {
        return m.geocoder->geocode(std::move(address), std::move(cancel));
    }

    peeler::future<bool>
    PositionLookup::store_position(std::string callsign, RefPtr<CallsignPosition> row,
                                   RefPtr<CallsignMiss> miss, double latitude, double longitude,
                                   Precision precision, sys_seconds now)
    {
        RefPtr<PositionLookup> keep { this };

        UniquePtr<GLib::Error> failure;
        if (row) {
            row->set_latitude(latitude);
            row->set_longitude(longitude);
            row->set_precision(precision);
            row->set_last_updated(now);
            auto updated = co_await peeler::into_future<Gom::MutationResult>(row->update());
            if (!updated)
                failure = std::move(updated).error();
        } else {
            auto fresh = GObject::Object::create<CallsignPosition>();
            fresh->set_callsign(callsign);
            fresh->set_latitude(latitude);
            fresh->set_longitude(longitude);
            fresh->set_precision(precision);
            fresh->set_last_updated(now);
            auto inserted = co_await peeler::into_future<bool>(m.repo->insert_entity(fresh));
            if (!inserted)
                failure = std::move(inserted).error();
        }
        if (failure) {
            log_write_failure("storing position", callsign, *failure);
            co_return false;
        }

        if (miss) {
            auto deleted = co_await peeler::into_future<Gom::MutationResult>(miss->delete_());
            if (!deleted)
                log_write_failure("clearing miss", callsign, *deleted.error());
        }
        co_return true;
    }

    peeler::future<bool>
    PositionLookup::record_miss(std::string callsign, RefPtr<CallsignMiss> miss, error reason,
                                sys_seconds now)
    {
        RefPtr<PositionLookup> keep { this };

        UniquePtr<GLib::Error> failure;
        if (miss) {
            miss->set_reason(std::to_underlying(reason));
            miss->set_last_checked(now);
            auto updated = co_await peeler::into_future<Gom::MutationResult>(miss->update());
            if (!updated)
                failure = std::move(updated).error();
        } else {
            auto fresh = GObject::Object::create<CallsignMiss>();
            fresh->set_callsign(callsign);
            fresh->set_reason(std::to_underlying(reason));
            fresh->set_last_checked(now);
            auto inserted = co_await peeler::into_future<bool>(m.repo->insert_entity(fresh));
            if (!inserted)
                failure = std::move(inserted).error();
        }
        if (failure) {
            log_write_failure("recording miss", callsign, *failure);
            co_return false;
        }
        co_return true;
    }

} // namespace mnn
