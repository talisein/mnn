// SPDX-FileCopyrightText: 2026 Andrew Potter
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <format>
#include <source_location>
#include <string_view>
#include <glib.h>

/* peeler_check.hpp — source_location-aware assertion primitives.
 *
 * peeler::loc_t is the type stored in pipeline adaptor structs and threaded
 * through to extract_value. Under G_DISABLE_CHECKS it becomes an empty struct
 * so [[no_unique_address]] members and lambda captures cost nothing.
 *
 * peeler::warn_if_fail(cond, loc [, msg/fmt...]) — g_warning at user call site
 * peeler::log_check_failure(expr, loc [, msg])   — g_critical, used by macros
 *
 * peeler_return_val_if_fail(expr, val, loc) — return val if expr is false
 * peeler_return_if_fail(expr, loc)          — return void if expr is false
 *
 * Macros take an explicit loc (already captured by the entry point's defaulted
 * std::source_location parameter) so return exits the caller, not a helper.
 * All are no-ops when G_DISABLE_CHECKS is defined. */

#ifdef G_DISABLE_CHECKS

namespace peeler {
    struct loc_t {
        constexpr loc_t() noexcept = default;
        constexpr loc_t(std::source_location) noexcept {}
    };
    inline constexpr void warn_if_fail(bool, loc_t = {}) noexcept {}
    inline constexpr void warn_if_fail(bool, loc_t, std::string_view) noexcept {}
    template<typename... Args>
    inline constexpr void warn_if_fail(bool, loc_t,
                                       std::format_string<Args...>, Args&&...) noexcept {}
    inline constexpr void log_check_failure(const char *, loc_t = {}) noexcept {}
} // namespace peeler

#  define peeler_return_val_if_fail(expr, val, loc) \
     G_STMT_START { (void)(expr); } G_STMT_END
#  define peeler_return_if_fail(expr, loc) \
     G_STMT_START { (void)(expr); } G_STMT_END

#else // !G_DISABLE_CHECKS

namespace peeler {

using loc_t = std::source_location;

/* Generic "runtime check failed" — no custom message. */
inline void
warn_if_fail(bool cond, loc_t loc = std::source_location::current()) noexcept
{
    if (!cond) [[unlikely]]
        g_warning("%s:%u: %s: runtime check failed",
                  loc.file_name(), loc.line(), loc.function_name());
}

/* With a pre-formatted message string. */
inline void
warn_if_fail(bool cond, loc_t loc, std::string_view msg) noexcept
{
    if (!cond) [[unlikely]]
        g_warning("%s:%u: %s: %.*s",
                  loc.file_name(), loc.line(), loc.function_name(),
                  static_cast<int>(msg.size()), msg.data());
}

/* With a std::format format string — avoids an intermediate std::string
 * allocation by formatting directly into a fixed stack buffer via format_to. */
template<typename... Args>
void
warn_if_fail(bool cond, loc_t loc,
             std::format_string<Args...> fmt, Args&&... args) noexcept
{
    if (!cond) [[unlikely]] {
        auto msg = std::format(fmt, std::forward<Args>(args)...);
        warn_if_fail(cond, loc, std::string_view{msg});
    }
}

inline void
log_check_failure(const char *expr,
                  loc_t loc = std::source_location::current()) noexcept
{
    g_critical("%s:%u: %s: check failed: (%s)",
               loc.file_name(), loc.line(), loc.function_name(), expr);
}

} // namespace peeler

#  define peeler_return_val_if_fail(expr, val, loc)            \
     G_STMT_START {                                             \
         if (!(expr)) [[unlikely]] {                           \
             ::peeler::log_check_failure(#expr, (loc));        \
             return (val);                                      \
         }                                                      \
     } G_STMT_END

#  define peeler_return_if_fail(expr, loc)                     \
     G_STMT_START {                                             \
         if (!(expr)) [[unlikely]] {                           \
             ::peeler::log_check_failure(#expr, (loc));        \
             return;                                            \
         }                                                      \
     } G_STMT_END

#endif // !G_DISABLE_CHECKS
