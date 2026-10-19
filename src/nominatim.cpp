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

#include "nominatim.hpp"
#include "coordinates.hpp"
#include "mnn_gerror.hpp"
#include "util.hpp"
#include <algorithm>
#include <format>
#include <string_view>
#include <nlohmann/json.hpp>
#include <peel/GLib/Bytes.h>
#include <peel/GLib/Error.h>
#include <peel/GLib/Uri.h>
#include <peel/Soup/Status.h>

PEEL_CLASS_IMPL(mnn::NominatimGeocoder, "MnnNominatimGeocoder", peel::GObject::Object)

namespace mnn
{
    using namespace peel;

    namespace
    {
        constexpr std::string_view search_url = "https://nominatim.openstreetmap.org/search";

        std::string
        escape(const std::string &s)
        {
            return std::string { to_sview(GLib::Uri::escape_string(s.c_str(), nullptr, true)) };
        }

        /* The ZIP from a callook line2 such as "MOUNTAIN VIEW, CA 94043" or
         * "... 94043-1234". */
        std::optional<std::string>
        zip_from(std::string_view line2)
        {
            auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
            while (!line2.empty() && line2.back() == ' ')
                line2.remove_suffix(1);
            if (line2.size() >= 10 && line2[line2.size() - 5] == '-'
                && std::ranges::all_of(line2.substr(line2.size() - 4), is_digit))
                line2.remove_suffix(5);
            if (line2.size() < 5)
                return std::nullopt;
            auto zip = line2.substr(line2.size() - 5);
            if (!std::ranges::all_of(zip, is_digit))
                return std::nullopt;
            if (line2.size() > 5 && line2[line2.size() - 6] != ' ')
                return std::nullopt;
            return std::string { zip };
        }

        /* Never throws: an exception escaping a peeler::future coroutine is
         * std::terminate. */
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

    } // namespace

    void
    NominatimGeocoder::Class::init()
    {
        override_vfunc_dispose<NominatimGeocoder>();
    }

    void
    NominatimGeocoder::vfunc_dispose()
    {
        m.http = nullptr;
        parent_vfunc_dispose<NominatimGeocoder>();
    }

    RefPtr<NominatimGeocoder>
    NominatimGeocoder::create(RefPtr<HttpClient> http, std::chrono::milliseconds cooldown)
    {
        auto self = GObject::Object::create<NominatimGeocoder>();
        self->m.http = std::move(http);
        self->m.cooldown.emplace(cooldown);
        return self;
    }

    peeler::future<HttpReply>
    NominatimGeocoder::send_limited(RefPtr<Soup::Message> msg, RefPtr<Gio::Cancellable> cancel)
    {
        RefPtr<NominatimGeocoder> keep { this };

        auto ticket = m.cooldown->enter();
        auto turn   = co_await ticket.wait(cancel);
        if (!turn)
            co_return std::unexpected(std::move(turn).error());

        auto reply = co_await m.http->send_and_read(std::move(msg), std::move(cancel));
        if (!reply)
            co_return std::unexpected(std::move(reply).error());
        co_return std::move(*reply);
        /* ~Ticket: the next request may start one cooldown from now. */
    }

    peeler::future<Shumate::Coordinate>
    NominatimGeocoder::search(std::string url, RefPtr<Gio::Cancellable> cancel)
    {
        RefPtr<NominatimGeocoder> keep { this };

        auto msg   = Soup::Message::create("GET", url.c_str());
        auto reply = co_await send_limited(std::move(msg), cancel);
        if (!reply) {
            auto code = to_error_code(*reply.error());
            if (code == error::cancelled)
                co_return std::unexpected(make_gerror(error::cancelled));
            g_debug("nominatim request failed: %s", reply.error()->message);
            co_return std::unexpected(make_gerror(error::network_error, reply.error()->message));
        }
        if (reply->status != Soup::Status::OK) {
            auto status = std::format("HTTP {}", static_cast<int>(reply->status));
            g_debug("nominatim request got %s", status.c_str());
            co_return std::unexpected(make_gerror(error::lookup_unavailable, status));
        }

        auto bytes = reply->body ? reply->body->get_data() : ArrayRef<const uint8_t> {};
        auto json  = nlohmann::json::parse(bytes.data(), bytes.data() + bytes.size(), nullptr, false);
        if (json.is_discarded() || !json.is_array())
            co_return std::unexpected(make_gerror(error::bad_response, "nominatim: expected a JSON array"));
        if (json.empty())
            co_return std::unexpected(make_gerror(error::no_coordinates, "nominatim: no match"));

        auto latitude  = parse_degrees(string_field(json.front(), "lat"), 90.0);
        auto longitude = parse_degrees(string_field(json.front(), "lon"), 180.0);
        if (!latitude || !longitude)
            co_return std::unexpected(make_gerror(error::bad_response, "nominatim: bad lat/lon"));
        co_return Shumate::Coordinate::create_full(*latitude, *longitude);
    }

    peeler::future<Position>
    NominatimGeocoder::geocode(PostalAddress address, RefPtr<Gio::Cancellable> cancel)
    {
        RefPtr<NominatimGeocoder> keep { this };

        auto query  = std::format("{}, {}", address.line1, address.line2);
        auto street = co_await search(std::format("{}?q={}&format=jsonv2&limit=1&countrycodes=us",
                                                  search_url, escape(query)),
                                      cancel);
        if (street)
            co_return Position { std::move(*street), Precision::precise };
        if (to_error_code(*street.error()) != error::no_coordinates)
            co_return std::unexpected(std::move(street).error());

        /* No street match (PO boxes never have one): the ZIP centroid is
         * still a useful dot on a net map. */
        auto zip = zip_from(address.line2);
        if (!zip)
            co_return std::unexpected(std::move(street).error());
        g_debug("nominatim: no street match for '%s', trying ZIP %s", query.c_str(), zip->c_str());
        auto centroid = co_await search(std::format("{}?postalcode={}&country=us&format=jsonv2&limit=1",
                                                    search_url, *zip),
                                        std::move(cancel));
        if (!centroid)
            co_return std::unexpected(std::move(centroid).error());
        co_return Position { std::move(*centroid), Precision::approximate };
    }

} // namespace mnn
