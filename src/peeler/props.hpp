// SPDX-FileCopyrightText: 2026 Andrew Potter
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Type-safe GObject property support for C++ standard-library types.
//
// peel keys every property hop off three specializable trait points:
//   * peel::GObject::Type::of<T>()             — the property's GType
//   * peel::GObject::Value::Traits<T>          — GValue <-> C++ marshalling
//   * peel::internals::PspecTraits<T>          — GParamSpec construction
// Specializing those three is all it takes to make a C++ type a first-class
// property: it then works in define_properties(), Object::create<T>(),
// g_object_get/set, GtkBuilder <lookup>, and property bindings — no peel-core
// change, no codegen, no reflection. This header adds the std:: types we use.
//
// Two coupled rules govern UnownedType / OwnedType here:
//
//   * No OwnedType on the value-typed traits. peel's Value::set<T> declares both
//     set(UnownedType) and set(OwnedType&&); defining OwnedType makes both exist,
//     and for a std::string property a string-literal argument converts to both
//     std::string (the param) and the OwnedType => ambiguous in Object::create.
//     Omitting OwnedType (like peel's enum trait) SFINAEs the OwnedType&& overload
//     away. It is also safe for the generic get_property(prop) route, which builds
//     a temporary GValue: with no OwnedType, GetPropertyHelper returns
//     Traits::get(), and for these traits get() returns an OWNED value (a fresh
//     std::string / vector / scalar copied out of the GValue), not a borrow.
//
//   * No std::string_view property type. Its get() would return a view INTO the
//     GValue, so get_property(prop) would hand back a dangling view of the
//     destroyed temporary. Use std::string for string properties; a plain C++
//     getter may still return std::string_view by viewing a member directly
//     (that borrows the object, not a temporary).
//
// OwnedType lives only on PEELER_BOXED_TYPE, where UnownedType is a pointer
// (const T*) so it can't tie with the OwnedType value, and get_property dups.
//
// Faithfulness: each wrapper round-trips the full domain of its C++ type, and
// the static_asserts below prove the GValue integer storage can hold every
// representable value without truncation. The wrapper imposes no range limits
// of its own (GParamSpec bounds are the full storage range).

#include <peel/GObject/Type.h>
#include <peel/GObject/Value.h>
#include <peel/property.h>
#include <peel/class.h>
#include <peel/String.h>
#include <glib-object.h>

#include <algorithm>
#include <chrono>
#include <compare>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

/* ------------------------------------------------------------------ */
/* std::string  <->  G_TYPE_STRING                                    */
/*                                                                    */
/* get() returns an owned copy (so the generic get_property route is   */
/* safe); set() copies into the GValue. The C++ accessor surface can   */
/* still be std::string_view by viewing a member — see the header.     */

template<>
inline peel::GObject::Type
peel::GObject::Type::of<std::string> ()
{
    return G_TYPE_STRING;
}

template<>
struct peel::GObject::Value::Traits<std::string>
{
    typedef std::string UnownedType;

    static std::string
    get (const ::GValue *value) noexcept
    {
        const char *s = g_value_get_string (value);
        return s ? std::string { s } : std::string {};
    }

    static void
    set (::GValue *value, const std::string &s) noexcept
    {
        g_value_set_string (value, s.c_str ());
    }

    static void
    set_marshal_return (::GValue *value, const std::string &s) noexcept
    {
        set (value, s);
    }
};

template<>
struct peel::internals::PspecTraits<std::string>
{
    const char *default_value;

    constexpr PspecTraits (const char *default_value = nullptr)
        : default_value (default_value)
    { }

    ::GParamSpec *
    create_pspec (peel::internals::PspecBasics basics)
    {
        return g_param_spec_string (basics.name, basics.nick, basics.blurb,
                                    default_value, basics.flags);
    }
};

/* ------------------------------------------------------------------ */
/* std::vector<std::string>  <->  G_TYPE_STRV                         */
/*                                                                    */
/* Each element is g_strdup'd into a fresh NUL-terminated GStrv that  */
/* the GValue owns (g_value_take_boxed); nothing is shared with the   */
/* source vector's std::strings.                                      */

template<>
inline peel::GObject::Type
peel::GObject::Type::of<std::vector<std::string>> ()
{
    return G_TYPE_STRV;
}

template<>
struct peel::GObject::Value::Traits<std::vector<std::string>>
{
    typedef std::vector<std::string> UnownedType;

    static std::vector<std::string>
    get (const ::GValue *value) noexcept
    {
        auto strv = static_cast<const char *const *> (g_value_get_boxed (value));
        if (!strv)
            return {};
        /* g_strv_length stops at the first NULL, so counted() spans only the
         * non-NULL prefix; the guard below is belt-and-suspenders. */
        return std::views::counted (strv, g_strv_length (const_cast<char **> (strv)))
             | std::views::transform ([] (const char *s) {
                   return s ? std::string { s } : std::string {};
               })
             | std::ranges::to<std::vector<std::string>> ();
    }

    static void
    set (::GValue *value, const std::vector<std::string> &vec) noexcept
    {
        char **strv = g_new0 (char *, vec.size () + 1);
        std::ranges::transform (vec, strv,
                                [] (const std::string &s) { return g_strdup (s.c_str ()); });
        g_value_take_boxed (value, strv);
    }

    static void
    set_marshal_return (::GValue *value, const std::vector<std::string> &vec) noexcept
    {
        set (value, vec);
    }
};

template<>
struct peel::internals::PspecTraits<std::vector<std::string>>
{
    constexpr PspecTraits ()
    { }

    ::GParamSpec *
    create_pspec (peel::internals::PspecBasics basics)
    {
        return g_param_spec_boxed (basics.name, basics.nick, basics.blurb,
                                   G_TYPE_STRV, basics.flags);
    }
};

/* ------------------------------------------------------------------ */
/* std::chrono::sys_seconds  <->  G_TYPE_INT64  (unix seconds)        */
/* Layout-identical to the raw std::int64_t timestamps used today.    */

static_assert (std::in_range<gint64> (std::numeric_limits<std::chrono::sys_seconds::rep>::min ())
            && std::in_range<gint64> (std::numeric_limits<std::chrono::sys_seconds::rep>::max ()),
               "sys_seconds tick count must fit losslessly in gint64");

template<>
inline peel::GObject::Type
peel::GObject::Type::of<std::chrono::sys_seconds> ()
{
    return G_TYPE_INT64;
}

template<>
struct peel::GObject::Value::Traits<std::chrono::sys_seconds>
{
    typedef std::chrono::sys_seconds UnownedType;

    static std::chrono::sys_seconds
    get (const ::GValue *value) noexcept
    {
        return std::chrono::sys_seconds { std::chrono::seconds { g_value_get_int64 (value) } };
    }

    static void
    set (::GValue *value, std::chrono::sys_seconds t) noexcept
    {
        g_value_set_int64 (value, t.time_since_epoch ().count ());
    }

    static void
    set_marshal_return (::GValue *value, std::chrono::sys_seconds t) noexcept
    {
        set (value, t);
    }
};

template<>
struct peel::internals::PspecTraits<std::chrono::sys_seconds>
{
    std::chrono::sys_seconds default_value;

    constexpr PspecTraits (std::chrono::sys_seconds default_value = {})
        : default_value (default_value)
    { }

    ::GParamSpec *
    create_pspec (peel::internals::PspecBasics basics)
    {
        return g_param_spec_int64 (basics.name, basics.nick, basics.blurb,
                                   G_MININT64, G_MAXINT64,
                                   default_value.time_since_epoch ().count (),
                                   basics.flags);
    }
};

/* ------------------------------------------------------------------ */
/* std::chrono::milliseconds  <->  G_TYPE_INT64  (millisecond count)  */

static_assert (std::in_range<gint64> (std::numeric_limits<std::chrono::milliseconds::rep>::min ())
            && std::in_range<gint64> (std::numeric_limits<std::chrono::milliseconds::rep>::max ()),
               "milliseconds tick count must fit losslessly in gint64");

template<>
inline peel::GObject::Type
peel::GObject::Type::of<std::chrono::milliseconds> ()
{
    return G_TYPE_INT64;
}

template<>
struct peel::GObject::Value::Traits<std::chrono::milliseconds>
{
    typedef std::chrono::milliseconds UnownedType;

    static std::chrono::milliseconds
    get (const ::GValue *value) noexcept
    {
        return std::chrono::milliseconds { g_value_get_int64 (value) };
    }

    static void
    set (::GValue *value, std::chrono::milliseconds d) noexcept
    {
        g_value_set_int64 (value, d.count ());
    }

    static void
    set_marshal_return (::GValue *value, std::chrono::milliseconds d) noexcept
    {
        set (value, d);
    }
};

template<>
struct peel::internals::PspecTraits<std::chrono::milliseconds>
{
    std::chrono::milliseconds default_value;

    constexpr PspecTraits (std::chrono::milliseconds d = {})
        : default_value (d)
    { }

    ::GParamSpec *
    create_pspec (peel::internals::PspecBasics basics)
    {
        return g_param_spec_int64 (basics.name, basics.nick, basics.blurb,
                                   G_MININT64, G_MAXINT64,
                                   default_value.count (),
                                   basics.flags);
    }
};

/* ------------------------------------------------------------------ */
/* std::chrono::year_month_day  <->  G_TYPE_INT64                     */
/*                                                                    */
/* Stored as the sys_days day-count from the system_clock epoch, so   */
/* every ok() date round-trips exactly regardless of year (the chrono */
/* type itself bounds year to [-32767,32767]); the wrapper adds no    */
/* limits. The GParamSpec spans the full int64 range.                 */

static_assert (std::in_range<gint64> (std::numeric_limits<std::chrono::sys_days::rep>::min ())
            && std::in_range<gint64> (std::numeric_limits<std::chrono::sys_days::rep>::max ()),
               "year_month_day day-count must fit losslessly in gint64");

namespace peeler
{

inline gint64
ymd_to_days (std::chrono::year_month_day d) noexcept
{
    return static_cast<std::chrono::sys_days> (d).time_since_epoch ().count ();
}

inline std::chrono::year_month_day
days_to_ymd (gint64 days) noexcept
{
    return std::chrono::year_month_day {
        std::chrono::sys_days { std::chrono::days { days } }
    };
}

} /* namespace peeler */

template<>
inline peel::GObject::Type
peel::GObject::Type::of<std::chrono::year_month_day> ()
{
    return G_TYPE_INT64;
}

template<>
struct peel::GObject::Value::Traits<std::chrono::year_month_day>
{
    typedef std::chrono::year_month_day UnownedType;

    static std::chrono::year_month_day
    get (const ::GValue *value) noexcept
    {
        return peeler::days_to_ymd (g_value_get_int64 (value));
    }

    static void
    set (::GValue *value, std::chrono::year_month_day d) noexcept
    {
        g_value_set_int64 (value, peeler::ymd_to_days (d));
    }

    static void
    set_marshal_return (::GValue *value, std::chrono::year_month_day d) noexcept
    {
        set (value, d);
    }
};

template<>
struct peel::internals::PspecTraits<std::chrono::year_month_day>
{
    std::chrono::year_month_day default_value {
        std::chrono::year { 1970 } / std::chrono::January / 1
    };

    constexpr PspecTraits ()
    { }

    constexpr PspecTraits (std::chrono::year_month_day default_value)
        : default_value (default_value)
    { }

    ::GParamSpec *
    create_pspec (peel::internals::PspecBasics basics)
    {
        return g_param_spec_int64 (basics.name, basics.nick, basics.blurb,
                                   G_MININT64, G_MAXINT64,
                                   peeler::ymd_to_days (default_value),
                                   basics.flags);
    }
};

/* ------------------------------------------------------------------ */
/* PEELER_BOXED_TYPE — register any copyable C++ type as a G_TYPE_BOXED  */
/* property type, eliminating "void* / G_TYPE_POINTER + remember the   */
/* type" casts. get() borrows the GValue's owned copy; dup() returns   */
/* independent value. Note: GtkBuilder cannot introspect a boxed C++   */
/* type's fields — consume it whole (a <closure> or in code).          */

#define PEELER_BOXED_TYPE(T, type_name)                                          \
    template<>                                                                 \
    inline peel::GObject::Type                                                 \
    peel::GObject::Type::of<T> ()                                              \
    {                                                                          \
        static ::GType _peel_tp;                                               \
        if (_peel_once_init_enter (&_peel_tp))                                 \
        {                                                                      \
            ::GType _peel_actual_tp = g_boxed_type_register_static (           \
                g_intern_static_string (type_name),                           \
                [] (gpointer p) -> gpointer { return new T (*static_cast<T *> (p)); }, \
                [] (gpointer p) { delete static_cast<T *> (p); });             \
            _peel_once_init_leave (&_peel_tp, _peel_actual_tp);                \
        }                                                                      \
        return _peel_tp;                                                       \
    }                                                                          \
    template<>                                                                 \
    struct peel::GObject::Value::Traits<T>                                     \
    {                                                                          \
        typedef const T *UnownedType;                                          \
        typedef T OwnedType;                                                   \
        static const T *get (const ::GValue *value) noexcept                   \
        {                                                                      \
            return static_cast<const T *> (g_value_get_boxed (value));         \
        }                                                                      \
        static T dup (const ::GValue *value) noexcept                          \
        {                                                                      \
            auto *p = static_cast<const T *> (g_value_get_boxed (value));      \
            return p ? *p : T {};                                             \
        }                                                                      \
        static void set (::GValue *value, const T *p) noexcept                 \
        {                                                                      \
            g_value_set_boxed (value, p);                                      \
        }                                                                      \
        static void take (::GValue *value, T &&val) noexcept                   \
        {                                                                      \
            g_value_take_boxed (value, new T (std::move (val)));               \
        }                                                                      \
        static const T *cast_for_create (const T *p) noexcept { return p; }    \
    };                                                                         \
    template<>                                                                 \
    struct peel::internals::PspecTraits<T>                                     \
    {                                                                          \
        constexpr PspecTraits () { }                                           \
        ::GParamSpec *create_pspec (peel::internals::PspecBasics basics)       \
        {                                                                      \
            return g_param_spec_boxed (basics.name, basics.nick, basics.blurb, \
                                       peel::GObject::Type::of<T> (), basics.flags); \
        }                                                                      \
    };                                                                         \
    /* end of PEELER_BOXED_TYPE */
