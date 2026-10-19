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

#include <boost/ut.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "http_client.hpp"
#include "mnn_error.hpp"
#include "nominatim.hpp"
#include "peeler/test_loop.hpp"
#include "result_future.hpp"
#include "util.hpp"
#include <peel/Dex/Timeout.h>
#include <peel/Dex/functions.h>
#include <peel/GLib/Bytes.h>
#include <peel/GLib/MainLoop.h>
#include <peel/GLib/Uri.h>
#include <peel/Gio/Cancellable.h>
#include <peel/Shumate/Location.h>
#include <peel/Soup/Message.h>
#include <peel/Soup/Status.h>

using namespace peel;
using namespace std::chrono_literals;

namespace
{
    constexpr std::string_view hit_body  = R"([{"lat":"37.4034","lon":"-122.0644","display_name":"Mountain View"}])";
    constexpr std::string_view zip_body  = R"([{"lat":"37.4","lon":"-122.07","type":"postcode"}])";
    constexpr std::string_view none_body = "[]";

    mnn::PostalAddress
    address(std::string line1, std::string line2 = "MOUNTAIN VIEW, CA 94043")
    {
        return { std::move(line1), std::move(line2), "" };
    }

    /* A fake Nominatim: `respond` picks the reply per URL; each reply arrives
     * `delay` after the request. Records when requests start and finish. */
    struct FakeNominatim
    {
        struct Request
        {
            std::string  url;
            std::int64_t started_us  = 0;
            std::int64_t finished_us = 0;
        };

        std::function<std::pair<Soup::Status, std::string>(const std::string &url)> respond =
            [](const std::string &) { return std::pair { Soup::Status::OK, std::string { hit_body } }; };
        int                  delay_ms = 0;
        int                  in_flight = 0;
        int                  max_in_flight = 0;
        std::vector<Request> requests;

        RefPtr<mnn::HttpClient>
        client()
        {
            return mnn::HttpClient::create_for_test([this](RefPtr<Soup::Message> msg) {
                auto url = std::string { to_sview(msg->get_uri()->to_string()) };
                requests.push_back({ url, g_get_monotonic_time(), 0 });
                max_in_flight = std::max(max_in_flight, ++in_flight);

                auto [status, body] = respond(url);
                auto bytes = GLib::Bytes::create({ reinterpret_cast<const uint8_t *>(body.data()), body.size() });
                mnn::HttpReply reply { status, std::move(bytes) };
                auto index = requests.size() - 1;
                return peeler::into_future<mnn::HttpReply>(Dex::Future::finally_(
                    Dex::Timeout::create_msec(delay_ms),
                    [this, index, reply](Dex::Future *) -> RefPtr<Dex::Future> {
                        requests[index].finished_us = g_get_monotonic_time();
                        --in_flight;
                        return peeler::just<mnn::HttpReply>(reply);
                    }));
            });
        }

        RefPtr<mnn::NominatimGeocoder>
        geocoder(std::chrono::milliseconds cooldown = 10ms)
        {
            return mnn::NominatimGeocoder::create(client(), cooldown);
        }
    };

    /* Await a geocode as an error_code result, as PositionLookup's callers see it. */
    mnn::result_future<mnn::Position>
    coded(peeler::future<mnn::Position> f)
    {
        return mnn::result_future<mnn::Position> { std::move(f) };
    }

    double latitude(const mnn::Position &p) { return p.coordinate->cast<Shumate::Location>()->get_latitude(); }

    bool contains(const std::string &haystack, std::string_view needle) { return haystack.find(needle) != std::string::npos; }

} // namespace

int main()
{
    using namespace boost::ut;

    Dex::init();
    peeler::install_test_log_handler();
    Type::of<mnn::NominatimGeocoder>().ensure();

    "street match"_test = [] {
        FakeNominatim nominatim;
        peeler::run_coro([&](GLib::MainLoop *) -> peeler::future<bool> {
            auto geocoder = nominatim.geocoder();
            auto pos = co_await coded(geocoder->geocode(address("528 EASY ST APT C")));
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 37.4034));
            expect(pos->precision == mnn::Precision::precise);
            expect(fatal(eq(nominatim.requests.size(), 1uz)));
            const auto &url = nominatim.requests[0].url;
            expect(url.starts_with("https://nominatim.openstreetmap.org/search?")) << url;
            expect(contains(url, "q=528%20EASY%20ST%20APT%20C%2C%20MOUNTAIN%20VIEW%2C%20CA%2094043")) << url;
            expect(contains(url, "format=jsonv2")) << url;
            expect(contains(url, "countrycodes=us")) << url;
            co_return true;
        });
    };

    "no street match falls back to the ZIP centroid"_test = [] {
        FakeNominatim nominatim;
        nominatim.respond = [](const std::string &url) {
            return std::pair { Soup::Status::OK, std::string { contains(url, "postalcode=") ? zip_body : none_body } };
        };
        peeler::run_coro([&](GLib::MainLoop *) -> peeler::future<bool> {
            auto geocoder = nominatim.geocoder();
            auto pos = co_await coded(geocoder->geocode(address("PO BOX 1", "MOUNTAIN VIEW, CA 94043-1234")));
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 37.4));
            expect(pos->precision == mnn::Precision::approximate);
            expect(fatal(eq(nominatim.requests.size(), 2uz)));
            expect(contains(nominatim.requests[1].url, "postalcode=94043&country=us")) << nominatim.requests[1].url;
            co_return true;
        });
    };

    "nothing found is no_coordinates"_test = [] {
        FakeNominatim nominatim;
        nominatim.respond = [](const std::string &) { return std::pair { Soup::Status::OK, std::string { none_body } }; };
        peeler::run_coro([&](GLib::MainLoop *) -> peeler::future<bool> {
            auto geocoder = nominatim.geocoder();
            auto both = co_await coded(geocoder->geocode(address("PO BOX 1")));
            expect(!both && both.error() == mnn::error::no_coordinates);
            expect(eq(nominatim.requests.size(), 2uz));

            /* No ZIP to fall back to: one request only. */
            auto no_zip = co_await coded(geocoder->geocode(address("PO BOX 1", "SOMEWHERE")));
            expect(!no_zip && no_zip.error() == mnn::error::no_coordinates);
            expect(eq(nominatim.requests.size(), 3uz));
            co_return true;
        });
    };

    "error mapping"_test = [] {
        FakeNominatim nominatim;
        peeler::run_coro([&](GLib::MainLoop *) -> peeler::future<bool> {
            auto geocoder = nominatim.geocoder();
            auto reply_with = [&](Soup::Status status, std::string_view body) {
                nominatim.respond = [status, body = std::string { body }](const std::string &) {
                    return std::pair { status, body };
                };
            };

            reply_with(static_cast<Soup::Status>(429), ""); /* not in libsoup's enum */
            auto throttled = co_await coded(geocoder->geocode(address("1 MAIN ST")));
            expect(!throttled && throttled.error() == mnn::error::lookup_unavailable);

            reply_with(Soup::Status::OK, "<html>");
            auto garbage = co_await coded(geocoder->geocode(address("1 MAIN ST")));
            expect(!garbage && garbage.error() == mnn::error::bad_response);

            reply_with(Soup::Status::OK, R"([{"lat":1}])");
            auto wrong_types = co_await coded(geocoder->geocode(address("1 MAIN ST")));
            expect(!wrong_types && wrong_types.error() == mnn::error::bad_response);

            reply_with(Soup::Status::OK, R"({"lat":"1","lon":"2"})");
            auto not_array = co_await coded(geocoder->geocode(address("1 MAIN ST")));
            expect(!not_array && not_array.error() == mnn::error::bad_response);
            co_return true;
        });
    };

    "concurrent geocodes are serialized and spaced by the cooldown"_test = [] {
        FakeNominatim nominatim;
        nominatim.delay_ms = 30;
        peeler::run_coro([&](GLib::MainLoop *) -> peeler::future<bool> {
            auto geocoder = nominatim.geocoder(100ms);
            auto a = coded(geocoder->geocode(address("1 A ST")));
            auto b = coded(geocoder->geocode(address("2 B ST")));
            auto c = coded(geocoder->geocode(address("3 C ST")));
            auto d = coded(geocoder->geocode(address("4 D ST")));
            expect((co_await a).has_value());
            expect((co_await b).has_value());
            expect((co_await c).has_value());
            expect((co_await d).has_value());

            expect(eq(nominatim.max_in_flight, 1));
            expect(fatal(eq(nominatim.requests.size(), 4uz)));
            const char *order[] = { "1%20A%20ST", "2%20B%20ST", "3%20C%20ST", "4%20D%20ST" };
            for (std::size_t i = 0; i < 4; ++i) {
                expect(contains(nominatim.requests[i].url, order[i])) << "request" << i << "out of order";
                if (i > 0) {
                    auto gap_us = nominatim.requests[i].started_us - nominatim.requests[i - 1].finished_us;
                    expect(ge(gap_us, 99'000)) << "request" << i << "started" << gap_us << "us after the previous finished";
                }
            }
            co_return true;
        });
    };

    "a cancelled waiter leaves the line without costing a cooldown"_test = [] {
        FakeNominatim nominatim;
        nominatim.delay_ms = 30;
        peeler::run_coro([&](GLib::MainLoop *) -> peeler::future<bool> {
            auto geocoder = nominatim.geocoder(100ms);
            auto cancel_b = Gio::Cancellable::create();
            auto a = coded(geocoder->geocode(address("1 A ST")));
            auto b = coded(geocoder->geocode(address("2 B ST"), cancel_b));
            auto c = coded(geocoder->geocode(address("3 C ST")));
            cancel_b->cancel();

            expect((co_await a).has_value());
            auto rb = co_await b;
            expect(!rb && rb.error() == mnn::error::cancelled);
            expect((co_await c).has_value());

            expect(eq(nominatim.max_in_flight, 1));
            expect(fatal(eq(nominatim.requests.size(), 2uz)));
            expect(contains(nominatim.requests[1].url, "3%20C%20ST")) << nominatim.requests[1].url;
            auto gap_us = nominatim.requests[1].started_us - nominatim.requests[0].finished_us;
            expect(ge(gap_us, 99'000)) << "C started" << gap_us << "us after A finished";
            expect(lt(gap_us, 170'000)) << "C waited an extra cooldown for cancelled B:" << gap_us << "us";
            co_return true;
        });
    };
}
