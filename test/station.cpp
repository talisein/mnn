#include <boost/ut.hpp>
#include "station.hpp"

int main() {
    using namespace boost::ut;
    using namespace std::literals;
    using namespace peel;

    Type::of<mnn::Station>().ensure();

    "basic"_test = [] {
        RefPtr<mnn::Station> p = Object::create<mnn::Station>(mnn::Station::prop_name(), "Test Name",
                                                              mnn::Station::prop_callsign(), "KI6KVZ",
                                                              mnn::Station::prop_is_assistant_emergency_coordinator(), true,
                                                              mnn::Station::prop_status(), mnn::StationStatus::HEARD_DIRECT
            );
        expect(nullptr != p);
        expect(eq("Test Name"sv, p->get_name()));
        expect(eq("Test Name"sv, std::string_view(p->get_property(mnn::Station::prop_name()))));
        expect(eq("KI6KVZ"sv, p->get_callsign()));
        expect(eq("KI6KVZ"sv, std::string_view(p->get_property(mnn::Station::prop_callsign()))));
        expect(eq("KI6"sv, std::string_view(p->get_property(mnn::Station::prop_prefix()))));
        expect(eq("KVZ"sv, std::string_view(p->get_property(mnn::Station::prop_suffix()))));
        expect(eq(true, p->get_property(mnn::Station::prop_is_assistant_emergency_coordinator())));
        expect(mnn::StationStatus::HEARD_DIRECT == p->get_property(mnn::Station::prop_status()));

        auto check_notify = [&p](auto prop, auto value) {
            bool notified = false;
            SignalConnection connection = p->connect_notify(prop, [&notified, &prop, &value](Object *c, GObject::ParamSpec *pspec) {
                notified = true;
                if constexpr (std::is_same_v<const char*, std::decay_t<decltype(value)>>) {
                    expect(eq(std::string_view(value), std::string_view(c->cast<mnn::Station>()->get_property(prop))));
                } else {
                    expect(value == c->cast<mnn::Station>()->get_property(prop));
                }

            });
            p->set_property(prop, value);
            connection.disconnect();
            expect(eq(true, notified));
        };

        check_notify(mnn::Station::prop_name(), "ARRL");
        check_notify(mnn::Station::prop_callsign(), "W1AW");
        expect(eq("W1"sv, std::string_view(p->get_property(mnn::Station::prop_prefix()))));
        expect(eq("AW"sv, std::string_view(p->get_property(mnn::Station::prop_suffix()))));

        auto check_notify_sideeffect = [&p](auto main_prop, auto value, auto side_prop, auto side_value)
        {
            bool main_notified = false, side_notified = false;
            SignalConnection main_connection = p->connect_notify(main_prop, [&main_notified](Object *c, GObject::ParamSpec *pspec) {
                main_notified = true;
            });
            SignalConnection side_connection = p->connect_notify(side_prop, [&side_notified, &side_prop, &side_value](Object *c, GObject::ParamSpec *pspec) {
                side_notified = true;
                if constexpr (std::is_same_v<const char*, std::decay_t<decltype(side_value)>>) {
                    expect(eq(std::string_view(side_value), std::string_view(c->cast<mnn::Station>()->get_property(side_prop))));
                } else {
                    expect(side_value == c->cast<mnn::Station>()->get_property(side_prop));
                }
            });
            p->set_property(main_prop, value);
            main_connection.disconnect();
            side_connection.disconnect();
            expect(main_notified);
            expect(side_notified);
        };
        check_notify_sideeffect(mnn::Station::prop_callsign(), "GB3RS", mnn::Station::prop_prefix(), "GB3");
        check_notify_sideeffect(mnn::Station::prop_callsign(), "W6ASH", mnn::Station::prop_suffix(), "ASH");
        check_notify(mnn::Station::prop_is_assistant_emergency_coordinator(), false);
        check_notify(mnn::Station::prop_status(), mnn::StationStatus::PENDING);
        check_notify_sideeffect(mnn::Station::prop_is_acknowledged(), true, mnn::Station::prop_status(), mnn::StationStatus::HEARD_RELAY);
    };

    "location"_test = [] {
        RefPtr<mnn::Station> p = Object::create<mnn::Station>(mnn::Station::prop_name(), "Test Name",
                                                              mnn::Station::prop_callsign(), "KI6KVZ");
        expect(nullptr != p);
        expect(eq(false, p->get_property(mnn::Station::prop_has_location())));
        expect(eq(SHUMATE_MIN_LATITUDE, p->get_property(mnn::Station::prop_latitude())));
        expect(eq(SHUMATE_MIN_LONGITUDE, p->get_property(mnn::Station::prop_longitude())));
        bool notified = false, notified_lat = false, notified_long = false;
        SignalConnection connection = p->connect_notify(mnn::Station::prop_has_location(), [&notified](Object *c, GObject::ParamSpec *pspec) {
            notified = true;
        });
        SignalConnection connection_lat = p->connect_notify(mnn::Station::prop_latitude(), [&notified_lat](Object *c, GObject::ParamSpec *pspec) {
            notified_lat = true;
        });
        SignalConnection connection_long = p->connect_notify(mnn::Station::prop_longitude(), [&notified_long](Object *c, GObject::ParamSpec *pspec) {
            notified_long = true;
        });
        p->set_location(122.2, 80.8);
        expect(eq(true, notified));
        expect(eq(true, notified_lat));
        expect(eq(true, notified_long));
        notified = notified_lat = notified_long = false;
        expect(eq(true, p->has_location()));
        expect(eq(122.2, p->get_latitude()));
        expect(eq(80.8, p->get_longitude()));
        p->set_location(SHUMATE_MIN_LATITUDE, SHUMATE_MIN_LONGITUDE);
        expect(eq(true, notified));
        expect(eq(true, notified_lat));
        expect(eq(true, notified_long));
        expect(eq(false, p->has_location()));
        expect(eq(SHUMATE_MIN_LATITUDE, p->get_latitude()));
        expect(eq(SHUMATE_MIN_LONGITUDE, p->get_longitude()));
    };
}
