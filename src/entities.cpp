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

#include "entities.hpp"
#include <peel/GObject/Type.h>

using namespace peel;

namespace mnn
{
    /* gom's default backfill_identity falls back to sqlite's rowid when the
     * insert result has no column named after the single identity field, and
     * writes that INT64 into it -- turning our TEXT callsign key into e.g.
     * "1" in memory (the row itself is fine). The key is caller-set, so there
     * is nothing to backfill. */
    bool
    CallsignPosition::vfunc_backfill_identity(StrvRef, Gom::Record *, UniquePtr<GLib::Error> *)
    {
        return true;
    }

    void
    CallsignPosition::Class::init()
    {
        override_vfunc_backfill_identity<CallsignPosition>();
        set_relation("callsign_positions");
        set_identity_field("callsign");
        property_set_field_name("last-updated", "last_updated");
        set_version_added(1);
    }

    /* Same rowid-backfill workaround as CallsignPosition. */
    bool
    CallsignMiss::vfunc_backfill_identity(StrvRef, Gom::Record *, UniquePtr<GLib::Error> *)
    {
        return true;
    }

    void
    CallsignMiss::Class::init()
    {
        override_vfunc_backfill_identity<CallsignMiss>();
        set_relation("callsign_misses");
        set_identity_field("callsign");
        property_set_field_name("last-checked", "last_checked");
        set_version_added(1);
    }

    void
    register_entities(const RefPtr<Gom::RegistryBuilder> &builder)
    {
        builder->add_entity_type(GObject::Type::of<CallsignPosition>());
        builder->add_entity_type(GObject::Type::of<CallsignMiss>());
    }

} // namespace mnn

PEEL_CLASS_IMPL(mnn::CallsignPosition, "MnnCallsignPosition", peel::Gom::Entity)
PEEL_CLASS_IMPL(mnn::CallsignMiss, "MnnCallsignMiss", peel::Gom::Entity)
