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
#include <chrono>
#include <string>
#include <utility>
#include "entities.hpp"
#include "http_client.hpp"
#include "mnn_error.hpp"
#include "nominatim.hpp"
#include "peeler/test_loop.hpp"
#include "position_lookup.hpp"
#include "util.hpp"
#include <peel/Dex/functions.h>
#include <peel/GLib/Bytes.h>
#include <peel/GLib/MainLoop.h>
#include <peel/GLib/Uri.h>
#include <peel/GObject/Value.h>
#include <peel/Gom/Repository.h>
#include <peel/Shumate/Location.h>
#include <peel/Soup/Message.h>
#include <peel/Soup/Status.h>

using namespace peel;
using namespace std::chrono_literals;
using std::chrono::sys_seconds;

namespace
{
    namespace GObject = peel::GObject;
    constexpr auto t0 = sys_seconds { std::chrono::seconds { 1'760'000'000 } };

    constexpr std::string_view valid_body = R"({
        "status": "VALID",
        "address": { "line1": "528 EASY ST APT C", "line2": "MOUNTAIN VIEW, CA 94043", "attn": "" },
        "location": { "latitude": "37.403684", "longitude": "-122.06438", "gridsquare": "CM87xj" }
    })";

    constexpr std::string_view moved_body = R"({
        "status": "VALID",
        "address": { "line1": "1 MAIN ST", "line2": "NEWINGTON, CT 06111", "attn": "" },
        "location": { "latitude": "41.714", "longitude": "-72.727", "gridsquare": "FN31pr" }
    })";

    /* Real shape of a callook record with an address but no coordinates
     * (e.g. KE6BJQ in default-net.json, true of 33 of its 58 calls as of
     * 2026-10); values here are made up. */
    constexpr std::string_view address_only_body = R"({
        "status": "VALID",
        "type": "PERSON",
        "current": { "callsign": "W1AW", "operClass": "GENERAL" },
        "previous": { "callsign": "", "operClass": "" },
        "trustee": { "callsign": "", "name": "" },
        "name": "JANE Q HAM",
        "address": { "line1": "PO BOX 1", "line2": "NEWINGTON, CT 06111", "attn": "" },
        "location": { "latitude": "", "longitude": "", "gridsquare": "" },
        "otherInfo": {
            "grantDate": "01/02/2020", "expiryDate": "01/02/2030", "lastActionDate": "01/02/2020",
            "frn": "0000000000", "ulsUrl": "https://wireless2.fcc.gov/UlsApp/UlsSearch/license.jsp?licKey=0"
        }
    })";

    constexpr std::string_view nothing_body = R"({
        "status": "VALID",
        "address": { "line1": "", "line2": "", "attn": "" },
        "location": { "latitude": "", "longitude": "", "gridsquare": "" }
    })";

    constexpr std::string_view invalid_body  = R"({"status": "INVALID"})";
    constexpr std::string_view updating_body = R"({"status": "UPDATING"})";

    constexpr std::string_view nominatim_hit = R"([{"lat":"41.7147","lon":"-72.7285"}])";

    /* A fake callook and Nominatim: callook answers with status/body;
     * Nominatim with the street or ZIP reply. Both count. */
    struct FakeCallook
    {
        int                 calls = 0;
        Soup::Status        status = Soup::Status::OK;
        std::string         body { valid_body };
        std::string         last_uri;
        sys_seconds         now = t0;

        int                 nominatim_calls = 0;
        Soup::Status        nominatim_status = Soup::Status::OK;
        std::string         nominatim_street { nominatim_hit };
        std::string         nominatim_zip { "[]" };

        void reply(Soup::Status s, std::string_view b) { status = s; body = b; }

        RefPtr<mnn::HttpClient>
        client()
        {
            return mnn::HttpClient::create_for_test([this](RefPtr<Soup::Message> msg) {
                auto uri = std::string { to_sview(msg->get_uri()->to_string()) };
                auto reply = [](Soup::Status s, const std::string &b) {
                    auto bytes = GLib::Bytes::create({ reinterpret_cast<const uint8_t *>(b.data()), b.size() });
                    return peeler::just<mnn::HttpReply>(mnn::HttpReply { s, std::move(bytes) });
                };
                if (uri.starts_with("https://nominatim.openstreetmap.org/")) {
                    ++nominatim_calls;
                    bool zip = uri.find("postalcode=") != std::string::npos;
                    return reply(nominatim_status, zip ? nominatim_zip : nominatim_street);
                }
                ++calls;
                last_uri = std::move(uri);
                return reply(status, body);
            });
        }

        RefPtr<mnn::PositionLookup>
        lookup(RefPtr<Gom::Repository> repo)
        {
            auto http = client();
            auto geocoder = mnn::NominatimGeocoder::create(http, 10ms);
            return mnn::PositionLookup::create(std::move(repo), std::move(http), [this] { return now; },
                                               std::move(geocoder));
        }
    };

    RefPtr<Dex::Future>
    find(Gom::Repository *repo, GObject::Type type, const char *callsign)
    {
        const char *props[] = { "callsign" };
        GObject::Value value { GObject::Type::of<const char *>() };
        value.set<const char *>(callsign);
        return repo->find_one_with_properties(type, props, { &value, 1 });
    }

    peeler::future<peeler::nullable<mnn::CallsignPosition>>
    find_position(Gom::Repository *repo, const char *callsign)
    {
        return peeler::into_future<peeler::nullable<mnn::CallsignPosition>>(
            find(repo, GObject::Type::of<mnn::CallsignPosition>(), callsign));
    }

    peeler::future<peeler::nullable<mnn::CallsignMiss>>
    find_miss(Gom::Repository *repo, const char *callsign)
    {
        return peeler::into_future<peeler::nullable<mnn::CallsignMiss>>(
            find(repo, GObject::Type::of<mnn::CallsignMiss>(), callsign));
    }

    peeler::future<bool>
    position_exists(RefPtr<Gom::Repository> repo, const char *callsign)
    {
        auto row = co_await find_position(repo, callsign);
        boost::ut::expect(row.has_value()) << "position read failed for" << callsign;
        co_return row && *row != nullptr;
    }

    peeler::future<bool>
    miss_exists(RefPtr<Gom::Repository> repo, const char *callsign)
    {
        auto miss = co_await find_miss(repo, callsign);
        boost::ut::expect(miss.has_value()) << "miss read failed for" << callsign;
        co_return miss && *miss != nullptr;
    }

    peeler::future<bool>
    insert_position(Gom::Repository *repo, const char *callsign, double lat, double lon, sys_seconds updated)
    {
        auto row = GObject::Object::create<mnn::CallsignPosition>();
        row->set_callsign(callsign);
        row->set_latitude(lat);
        row->set_longitude(lon);
        row->set_last_updated(updated);
        return peeler::into_future<bool>(repo->insert_entity(row));
    }

    double latitude(const mnn::Position &p)  { return p.coordinate->cast<Shumate::Location>()->get_latitude(); }
    double longitude(const mnn::Position &p) { return p.coordinate->cast<Shumate::Location>()->get_longitude(); }

} // namespace

int main()
{
    using namespace boost::ut;

    Dex::init();
    peeler::install_test_log_handler();
    Type::of<mnn::CallsignPosition>().ensure();
    Type::of<mnn::CallsignMiss>().ensure();
    Type::of<mnn::PositionLookup>().ensure();

    "miss fetches, stores, then hits the cache"_test = [] {
        FakeCallook callook;
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("KI6KVZ");
            static_assert(std::is_same_v<decltype(pos),
                                         std::expected<mnn::Position, std::error_code>>);
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 37.403684));
            expect(eq(longitude(*pos), -122.06438));
            expect(pos->precision == mnn::Precision::precise);
            expect(eq(callook.calls, 1));
            expect(eq(callook.last_uri, std::string { "https://callook.info/KI6KVZ/json" }));

            auto row = co_await find_position(repo, "KI6KVZ");
            expect(fatal(row.has_value() && *row != nullptr));
            expect(eq((*row)->get_callsign(), std::string { "KI6KVZ" }));
            expect(eq((*row)->get_latitude(), 37.403684));
            expect((*row)->get_last_updated() == t0);

            callook.now = t0 + std::chrono::days { 89 };
            auto again = co_await lookup->lookup("KI6KVZ");
            expect(fatal(again.has_value()));
            expect(eq(latitude(*again), 37.403684));
            expect(eq(callook.calls, 1));
            co_return true;
        });
    };

    "stale row is refetched and updated"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, moved_body);
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            expect(fatal((co_await insert_position(repo, "KI6KVZ", 37.4, -122.0, t0 - std::chrono::days { 91 })).has_value()));
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("KI6KVZ");
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 41.714));
            expect(eq(callook.calls, 1));

            auto row = co_await find_position(repo, "KI6KVZ");
            expect(fatal(row.has_value() && *row != nullptr));
            expect(eq((*row)->get_latitude(), 41.714));
            expect(eq((*row)->get_longitude(), -72.727));
            expect((*row)->get_last_updated() == t0);
            co_return true;
        });
    };

    "failed refetch returns the stale row"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::SERVICE_UNAVAILABLE, "");
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            expect(fatal((co_await insert_position(repo, "KI6KVZ", 37.4, -122.0, t0 - std::chrono::days { 91 })).has_value()));
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("KI6KVZ");
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 37.4));
            expect(eq(callook.calls, 1));
            co_return true;
        });
    };

    "VALID with only an address is geocoded and cached"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, address_only_body);
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("W1AW");
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 41.7147));
            expect(eq(longitude(*pos), -72.7285));
            expect(pos->precision == mnn::Precision::precise);
            expect(eq(callook.nominatim_calls, 1));
            expect((co_await position_exists(repo, "W1AW")) == true);

            auto again = co_await lookup->lookup("W1AW");
            expect(fatal(again.has_value()));
            expect(eq(latitude(*again), 41.7147));
            expect(eq(callook.calls, 1));
            expect(eq(callook.nominatim_calls, 1));
            co_return true;
        });
    };

    "ZIP centroid is approximate, stored, and served from cache"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, address_only_body);
        callook.nominatim_street = "[]";
        callook.nominatim_zip = R"([{"lat":"41.6863","lon":"-72.7287"}])";
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("W1AW");
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 41.6863));
            expect(pos->precision == mnn::Precision::approximate);
            expect(eq(callook.nominatim_calls, 2));

            auto row = co_await find_position(repo, "W1AW");
            expect(fatal(row.has_value() && *row != nullptr));
            expect((*row)->get_precision() == mnn::Precision::approximate);

            auto cached = co_await lookup->lookup("W1AW");
            expect(fatal(cached.has_value()));
            expect(cached->precision == mnn::Precision::approximate);
            expect(eq(callook.nominatim_calls, 2));

            /* A failed refresh keeps the stale row's precision... */
            callook.now = t0 + std::chrono::days { 91 };
            callook.reply(Soup::Status::SERVICE_UNAVAILABLE, "");
            auto stale = co_await lookup->lookup("W1AW");
            expect(fatal(stale.has_value()));
            expect(stale->precision == mnn::Precision::approximate);

            /* ...and a successful one with real coordinates replaces it. */
            callook.reply(Soup::Status::OK, valid_body);
            auto refreshed = co_await lookup->lookup("W1AW");
            expect(fatal(refreshed.has_value()));
            expect(refreshed->precision == mnn::Precision::precise);
            auto updated = co_await find_position(repo, "W1AW");
            expect(fatal(updated.has_value() && *updated != nullptr));
            expect((*updated)->get_precision() == mnn::Precision::precise);
            expect(eq((*updated)->get_latitude(), 37.403684));
            co_return true;
        });
    };

    "address that geocodes to nothing is remembered as a miss"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, address_only_body);
        callook.nominatim_street = "[]";
        callook.nominatim_zip = "[]";
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("W1AW");
            expect(!pos && pos.error() == mnn::error::no_coordinates);
            expect(eq(callook.nominatim_calls, 2));
            expect((co_await position_exists(repo, "W1AW")) == false);
            expect((co_await miss_exists(repo, "W1AW")) == true);

            auto again = co_await lookup->lookup("W1AW");
            expect(!again && again.error() == mnn::error::no_coordinates);
            expect(eq(callook.calls, 1));
            expect(eq(callook.nominatim_calls, 2));
            co_return true;
        });
    };

    "geocoder throttling is not remembered"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, address_only_body);
        callook.nominatim_status = static_cast<Soup::Status>(429); /* not in libsoup's enum */
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("W1AW");
            expect(!pos && pos.error() == mnn::error::lookup_unavailable);
            expect((co_await miss_exists(repo, "W1AW")) == false);
            co_return true;
        });
    };

    "concurrent lookups of one callsign share a request"_test = [] {
        FakeCallook callook;
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto first  = lookup->lookup("KI6KVZ");
            auto second = lookup->lookup(" ki6kvz ");
            auto a = co_await first;
            auto b = co_await second;
            expect(fatal(a.has_value() && b.has_value()));
            expect(eq(latitude(*a), 37.403684));
            expect(eq(latitude(*b), 37.403684));
            expect(eq(callook.calls, 1));

            /* The shared entry is gone once settled: a stale refresh fetches. */
            callook.now = t0 + std::chrono::days { 91 };
            auto later = co_await lookup->lookup("KI6KVZ");
            expect(fatal(later.has_value()));
            expect(eq(callook.calls, 2));
            co_return true;
        });
    };

    "error mapping"_test = [] {
        FakeCallook callook;
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            callook.reply(Soup::Status::OK, invalid_body);
            auto invalid = co_await lookup->lookup("N0CALL");
            expect(!invalid && invalid.error() == mnn::error::callsign_not_found);

            callook.reply(Soup::Status::SERVICE_UNAVAILABLE, "");
            auto unavailable = co_await lookup->lookup("K1ABC");
            expect(!unavailable && unavailable.error() == mnn::error::lookup_unavailable);

            callook.reply(Soup::Status::OK, updating_body);
            auto updating = co_await lookup->lookup("K1ABD");
            expect(!updating && updating.error() == mnn::error::lookup_unavailable);

            callook.reply(Soup::Status::OK, "<html>not json</html>");
            auto garbage = co_await lookup->lookup("K1ABE");
            expect(!garbage && garbage.error() == mnn::error::bad_response);

            callook.reply(Soup::Status::OK, R"({"status": "VALID", "location": {"latitude": 37.4}})");
            auto wrong_types = co_await lookup->lookup("K1ABF");
            expect(!wrong_types && wrong_types.error() == mnn::error::no_coordinates);

            callook.reply(Soup::Status::OK, R"(["VALID"])");
            auto not_object = co_await lookup->lookup("K1ABG");
            expect(!not_object && not_object.error() == mnn::error::bad_response);

            auto calls_before = callook.calls;
            for (auto bogus : { "", "   ", "!!", "KI", "KI6KVZ/P", "ABCDEF", "123456", "KI6KVZKI6KVZ" }) {
                auto r = co_await lookup->lookup(bogus);
                expect(!r && r.error() == mnn::error::invalid_callsign) << "callsign: '" << bogus << "'";
            }
            expect(eq(callook.calls, calls_before));
            co_return true;
        });
    };

    "negative results are remembered for a week"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, invalid_body);
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto first = co_await lookup->lookup("N0CALL");
            expect(!first && first.error() == mnn::error::callsign_not_found);
            auto miss = co_await find_miss(repo, "N0CALL");
            expect(fatal(miss.has_value() && *miss != nullptr));
            expect(eq((*miss)->get_callsign(), std::string { "N0CALL" }));
            expect(eq((*miss)->get_reason(), std::to_underlying(mnn::error::callsign_not_found)));

            callook.now = t0 + std::chrono::days { 6 };
            auto cached = co_await lookup->lookup("n0call");
            expect(!cached && cached.error() == mnn::error::callsign_not_found);
            expect(eq(callook.calls, 1));

            /* Expired: asks again. This time the call is licensed. */
            callook.now = t0 + std::chrono::days { 8 };
            callook.reply(Soup::Status::OK, valid_body);
            auto licensed = co_await lookup->lookup("N0CALL");
            expect(fatal(licensed.has_value()));
            expect(eq(callook.calls, 2));
            expect((co_await miss_exists(repo, "N0CALL")) == false);
            expect((co_await position_exists(repo, "N0CALL")) == true);
            co_return true;
        });
    };

    "no location and no address is remembered; transient failures are not"_test = [] {
        FakeCallook callook;
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            callook.reply(Soup::Status::OK, nothing_body);
            auto nothing = co_await lookup->lookup("K1XYZ");
            expect(!nothing && nothing.error() == mnn::error::no_coordinates);
            auto again = co_await lookup->lookup("K1XYZ");
            expect(!again && again.error() == mnn::error::no_coordinates);
            expect(eq(callook.calls, 1));
            auto miss = co_await find_miss(repo, "K1XYZ");
            expect(fatal(miss.has_value() && *miss != nullptr));
            expect(eq((*miss)->get_reason(), std::to_underlying(mnn::error::no_coordinates)));

            callook.reply(Soup::Status::SERVICE_UNAVAILABLE, "");
            auto down = co_await lookup->lookup("K2XYZ");
            expect(!down && down.error() == mnn::error::lookup_unavailable);
            expect((co_await miss_exists(repo, "K2XYZ")) == false);
            co_return true;
        });
    };

    "stale row plus INVALID: stale position, and the miss stops refetching"_test = [] {
        FakeCallook callook;
        callook.reply(Soup::Status::OK, invalid_body);
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            expect(fatal((co_await insert_position(repo, "KI6KVZ", 37.4, -122.0, t0 - std::chrono::days { 91 })).has_value()));
            auto lookup = callook.lookup(repo);

            auto pos = co_await lookup->lookup("KI6KVZ");
            expect(fatal(pos.has_value()));
            expect(eq(latitude(*pos), 37.4));
            expect((co_await miss_exists(repo, "KI6KVZ")) == true);

            auto again = co_await lookup->lookup("KI6KVZ");
            expect(fatal(again.has_value()));
            expect(eq(latitude(*again), 37.4));
            expect(eq(callook.calls, 1));
            co_return true;
        });
    };

    "callsigns are normalized to one row"_test = [] {
        FakeCallook callook;
        peeler::run_coro_with_repo([&](GLib::MainLoop *, RefPtr<Gom::Repository> repo) -> peeler::future<bool> {
            auto lookup = callook.lookup(repo);

            auto lower = co_await lookup->lookup("ki6kvz");
            expect(fatal(lower.has_value()));
            expect(eq(callook.last_uri, std::string { "https://callook.info/KI6KVZ/json" }));

            auto padded = co_await lookup->lookup(" KI6KVZ \t");
            expect(fatal(padded.has_value()));
            expect(eq(latitude(*padded), 37.403684));
            expect(eq(callook.calls, 1));
            expect((co_await position_exists(repo, "KI6KVZ")) == true);
            co_return true;
        });
    };
}
