#include <string>
#include <string_view>
#include <optional>
#include <peel/GObject/Object.h>
#include <peel/class.h>
#include <peel/enum.h>
#include <peel/String.h>
#include <peel/GLib/GLib.h>
#include <peel/Shumate/Location.h>

namespace mnn
{
    enum class StationStatus
    {
        PENDING,
        HEARD_DIRECT,
        HEARD_RELAY
    };
}
PEEL_ENUM(mnn::StationStatus)

namespace mnn
{
    class Station final : public peel::Shumate::Location
    {
        PEEL_SIMPLE_CLASS (Station, Object)
        friend class peel::Shumate::Location;

        static void init_type(peel::Type tp);
        static void init_interface(peel::Shumate::Location::Iface*);

        struct Location {
            double latitude;
            double longitude;
        };

        struct Members {
            std::string name;
            std::string callsign;
            std::string prefix;
            std::string suffix;
            bool is_assistant_emergency_coordinator;
            bool is_acknowledged;
            StationStatus status;
            std::optional<Location> location;
        } m;

    public:
        PEEL_PROPERTY(const char *, name, "name");
        PEEL_PROPERTY(const char *, callsign, "callsign");
        PEEL_PROPERTY(const char *, prefix, "prefix");
        PEEL_PROPERTY(const char *, suffix, "suffix");
        PEEL_PROPERTY(bool, is_acknowledged, "is-acknowledged");
        PEEL_PROPERTY(bool, is_assistant_emergency_coordinator, "is-assistant-emergency-coordinator");
        PEEL_PROPERTY(StationStatus, status, "status");
        PEEL_PROPERTY(bool, has_location, "has-location");

        double vfunc_get_latitude() const noexcept;
        double vfunc_get_longitude() const noexcept;
        void vfunc_set_location(double, double) noexcept;

        void set_name(std::string_view str);
        std::string get_name() const;
        void set_callsign(std::string_view str);
        std::string get_callsign() const;

        bool get_is_assistant_emergency_coordinator() const;
        void set_is_assistant_emergency_coordinator(bool);
        bool get_is_acknowledged() const;
        void set_is_acknowledged(bool);
        bool has_location() const;
        StationStatus get_status() const;
        void set_status(StationStatus status);

    private:
        void update_prefix_suffix();
        const char* get_name_cstr();
        const char* get_callsign_cstr();
        const char* get_prefix_cstr();
        const char* get_suffix_cstr();
        void set_name_cstr(const char* str);
        void set_callsign_cstr(const char* str);

        template<typename F>
        static void
        define_properties (F &f)
        {
            f.prop(prop_name(), nullptr)
                .get(&Station::get_name_cstr)
                .set(&Station::set_name_cstr);
            f.prop(prop_callsign(), nullptr)
                .get(&Station::get_callsign_cstr)
                .set(&Station::set_callsign_cstr);
            f.prop(prop_prefix(), nullptr)
                .get(&Station::get_prefix_cstr);
            f.prop(prop_suffix(), nullptr)
                .get(&Station::get_suffix_cstr);
            f.prop(prop_is_acknowledged(), false)
                .get(&Station::get_is_acknowledged)
                .set(&Station::set_is_acknowledged);
            f.prop(prop_is_assistant_emergency_coordinator(), false)
                .get(&Station::get_is_assistant_emergency_coordinator)
                .set(&Station::set_is_assistant_emergency_coordinator);
            f.prop(prop_has_location(), false)
                .get(&Station::has_location);
            f.prop(prop_status(), StationStatus::PENDING)
                .get(&Station::get_status)
                .set(&Station::set_status);
            f.override_prop(peel::Shumate::Location::prop_latitude())
                .get(&Station::vfunc_get_latitude);
            f.override_prop(peel::Shumate::Location::prop_longitude())
                .get(&Station::vfunc_get_longitude);
        }

    public:
        void init (Class*);

    };

} // namespace mnn
