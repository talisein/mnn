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

#include "peeler/props.hpp"
#include "position.hpp"
#include <chrono>
#include <string>
#include <string_view>
#include <peel/GLib/Error.h>
#include <peel/Gom/Entity.h>
#include <peel/Gom/Record.h>
#include <peel/Gom/RegistryBuilder.h>
#include <peel/RefPtr.h>
#include <peel/UniquePtr.h>
#include <peel/class.h>

namespace mnn
{
    /* A cached callook.info position for one callsign. */
    class CallsignPosition final : public peel::Gom::Entity
    {
        PEEL_SIMPLE_CLASS(CallsignPosition, peel::Gom::Entity);
        friend class peel::Gom::Entity::Class;

        struct Members {
            std::string              callsign;     /* primary key, upper-case */
            double                   latitude;
            double                   longitude;
            std::chrono::sys_seconds last_updated; /* when callook last gave us this */
            Precision                precision;    /* approximate: a ZIP centroid */
        } m;

        void init(Class *) { new (&m) Members; }

        bool vfunc_backfill_identity(peel::StrvRef identity_fields, peel::Gom::Record *record,
                                     peel::UniquePtr<peel::GLib::Error> *error);

        template<typename F>
        static void define_properties(F &f)
        {
            f.prop(prop_callsign(), nullptr)
                .get(&CallsignPosition::get_callsign).set(&CallsignPosition::set_callsign);
            f.prop(prop_latitude(), -90.0, 90.0, 0.0)
                .get(&CallsignPosition::get_latitude).set(&CallsignPosition::set_latitude);
            f.prop(prop_longitude(), -180.0, 180.0, 0.0)
                .get(&CallsignPosition::get_longitude).set(&CallsignPosition::set_longitude);
            f.prop(prop_last_updated())
                .get(&CallsignPosition::get_last_updated).set(&CallsignPosition::set_last_updated);
            f.prop(prop_precision(), Precision::precise)
                .get(&CallsignPosition::get_precision).set(&CallsignPosition::set_precision);
        }

    public:
        const std::string       &get_callsign() const     { return m.callsign; }
        double                   get_latitude() const     { return m.latitude; }
        double                   get_longitude() const    { return m.longitude; }
        std::chrono::sys_seconds get_last_updated() const { return m.last_updated; }
        Precision                get_precision() const    { return m.precision; }

        void set_callsign(std::string_view v)
        {
            if (v == m.callsign) return;
            m.callsign.assign(v);
            notify(prop_callsign());
        }
        void set_latitude(double v)
        {
            if (m.latitude == v) return;
            m.latitude = v;
            notify(prop_latitude());
        }
        void set_longitude(double v)
        {
            if (m.longitude == v) return;
            m.longitude = v;
            notify(prop_longitude());
        }
        void set_last_updated(std::chrono::sys_seconds v)
        {
            if (m.last_updated == v) return;
            m.last_updated = v;
            notify(prop_last_updated());
        }
        void set_precision(Precision v)
        {
            if (m.precision == v) return;
            m.precision = v;
            notify(prop_precision());
        }

        PEEL_PROPERTY(std::string,              callsign,     "callsign")
        PEEL_PROPERTY(double,                   latitude,     "latitude")
        PEEL_PROPERTY(double,                   longitude,    "longitude")
        PEEL_PROPERTY(std::chrono::sys_seconds, last_updated, "last-updated")
        PEEL_PROPERTY(Precision,                precision,    "precision")
    };

    /* A remembered "callook has nothing for this callsign", so the same call
     * isn't re-queried on every lookup. Kept apart from CallsignPosition so
     * positive rows are always real positions. */
    class CallsignMiss final : public peel::Gom::Entity
    {
        PEEL_SIMPLE_CLASS(CallsignMiss, peel::Gom::Entity);
        friend class peel::Gom::Entity::Class;

        struct Members {
            std::string              callsign;     /* primary key, upper-case */
            int                      reason;       /* an mnn::error value */
            std::chrono::sys_seconds last_checked;
        } m;

        void init(Class *) { new (&m) Members; }

        bool vfunc_backfill_identity(peel::StrvRef identity_fields, peel::Gom::Record *record,
                                     peel::UniquePtr<peel::GLib::Error> *error);

        template<typename F>
        static void define_properties(F &f)
        {
            f.prop(prop_callsign(), nullptr)
                .get(&CallsignMiss::get_callsign).set(&CallsignMiss::set_callsign);
            f.prop(prop_reason(), 0, G_MAXINT, 0)
                .get(&CallsignMiss::get_reason).set(&CallsignMiss::set_reason);
            f.prop(prop_last_checked())
                .get(&CallsignMiss::get_last_checked).set(&CallsignMiss::set_last_checked);
        }

    public:
        const std::string       &get_callsign() const     { return m.callsign; }
        int                      get_reason() const       { return m.reason; }
        std::chrono::sys_seconds get_last_checked() const { return m.last_checked; }

        void set_callsign(std::string_view v)
        {
            if (v == m.callsign) return;
            m.callsign.assign(v);
            notify(prop_callsign());
        }
        void set_reason(int v)
        {
            if (m.reason == v) return;
            m.reason = v;
            notify(prop_reason());
        }
        void set_last_checked(std::chrono::sys_seconds v)
        {
            if (m.last_checked == v) return;
            m.last_checked = v;
            notify(prop_last_checked());
        }

        PEEL_PROPERTY(std::string,              callsign,     "callsign")
        PEEL_PROPERTY(int,                      reason,       "reason")
        PEEL_PROPERTY(std::chrono::sys_seconds, last_checked, "last-checked")
    };

    void register_entities(const peel::RefPtr<peel::Gom::RegistryBuilder> &builder);

} // namespace mnn
