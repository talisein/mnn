// SPDX-FileCopyrightText: 2026 Andrew Potter
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
/* Bridges a libdex Dex::Future into the peel C++ coroutine world so it can be
 * `co_await`ed directly from a peeler::future / coro::SimpleTask, instead of
 * spinning up a Dex fiber and blocking on the fiber-only Dex::Future::await*
 * family.
 *
 * Two layers:
 *
 *  peeler::future<T> — typed newtype wrapping RefPtr<Dex::Future>. Functions
 *    that produce Dex futures return this so callers know the element type.
 *    `co_await` on it directly yields expected<dex_value_t<T>>.
 *    Pipe operators maintain the type: future<T> | let_value<U>(cb) → future<U>.
 *    Implicit conversion to RefPtr<Dex::Future> lets it be returned from
 *    let_value/finally callbacks.
 *
 *  RefPtr<Dex::Future> (raw/untyped) — gom and other C APIs hand these back.
 *    `co_await` on a raw future yields the settled RefPtr for manual inspection.
 *    into_future<T>(raw) enters the typed world as future<T>.
 *    | let_value<T>(cb) chains a typed step from an untyped base.
 *
 * Examples:
 *
 *   // Async pipeline (let_value) — OutT infers InT from future<InT> LHS:
 *   auto texture = co_await (
 *       glycin_load(loader, cancel)           // future<Gly::Image>
 *       | peeler::let_value<Gly::Frame>([cancel](RefPtr<Gly::Image> img) -> RefPtr<Dex::Future> {
 *             return glycin_next_frame(img, cancel);   // future<Gly::Frame>
 *         })                                  // → future<Gly::Frame>
 *       | peeler::let_value<Gdk::Texture>(...) // → future<Gdk::Texture>
 *   );
 *
 *   // Deducing let_value — cb returns peeler::future<T>:
 *   auto texture = co_await (
 *       load_bytes(file, cancel)              // future<GLib::Bytes>
 *       | peeler::let_value([cancel](RefPtr<GLib::Bytes> b) {
 *             return load_texture_from_bytes(b, cancel);  // future<Gdk::Texture>
 *         })                                  // → future<Gdk::Texture>, deduced
 *   );
 *
 *   // Synchronous then — cb returns a plain value (no new future):
 *   auto texture = co_await (
 *       load_frame(loader, cancel)            // future<Gly::Frame>
 *       | peeler::then([](RefPtr<Gly::Frame> frame) {
 *             return GlyGtk4::frame_get_texture(frame);  // RefPtr<Gdk::Texture>
 *         })                                  // → future<Gdk::Texture>, deduced
 *   );
 *
 *   // into_future<T> — typed entry point from a raw promise/future:
 *   auto p = Dex::Promise::create();
 *   loader->load_async(cancel, [p, loader](...) {
 *       auto image = loader->load_finish(ar, &err);
 *       if (err) p->reject(std::move(err));
 *       else     p->resolve_object(RefPtr<peel::GObject::Object>(std::move(image)));
 *   });
 *   return peeler::into_future<Gly::Image>(p);      // future<Gly::Image>
 *
 *   // Gom pipeline with raw base (T serves as both extraction and label):
 *   auto r = repo->list_query(q)             // RefPtr<Dex::Future>
 *       | peeler::let_value<Gom::EntityListModel>([&m](auto model) {
 *             m = model;
 *             return m->reload();             // RefPtr<Dex::Future>
 *         })                                  // → future<Gom::EntityListModel>
 *   if (!co_await r) { … }
 */

#include <array>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <expected>
#include <tuple>
#ifdef __cpp_impl_reflection
#  include <meta>
#endif
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "check.hpp"

#include <peel/Dex/Future.h>
#include <peel/Dex/FutureStatus.h>   /* Future::Status enumerators */
#include <peel/Dex/Promise.h>
#include <peel/GLib/Error.h>
#include <peel/coro/SimpleTask.h>
#include <peel/GObject/Object.h>
#include <peel/GObject/Type.h>
#include <peel/GObject/Value.h>
#include <peel/RefPtr.h>
#include <peel/UniquePtr.h>

namespace peeler {

/* Forward declaration so detail:: traits can reference it. */
template<typename T>
class future;

/* Tag type: marks a nullable gom single-row query result. Using
 * into_future<nullable<T>> instead of into_future<T> resolves to the same
 * value type (RefPtr<T> for GObjects) but suppresses the null warn_if_fail
 * that guards non-nullable queries. */
template<typename T>
struct nullable { using type = T; };

/* The outcome of a settled Dex operation: the value on success, or an owning
 * GLib::Error on failure. */
template<typename T>
using expected = std::expected<T, peel::UniquePtr<peel::GLib::Error>>;

/* Suspends the awaiting coroutine until `future` settles, then yields it as an
 * owning RefPtr from await_resume. Backs both `operator co_await` (raw futures)
 * and IntoExpectedAwaiter<T> (typed). */
class DexAwaiter {
    std::atomic<void *> rendezvous { nullptr };
    peel::RefPtr<peel::Dex::Future> future;
    peel::RefPtr<peel::Dex::Future> chain;    /* keeps the finally callback alive */

public:
    explicit DexAwaiter(peel::RefPtr<peel::Dex::Future> f) noexcept
        : future(std::move(f))
    { }

    DexAwaiter(const DexAwaiter &) = delete;
    DexAwaiter(DexAwaiter &&) = delete;
    DexAwaiter &operator=(const DexAwaiter &) = delete;
    DexAwaiter &operator=(DexAwaiter &&) = delete;

    bool await_ready() const noexcept
    {
        return future->get_status() != peel::Dex::Future::Status::PENDING;
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept
    {
        chain = peel::Dex::Future::finally_(
            future,
            [this](peel::Dex::Future *) noexcept -> peel::RefPtr<peel::Dex::Future> {
                void *ptr = rendezvous.exchange(this, std::memory_order_acq_rel);
                if (ptr)
                    std::coroutine_handle<>::from_address(ptr).resume();
                return {};
            });
        void *ptr = rendezvous.exchange(h.address(), std::memory_order_acq_rel);
        if (!ptr)
            return true;
        return false;
    }

    peel::RefPtr<peel::Dex::Future>
    await_resume() noexcept { return std::move(future); }
};

namespace detail {

/* Detect peel boxed types (GLib::Bytes, …): they have a Value::Traits
 * specialisation with OwnedType = RefPtr<T>.  peel::GObject::Object subclasses are
 * excluded so the GObject branch always wins for them. */
template<typename T, typename = void>
struct is_boxed : std::false_type {};

template<typename T>
struct is_boxed<T, std::void_t<typename peel::GObject::Value::Traits<T>::OwnedType>>
    : std::bool_constant<
          !std::is_base_of_v<peel::GObject::Object, T> &&
          std::is_same_v<typename peel::GObject::Value::Traits<T>::OwnedType,
                         peel::RefPtr<T>>> {};

template<typename T>
inline constexpr bool is_boxed_v = is_boxed<T>::value;

/* Detect C++ types registered with PEELER_BOXED_TYPE: OwnedType = T (not
 * RefPtr<T>).  Stored via resolve_boxed with new/delete ownership transfer. */
template<typename T, typename = void>
struct is_cpp_boxed : std::false_type {};

template<typename T>
struct is_cpp_boxed<T, std::void_t<typename peel::GObject::Value::Traits<T>::OwnedType>>
    : std::bool_constant<
          !std::is_base_of_v<peel::GObject::Object, T> &&
          std::is_same_v<typename peel::GObject::Value::Traits<T>::OwnedType, T>> {};

template<typename T>
inline constexpr bool is_cpp_boxed_v = is_cpp_boxed<T>::value;

/* Value type a future<T> resolves to:
 *   GObject or boxed type → RefPtr<T>
 *   scalar                → T
 *   void                  → void  */
template<typename T>
struct dex_value {
    using type = std::conditional_t<
        std::is_base_of_v<peel::GObject::Object, T> || is_boxed_v<T>,
        peel::RefPtr<T>, T>;
};
template<>
struct dex_value<void> { using type = void; };

/* nullable<T>: resolves to the same value type as T (RefPtr<T> for GObjects).
 * extract_value suppresses the null warn_if_fail for these queries. */
template<typename T>
struct dex_value<peeler::nullable<T>> : dex_value<T> {};

template<typename T>
using dex_value_t = typename dex_value<T>::type;

/* Detects nullable<U> where U is a GObject subclass. */
template<typename T>
struct is_nullable_gobject : std::false_type {};
template<typename U>
    requires std::is_base_of_v<peel::GObject::Object, U>
struct is_nullable_gobject<peeler::nullable<U>> : std::true_type {};
template<typename T>
inline constexpr bool is_nullable_gobject_v = is_nullable_gobject<T>::value;

template<typename T>
using dex_expected_t = expected<dex_value_t<T>>;

/* C++ type name for T: fully qualified peel/C++ name when C++26 reflection is
 * available (display_string_of gives e.g. "Gdk::Texture"), otherwise falls
 * back to the GType registry name (e.g. "GdkTexture"). */
template<typename T>
#ifdef __cpp_impl_reflection
consteval
#endif
std::string_view
cpp_type_name() noexcept
{
#ifdef __cpp_impl_reflection
    return std::meta::display_string_of(^^T);
#else
    return peel::GObject::Type::of<T>().name();
#endif
}

/* Extract the value from a settled GValue with correct ownership.
 * GObjects and peel boxed types: dup (owned RefPtr<T>).
 * PEELER_BOXED_TYPE: dup (owned T by value).
 * Scalars: get (value copy). */
template<typename T>
dex_value_t<T>
extract_value(const peel::GObject::Value *v, peeler::loc_t loc)
{
    if constexpr (is_nullable_gobject_v<T>) {
      using U = typename T::type;
      peeler::warn_if_fail(v->holds<peel::GObject::Object>(), loc,
                           "type mismatch in peeler pipeline: expected {}, GValue holds {}",
                           cpp_type_name<U>(), v->get_type().name());
      auto obj = v->template dup<U>();
      if (obj)
          peeler::warn_if_fail(obj->check_type(peel::Type::of<U>()), loc,
                               "type mismatch in peeler pipeline: expected {}, GValue holds {}",
                               cpp_type_name<U>(), v->get_type().name());
      return obj;   /* null is expected — no warning */
    } else if constexpr (std::is_base_of_v<peel::GObject::Object, T>) {
      peeler::warn_if_fail(v->holds<peel::GObject::Object>(), loc,
                           "type mismatch in peeler pipeline: expected {}, GValue holds {}",
                           cpp_type_name<T>(), v->get_type().name());
      auto obj = v->template dup<T>();
      peeler::warn_if_fail(obj != nullptr, loc,
                           "unexpected null in peeler pipeline for {}: "
                           "use into_future<peeler::nullable<T>> for nullable gom queries",
                           cpp_type_name<T>());
      if (obj) {
        peeler::warn_if_fail(obj->check_type(peel::Type::of<T>()), loc,
                             "type mismatch in peeler pipeline: expected {}, GValue holds {}",
                             cpp_type_name<T>(), v->get_type().name());
      }
      return obj;
    } else if constexpr (is_boxed_v<T> || is_cpp_boxed_v<T>) {
      peeler::warn_if_fail(v->holds<T>(), loc,
                           "type mismatch in peeler pipeline: expected {}, GValue holds {}",
                           cpp_type_name<T>(), v->get_type().name());
        return v->template dup<T>();
    } else {
      peeler::warn_if_fail(v->holds<T>(), loc,
                           "type mismatch in peeler pipeline: expected {}, GValue holds {}",
                           cpp_type_name<T>(), v->get_type().name());
        return v->template get<T>();
    }
}

/* Reverse of dex_value_t: given a value type V, recover the element type T.
 * RefPtr<T> → T (GObject and boxed); T → T (scalars). Used by deducing then()
 * to turn a callback's return type back into a future element type. */
template<typename V>
struct dex_element_from_value { using type = V; };
template<typename T>
struct dex_element_from_value<peel::RefPtr<T>> { using type = T; };
template<typename V>
using dex_element_from_value_t = typename dex_element_from_value<std::remove_cvref_t<V>>::type;

/* Trait: detect peeler::future<T> and extract its element type. */
template<typename>
struct is_future : std::false_type {};
template<typename T>
struct is_future<future<T>> : std::true_type {};
template<typename T>
inline constexpr bool is_future_v = is_future<T>::value;

template<typename>
struct future_value {};
template<typename T>
struct future_value<future<T>> { using type = T; };
template<typename T>
using future_value_t = typename future_value<T>::type;

/* Resolves a Dex::Promise with a typed value. Dispatches to the appropriate
 * resolve_* method based on T.  Used by future<T>::promise_type::return_value. */
template<typename T>
void resolve_promise(const peel::RefPtr<peel::Dex::Promise> &p, dex_value_t<T> val) noexcept
{
    if constexpr (is_nullable_gobject_v<T>)
        p->resolve_object(peel::RefPtr<peel::GObject::Object>(std::move(val)));
    else if constexpr (std::is_base_of_v<peel::GObject::Object, T>)
        p->resolve_object(peel::RefPtr<peel::GObject::Object>(std::move(val)));
    else if constexpr (is_boxed_v<T>)
        p->resolve_boxed(peel::GObject::Type::of<T>(), std::move(val).release_ref());
    else if constexpr (is_cpp_boxed_v<T>)
        p->resolve_boxed(peel::GObject::Type::of<T>(), new T(std::move(val)));
    else if constexpr (std::is_same_v<T, bool>)
        p->resolve_boolean(static_cast<gboolean>(val));
    else if constexpr (std::is_same_v<T, int>)
        p->resolve_int(val);
    else if constexpr (std::is_same_v<T, std::int64_t>)
        p->resolve_int64(val);
    else if constexpr (std::is_same_v<T, std::chrono::sys_seconds>)
        p->resolve_int64(val.time_since_epoch().count());
    else if constexpr (std::is_same_v<T, double>)
        p->resolve_double(val);
    else
        static_assert(!sizeof(T *), "peeler::future<T>: no Dex resolve method for this type");
}

} /* namespace detail */

/* ── IntoExpectedAwaiter ─────────────────────────────────────────────────── */

/* Awaitable that extracts a typed value from a settled Dex::Future as
 * expected<dex_value_t<T>>. */
template<typename T = void>
class IntoExpectedAwaiter {
    DexAwaiter inner;
    [[no_unique_address]] peeler::loc_t loc;

public:
    explicit IntoExpectedAwaiter(peel::RefPtr<peel::Dex::Future> f,
                                 peeler::loc_t loc = {}) noexcept
        : inner(std::move(f)), loc(loc) { }

    bool await_ready() const noexcept { return inner.await_ready(); }
    bool await_suspend(std::coroutine_handle<> h) noexcept { return inner.await_suspend(h); }

    detail::dex_expected_t<T>
    await_resume() noexcept
    {
        peel::RefPtr<peel::Dex::Future> settled = inner.await_resume();
        peel::UniquePtr<peel::GLib::Error> error;
        const peel::GObject::Value *value = settled->get_value(&error);
        if (!value)
            return std::unexpected(std::move(error));
        if constexpr (std::is_void_v<T>) {
            /* If this fires, the future resolved with a real GValue but the caller
             * used into_future<void> to discard it. Use the actual return type instead. */
            peeler::warn_if_fail(!value, loc);
            return {};
        } else
            return detail::extract_value<T>(value, loc);
    }
};

/* ── peeler::future<T> ───────────────────────────────────────────────────── *
 * Typed newtype wrapping a RefPtr<Dex::Future>. Carries the element type T
 * so function signatures document what the future resolves to.
 *
 * co_await on a future<T> yields expected<dex_value_t<T>> directly.
 * Implicit conversion
 * to RefPtr<Dex::Future> lets it be returned from let_value/finally callbacks. */
template<typename T>
class future {
    peel::RefPtr<peel::Dex::Future> m;
    [[no_unique_address]] peeler::loc_t loc_;

public:
    explicit future(peel::RefPtr<peel::Dex::Future> f,
                    peeler::loc_t loc = {}) noexcept
        : m(std::move(f)), loc_(loc) { }

    /* Implicit strip for use as a callback return value or raw-future API. */
    operator peel::RefPtr<peel::Dex::Future>() && noexcept
    {
        return std::move(m);
    }

    peeler::loc_t loc() const noexcept { return loc_; }

    /* Direct co_await: yields expected<dex_value_t<T>>.
     * Uses the loc carried from the factory call (let_value, then, etc.) so
     * that type-mismatch warnings cite the user's call site, not peeler internals. */
    IntoExpectedAwaiter<T>
    operator co_await() noexcept
    {
        return IntoExpectedAwaiter<T>(std::move(m), loc_);
    }

    /* Coroutine promise — lets functions declared as returning future<T> be
     * written as coroutines using co_await/co_return.
     *
     * co_return value            → resolves with value (dispatch on T)
     * co_return std::unexpected(err) → rejects with the error
     * co_return;  (void only)   → resolves as success
     *
     * All standard awaitables work inside: IntoExpectedAwaiter, DexAwaiter,
     * peeler::future<T>, and any other type providing await_* members. */
    struct promise_type {
        peel::RefPtr<peel::Dex::Promise> _p { peel::Dex::Promise::create() };

        future get_return_object() noexcept
        {
            return future(peel::RefPtr<peel::Dex::Future>(_p));
        }

        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept { std::terminate(); }

        /* Error path: co_return std::unexpected(err) rejects the future. */
        void return_value(std::unexpected<peel::UniquePtr<peel::GLib::Error>> err) noexcept
        {
            _p->reject(std::move(err).error());
        }

        /* Success path for GObject/boxed/scalar T.
         * void is not supported as a coroutine return type; use future<bool>
         * with co_return true; and co_return std::unexpected(err); instead. */
        void return_value(detail::dex_value_t<T> val) noexcept
            requires (!std::is_void_v<T>)
        {
            detail::resolve_promise<T>(_p, std::move(val));
        }
    };
};

/* ── peeler::when_all ────────────────────────────────────────────────────── *
 *
 * co_await peeler::when_all(f1, f2, ...) suspends until ALL typed futures
 * settle — whether they resolve or reject — then yields:
 *   std::tuple<expected<dex_value_t<T1>>, expected<dex_value_t<T2>>, ...>
 *
 * Futures run concurrently; the coroutine resumes only when the last one
 * settles.  Each slot carries its own expected<T> independently — a rejection
 * in one slot does not cancel or suppress results in other slots.
 *
 * Usage:
 *   auto [a, b] = co_await peeler::when_all(fut_a, fut_b);
 */
template<typename... Ts>
class WhenAllAwaitable {
    static constexpr std::size_t N = sizeof...(Ts);

    std::array<peel::RefPtr<peel::Dex::Future>, N> futures_;
    std::array<peel::RefPtr<peel::Dex::Future>, N> chains_;
    std::array<peeler::loc_t, N>                   locs_;
    std::atomic<unsigned>  remaining_ { N };
    std::atomic<void *>    rendezvous_ { nullptr };

    void on_settled() noexcept
    {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) > 1)
            return;
        void *ptr = rendezvous_.exchange(this, std::memory_order_acq_rel);
        if (ptr)
            std::coroutine_handle<>::from_address(ptr).resume();
    }

    template<std::size_t I>
    void setup_chain() noexcept
    {
        chains_[I] = peel::Dex::Future::finally_(
            futures_[I],
            [this](peel::Dex::Future *) noexcept -> peel::RefPtr<peel::Dex::Future> {
                on_settled();
                return {};
            });
    }

    template<std::size_t I>
    detail::dex_expected_t<std::tuple_element_t<I, std::tuple<Ts...>>>
    extract_slot() noexcept
    {
        using T = std::tuple_element_t<I, std::tuple<Ts...>>;
        peel::UniquePtr<peel::GLib::Error> err;
        const peel::GObject::Value *v = futures_[I]->get_value(&err);
        if (!v)
            return std::unexpected(std::move(err));
        return detail::extract_value<T>(v, locs_[I]);
    }

public:
    explicit WhenAllAwaitable(std::array<peel::RefPtr<peel::Dex::Future>, N> futs,
                               std::array<peeler::loc_t, N> locs) noexcept
        : futures_(std::move(futs)), chains_{}, locs_(std::move(locs))
    {}

    WhenAllAwaitable(const WhenAllAwaitable &) = delete;
    WhenAllAwaitable &operator=(const WhenAllAwaitable &) = delete;

    bool await_ready() const noexcept
    {
        for (const auto &f : futures_)
            if (f->get_status() == peel::Dex::Future::Status::PENDING)
                return false;
        return true;
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept
    {
        [this]<std::size_t... Is>(std::index_sequence<Is...>) {
            (setup_chain<Is>(), ...);
        }(std::index_sequence_for<Ts...>{});
        void *ptr = rendezvous_.exchange(h.address(), std::memory_order_acq_rel);
        return !ptr;
    }

    std::tuple<detail::dex_expected_t<Ts>...>
    await_resume() noexcept
    {
        return [this]<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::make_tuple(extract_slot<Is>()...);
        }(std::index_sequence_for<Ts...>{});
    }
};

template<typename... Ts>
WhenAllAwaitable<Ts...>
when_all(future<Ts>... futs)
{
    std::array<peeler::loc_t, sizeof...(Ts)> locs { futs.loc()... };
    std::array<peel::RefPtr<peel::Dex::Future>, sizeof...(Ts)> raw {
        peel::RefPtr<peel::Dex::Future>(std::move(futs))...
    };
    return WhenAllAwaitable<Ts...>(std::move(raw), std::move(locs));
}

/* ── peeler::first ───────────────────────────────────────────────────────── *
 *
 * co_await peeler::first(f1, f2, ...) suspends until the FIRST of the futures
 * settles (resolve or reject) and yields that winner's result:
 *   expected<dex_value_t<T>>
 *
 * The other futures are abandoned. This is a thin typed wrapper over libdex's
 * dex_future_first, which owns the set and keeps the losers alive until the
 * result is dropped — so nothing dangles. (A hand-rolled when_all-style race
 * could not: resuming on the first settle leaves a slower loser's continuation
 * to fire later on a freed awaitable.)
 *
 * Homogeneous by construction — every future must share one type T, so the
 * single shared result needs no variant. dex_future_first propagates only the
 * winner's value, not its identity; a caller that must know *which* future won
 * should tag each T before racing.
 *
 * Usage:
 *   auto r = co_await peeler::first(fut_a, fut_b);
 */
template<typename T, typename... Us>
    requires (std::is_same_v<T, Us> && ...)
future<T>
first(future<T> f0, future<Us>... fs)
{
    peeler::loc_t loc = f0.loc();
    std::array<peel::RefPtr<peel::Dex::Future>, 1 + sizeof...(Us)> owned {
        peel::RefPtr<peel::Dex::Future>(std::move(f0)),
        peel::RefPtr<peel::Dex::Future>(std::move(fs))...
    };
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        peel::Dex::Future *const ptrs[] { owned[Is]... };
        return future<T>(peel::Dex::Future::firstv(ptrs), loc);
    }(std::make_index_sequence<1 + sizeof...(Us)>{});
}

/* ── peeler::alongside ──────────────────────────────────────────────────── *
 *
 * future<InT>
 *     | alongside<OutT>(f: dex_value_t<InT> → future<OutT>)
 * → PairedFuture<InT, OutT, F>
 *     ├── | then(g: (dex_value_t<InT>, dex_value_t<OutT>) → C)    → future<C>
 *     ├── | let_value(g: (...) → future<C>)                        → future<C>
 *     └── co_await  → expected<pair<dex_value_t<InT>, dex_value_t<OutT>>>
 *
 * The value pair is never stored in a GValue — it is carried through C++
 * lambda captures (pipe path) or returned directly from await_resume (co_await
 * path).  Use alongside when a later pipe stage needs an intermediate value
 * from an earlier stage without nesting lambdas.
 *
 * Usage (flat four-stage chain):
 *   auto texture = co_await (
 *       fetch_bytes(url, cancel)
 *       | peeler::let_value<GLib::Bytes>(check_status)
 *       | peeler::alongside<Gdk::Texture>([cancel](RefPtr<GLib::Bytes> b) {
 *             return decode_texture(b, cancel);
 *         })
 *       | peeler::then([path](RefPtr<GLib::Bytes> b, RefPtr<Gdk::Texture> t) {
 *             save_to_cache(b, path);
 *             return t;
 *         })
 *   );
 *
 * co_await the PairedFuture directly (GCC -Wmaybe-uninitialized split pattern):
 *   auto r = co_await (base | alongside<OutT>(f));
 *   if (!r) { … handle error … }
 *   auto& [in_val, out_val] = *r;
 */

template<typename InT, typename OutT, typename F>
class AlongsideAwaitable {
    peel::RefPtr<peel::Dex::Future> base_;
    F f_;
    peeler::loc_t loc_in_, loc_out_;

    peel::RefPtr<peel::Dex::Future> out_future_;
    peel::RefPtr<peel::Dex::Future> base_chain_;
    peel::RefPtr<peel::Dex::Future> out_chain_;
    std::atomic<void *>              rendezvous_ { nullptr };

public:
    AlongsideAwaitable(peel::RefPtr<peel::Dex::Future> base, F f,
                        peeler::loc_t loc_in, peeler::loc_t loc_out) noexcept
        : base_(std::move(base)), f_(std::move(f))
        , loc_in_(loc_in), loc_out_(loc_out)
    {}

    AlongsideAwaitable(const AlongsideAwaitable &) = delete;
    AlongsideAwaitable &operator=(const AlongsideAwaitable &) = delete;

    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> h) noexcept
    {
        base_chain_ = peel::Dex::Future::finally_(base_,
            [this](peel::Dex::Future *bf) mutable
                -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> err;
                const peel::GObject::Value *v = bf->get_value(&err);
                if (v) {
                    out_future_ = peel::RefPtr<peel::Dex::Future>(
                        f_(detail::extract_value<InT>(v, loc_in_)));
                    out_chain_ = peel::Dex::Future::finally_(out_future_,
                        [this](peel::Dex::Future *) mutable
                            -> peel::RefPtr<peel::Dex::Future> {
                            void *ptr = rendezvous_.exchange(
                                this, std::memory_order_acq_rel);
                            if (ptr)
                                std::coroutine_handle<>::from_address(ptr).resume();
                            return {};
                        });
                } else {
                    void *ptr = rendezvous_.exchange(
                        this, std::memory_order_acq_rel);
                    if (ptr)
                        std::coroutine_handle<>::from_address(ptr).resume();
                }
                return {};
            });
        void *ptr = rendezvous_.exchange(h.address(), std::memory_order_acq_rel);
        return !ptr;
    }

    expected<std::pair<detail::dex_value_t<InT>, detail::dex_value_t<OutT>>>
    await_resume() noexcept
    {
        using Pair = std::pair<detail::dex_value_t<InT>, detail::dex_value_t<OutT>>;
        peel::UniquePtr<peel::GLib::Error> err;
        const peel::GObject::Value *v1 = base_->get_value(&err);
        if (!v1)
            return std::unexpected(std::move(err));
        auto in_val = detail::extract_value<InT>(v1, loc_in_);
        const peel::GObject::Value *v2 = out_future_->get_value(&err);
        if (!v2)
            return std::unexpected(std::move(err));
        return Pair { std::move(in_val), detail::extract_value<OutT>(v2, loc_out_) };
    }
};

/* PairedFuture — lazy holder of (base future, alongside callback, location info).
 * Consumed by whichever exit path is taken first (pipe or co_await). */
template<typename InT, typename OutT, typename F>
struct PairedFuture {
    peel::RefPtr<peel::Dex::Future> base;
    F f;
    peeler::loc_t loc_in;
    peeler::loc_t loc_out;

    AlongsideAwaitable<InT, OutT, F>
    operator co_await() && noexcept
    {
        return AlongsideAwaitable<InT, OutT, F>(
            std::move(base), std::move(f), loc_in, loc_out);
    }
};

template<typename OutT, typename F>
struct AlongsideAdaptor {
    F callback;
    [[no_unique_address]] peeler::loc_t loc;
};

template<typename OutT, typename F>
AlongsideAdaptor<OutT, std::decay_t<F>>
alongside(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

template<typename InT, typename OutT, typename F>
PairedFuture<InT, OutT, F>
operator|(future<InT> base, AlongsideAdaptor<OutT, F> adaptor)
{
    auto loc_in = base.loc();
    return PairedFuture<InT, OutT, F> {
        peel::RefPtr<peel::Dex::Future>(std::move(base)),
        std::move(adaptor.callback),
        loc_in,
        adaptor.loc
    };
}

/* ── Pipe adaptors ───────────────────────────────────────────────────────── *
 *
 * into_future<T>(f)    — typed entry point from a raw promise/future (direct call).
 * reject(err)          — create a rejected future (for use inside callbacks).
 *
 * let_value<OutT>(cb)  — success-only async flat-map; cb receives dex_value_t<InT>,
 *                        returns RefPtr<Dex::Future> to chain another async step.
 *                        For raw base, OutT = InT (extraction + label).
 *                        For typed base future<InT>, InT from LHS; OutT labels output.
 *
 * let_value(cb)        — deducing form: cb returns future<OutT> (typed),
 *                        OutT deduced from cb's return type.
 *                        InT inferred from the LHS future<InT>.
 *                        For raw base, cb must take no argument.
 *
 * then<OutT>(cb)       — success-only synchronous map; cb receives dex_value_t<InT>,
 *                        returns dex_value_t<OutT> directly (no future).
 *                        Requires typed future<InT> base (InT must be known).
 *                        Cost: one Promise allocation per step (just<OutT>).
 *
 * then(cb)             — deducing form: cb returns a non-future value,
 *                        OutT deduced from cb's return type.
 *                        Requires typed future<InT> base.
 *
 * let_error<T>(cb)     — error-path async recovery: cb receives
 *                        UniquePtr<GLib::Error>, returns RefPtr<Dex::Future>.
 *                        Success path passes through unchanged. T = output type.
 *
 * upon_error(cb)       — error-path synchronous recovery: cb receives
 *                        UniquePtr<GLib::Error>, returns dex_value_t<T>.
 *                        One Promise allocation (just<T>). T inferred from base.
 *
 * timeout(duration)    — rejects with timeout error if not settled in time.
 *                        Accepts any std::chrono::duration.
 *
 * finally<T>(cb)       — fires on any settlement; cb receives expected<T>.
 *                        Returns RefPtr<Dex::Future> (output type from callback).
 *
 * into_future<T>()     — pipe adaptor to retag a raw or differently-labelled
 *                        future as future<T>. */

/* ── just ── */

/* just(val) — pre-resolved future from a GObject RefPtr value.
 * T is deduced from the argument so callers can write just(tex). */
template<typename T>
    requires std::is_base_of_v<peel::GObject::Object, T>
future<T>
just(peel::RefPtr<T> val) noexcept
{
    return future<T>(peel::Dex::Future::create_for_object(
        val->template cast<peel::GObject::Object>()));
}

/* ── reject ── */

inline peel::RefPtr<peel::Dex::Future>
reject(peel::UniquePtr<peel::GLib::Error> error) noexcept
{
    return peel::Dex::Future::create_for_error(std::move(error));
}

/* ── just (explicit T) ── */

/* just<T>(val) — pre-resolved typed future from any value type.
 * Explicit T required; handles peel boxed, cpp boxed, and scalar T.
 * Constrained to non-GObject types: for GObject T the deducing overload above
 * is always used (it calls create_for_object rather than the promise path). */
template<typename T>
    requires (!std::is_base_of_v<peel::GObject::Object, T>)
future<T>
just(detail::dex_value_t<T> val) noexcept
{
    auto p = peel::Dex::Promise::create();
    detail::resolve_promise<T>(p, std::move(val));
    return future<T>(peel::RefPtr<peel::Dex::Future>(p));
}

/* ── let_value (explicit InT + OutT) ── */

/* InT: type extracted from the input future.
 * OutT: label on the output future (independent of what the callback's returned
 *       future actually resolves to).
 * Single-arg let_value<T> keeps InT == OutT == T for backward compatibility. */
template<typename InT, typename OutT, typename F>
struct LetValueAdaptor {
    F callback;
    [[no_unique_address]] peeler::loc_t loc;
};

/* Single-template-arg: extraction type and output label are both T. */
template<typename T, typename F>
LetValueAdaptor<T, T, std::decay_t<F>>
let_value(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

/* Two-template-arg: InT for extraction, OutT for output label.
 * Use when the callback returns a future whose resolved type differs from InT,
 * e.g. let_value<HiddenState, void> when the callback returns insert_entity
 * (resolves to bool) and the caller only checks for errors, not the value. */
template<typename InT, typename OutT, typename F>
LetValueAdaptor<InT, OutT, std::decay_t<F>>
let_value(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

/* Raw base: InT for extraction, OutT for output label. */
template<typename InT, typename OutT, typename F>
future<OutT>
operator|(peel::RefPtr<peel::Dex::Future> base, LetValueAdaptor<InT, OutT, F> adaptor)
{
    auto loc = adaptor.loc;
    return future<OutT>(
        peel::Dex::Future::then(
            std::move(base),
            [cb = std::move(adaptor.callback), loc]
            (peel::Dex::Future *f) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> ignored;
                const peel::GObject::Value *value = f->get_value(&ignored);
                if constexpr (std::is_void_v<InT>)
                    return cb();
                else
                    return cb(detail::extract_value<InT>(value, loc));
            }),
        loc);
}

/* Typed base: PrevT (from future<PrevT>) is the extraction type; OutT from the
 * adaptor is the output label. Allows let_value<Frame>(cb_receiving_Image) after
 * a future<Image> without needing an intermediate into_future<>. */
template<typename PrevT, typename InT, typename OutT, typename F>
future<OutT>
operator|(future<PrevT> base, LetValueAdaptor<InT, OutT, F> adaptor)
{
    auto loc = adaptor.loc;
    return future<OutT>(
        peel::Dex::Future::then(
            peel::RefPtr<peel::Dex::Future>(std::move(base)),
            [cb = std::move(adaptor.callback), loc]
            (peel::Dex::Future *f) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> ignored;
                const peel::GObject::Value *value = f->get_value(&ignored);
                if constexpr (std::is_void_v<PrevT>)
                    return cb();
                else
                    return cb(detail::extract_value<PrevT>(value, loc));
            }),
        loc);
}

/* ── let_value (deducing OutT from callback return type) ── */

template<typename F>
struct LetValueDeducingAdaptor {
    F callback;
    [[no_unique_address]] peeler::loc_t loc;
};

/* let_value(cb) — no explicit template param; cb must return peeler::future<T>.
 * Distinguish from let_value<OutT>(cb) by the absence of an explicit T argument:
 * the explicit-T overload requires two template params (OutT, F), the deducing
 * overload has only F, so 'let_value(cb)' unambiguously selects this one. */
template<typename F>
LetValueDeducingAdaptor<std::decay_t<F>>
let_value(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

/* Raw base + no-arg callback returning future<OutT>. No extraction, no loc needed. */
template<typename F>
auto operator|(peel::RefPtr<peel::Dex::Future> base, LetValueDeducingAdaptor<F> adaptor)
    requires detail::is_future_v<std::invoke_result_t<F>>
{
    using OutT = detail::future_value_t<std::invoke_result_t<F>>;
    return future<OutT>(
        peel::Dex::Future::then(
            std::move(base),
            [cb = std::move(adaptor.callback)](peel::Dex::Future *) mutable
                -> peel::RefPtr<peel::Dex::Future> {
                return peel::RefPtr<peel::Dex::Future>(cb());
            }));
}

/* Typed base future<InT> + callback receiving dex_value_t<InT>, returning
 * future<OutT>. OutT is deduced from the callback's return type. */
template<typename InT, typename F>
auto operator|(future<InT> base, LetValueDeducingAdaptor<F> adaptor)
    requires detail::is_future_v<std::invoke_result_t<F, detail::dex_value_t<InT>>>
{
    using OutT = detail::future_value_t<
        std::invoke_result_t<F, detail::dex_value_t<InT>>>;
    auto loc = adaptor.loc;
    return future<OutT>(
        peel::Dex::Future::then(
            peel::RefPtr<peel::Dex::Future>(std::move(base)),
            [cb = std::move(adaptor.callback), loc]
            (peel::Dex::Future *f) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> ignored;
                const peel::GObject::Value *value = f->get_value(&ignored);
                if constexpr (std::is_void_v<InT>)
                    return peel::RefPtr<peel::Dex::Future>(cb());
                else
                    return peel::RefPtr<peel::Dex::Future>(
                        cb(detail::extract_value<InT>(value, loc)));
            }),
        loc);
}

/* ── then (synchronous map) ── *
 *
 * Unlike let_value, the callback returns a plain dex_value_t<OutT> — no new
 * future. The implementation wraps it in just<OutT>() before
 * returning to libdex (libdex has no native synchronous map; every combinator
 * callback must return a DexFuture*). Cost: one Promise allocation per step,
 * settled synchronously without touching the scheduler.
 *
 * Only typed future<InT> bases are supported; InT must be known to extract
 * the input value. For a raw RefPtr<Dex::Future> base, use let_value<T>. */

template<typename OutT, typename F>
struct ThenAdaptor {
    F callback;
    [[no_unique_address]] peeler::loc_t loc;
};

template<typename OutT, typename F>
ThenAdaptor<OutT, std::decay_t<F>>
then(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

template<typename F>
struct ThenDeducingAdaptor {
    F callback;
    [[no_unique_address]] peeler::loc_t loc;
};

/* then(cb) — deducing form. cb must return a non-future value so it is
 * unambiguous with let_value(cb) (which requires future<T> return). */
template<typename F>
ThenDeducingAdaptor<std::decay_t<F>>
then(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

/* Explicit OutT: cb receives dex_value_t<InT>, returns dex_value_t<OutT>. */
template<typename InT, typename OutT, typename F>
future<OutT>
operator|(future<InT> base, ThenAdaptor<OutT, F> adaptor)
{
    auto loc = adaptor.loc;
    return future<OutT>(
        peel::Dex::Future::then(
            peel::RefPtr<peel::Dex::Future>(std::move(base)),
            [cb = std::move(adaptor.callback), loc]
            (peel::Dex::Future *f) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> ignored;
                const peel::GObject::Value *value = f->get_value(&ignored);
                if constexpr (std::is_void_v<InT>)
                    return just<OutT>(cb());
                else
                    return just<OutT>(cb(detail::extract_value<InT>(value, loc)));
            }),
        loc);
}

/* Deducing OutT: inferred from cb's return type; constrained to non-future
 * return so this overload is distinct from the let_value deducing form.
 * dex_element_from_value_t maps RefPtr<T> → T so the output future is
 * future<T>, not future<RefPtr<T>>, for GObject/boxed return types. */
template<typename InT, typename F>
auto operator|(future<InT> base, ThenDeducingAdaptor<F> adaptor)
    requires (!detail::is_future_v<std::invoke_result_t<F, detail::dex_value_t<InT>>>)
{
    using ValOut = std::remove_cvref_t<std::invoke_result_t<F, detail::dex_value_t<InT>>>;
    using OutT = detail::dex_element_from_value_t<ValOut>;
    auto loc = adaptor.loc;
    return future<OutT>(
        peel::Dex::Future::then(
            peel::RefPtr<peel::Dex::Future>(std::move(base)),
            [cb = std::move(adaptor.callback), loc]
            (peel::Dex::Future *f) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> ignored;
                const peel::GObject::Value *value = f->get_value(&ignored);
                if constexpr (std::is_void_v<InT>)
                    return just<OutT>(cb());
                else
                    return just<OutT>(cb(detail::extract_value<InT>(value, loc)));
            }),
        loc);
}

/* ── alongside binary pipe continuations ──────────────────────────────────
 *
 * PairedFuture | then(g)       — g(dex_value_t<InT>, dex_value_t<OutT>) → C
 * PairedFuture | let_value(g)  — g(dex_value_t<InT>, dex_value_t<OutT>) → future<C>
 *
 * In both cases the InT value from the base future is:
 *   1. Extracted once and forwarded to the alongside callback f (own copy).
 *   2. Captured in the inner callback for the binary g invocation.
 * No pair is ever stored in a GValue — both values travel through C++ captures
 * inside nested Dex::Future::then callbacks. */

template<typename InT, typename OutT, typename FA, typename G>
auto
operator|(PairedFuture<InT, OutT, FA> paired, ThenDeducingAdaptor<G> adaptor)
    requires (!detail::is_future_v<
        std::invoke_result_t<G, detail::dex_value_t<InT>, detail::dex_value_t<OutT>>>)
{
    using ValOut = std::remove_cvref_t<
        std::invoke_result_t<G, detail::dex_value_t<InT>, detail::dex_value_t<OutT>>>;
    using OutT2  = detail::dex_element_from_value_t<ValOut>;
    auto loc_in  = paired.loc_in;
    auto loc_out = paired.loc_out;
    auto loc_g   = adaptor.loc;
    return future<OutT2>(
        peel::Dex::Future::then(
            std::move(paired.base),
            [f       = std::move(paired.f),
             g       = std::move(adaptor.callback),
             loc_in, loc_out, loc_g]
            (peel::Dex::Future *bf) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> err;
                const peel::GObject::Value *v1 = bf->get_value(&err);
                if (!v1)
                    return peel::Dex::Future::create_for_error(std::move(err));
                auto in_val   = detail::extract_value<InT>(v1, loc_in);
                auto in_for_f = in_val;
                peel::RefPtr<peel::Dex::Future> out_f = f(std::move(in_for_f));
                return peel::Dex::Future::then(
                    std::move(out_f),
                    [in_val = std::move(in_val), g = std::move(g), loc_out, loc_g]
                    (peel::Dex::Future *of) mutable -> peel::RefPtr<peel::Dex::Future> {
                        peel::UniquePtr<peel::GLib::Error> err2;
                        const peel::GObject::Value *v2 = of->get_value(&err2);
                        if (!v2)
                            return peel::Dex::Future::create_for_error(std::move(err2));
                        auto out_val = detail::extract_value<OutT>(v2, loc_out);
                        return just<OutT2>(g(std::move(in_val), std::move(out_val)));
                    });
            }),
        loc_g);
}

template<typename InT, typename OutT, typename FA, typename G>
auto
operator|(PairedFuture<InT, OutT, FA> paired, LetValueDeducingAdaptor<G> adaptor)
    requires detail::is_future_v<
        std::invoke_result_t<G, detail::dex_value_t<InT>, detail::dex_value_t<OutT>>>
{
    using OutT2  = detail::future_value_t<
        std::invoke_result_t<G, detail::dex_value_t<InT>, detail::dex_value_t<OutT>>>;
    auto loc_in  = paired.loc_in;
    auto loc_out = paired.loc_out;
    auto loc_g   = adaptor.loc;
    return future<OutT2>(
        peel::Dex::Future::then(
            std::move(paired.base),
            [f       = std::move(paired.f),
             g       = std::move(adaptor.callback),
             loc_in, loc_out, loc_g]
            (peel::Dex::Future *bf) mutable -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> err;
                const peel::GObject::Value *v1 = bf->get_value(&err);
                if (!v1)
                    return peel::Dex::Future::create_for_error(std::move(err));
                auto in_val   = detail::extract_value<InT>(v1, loc_in);
                auto in_for_f = in_val;
                peel::RefPtr<peel::Dex::Future> out_f = f(std::move(in_for_f));
                return peel::Dex::Future::then(
                    std::move(out_f),
                    [in_val = std::move(in_val), g = std::move(g), loc_out, loc_g]
                    (peel::Dex::Future *of) mutable -> peel::RefPtr<peel::Dex::Future> {
                        peel::UniquePtr<peel::GLib::Error> err2;
                        const peel::GObject::Value *v2 = of->get_value(&err2);
                        if (!v2)
                            return peel::Dex::Future::create_for_error(std::move(err2));
                        auto out_val = detail::extract_value<OutT>(v2, loc_out);
                        return peel::RefPtr<peel::Dex::Future>(
                            g(std::move(in_val), std::move(out_val)));
                    });
            }),
        loc_g);
}

/* ── let_error / upon_error ── *
 *
 * Error-path counterparts to let_value / then. Both only fire on rejection;
 * the success path passes through unchanged. Backed by Dex::Future::catch_().
 *
 * let_error<T>(cb)  — async error recovery: cb receives UniquePtr<GLib::Error>,
 *                     returns RefPtr<Dex::Future>. Same output type T as input.
 *
 * upon_error(cb)    — synchronous error recovery: cb receives
 *                     UniquePtr<GLib::Error>, returns dex_value_t<T> directly.
 *                     Wraps result in just<T>() — one Promise allocation. */

template<typename T, typename F>
struct LetErrorAdaptor { F callback; };

template<typename T, typename F>
LetErrorAdaptor<T, std::decay_t<F>>
let_error(F &&callback)
{
    return { std::forward<F>(callback) };
}

template<typename T, typename F>
struct UponErrorAdaptor { F callback; };

template<typename T, typename F>
UponErrorAdaptor<T, std::decay_t<F>>
upon_error(F &&callback)
{
    return { std::forward<F>(callback) };
}

/* let_error: raw base — T labels both the expected input and output type. */
template<typename T, typename F>
future<T>
operator|(peel::RefPtr<peel::Dex::Future> base, LetErrorAdaptor<T, F> adaptor)
{
    return future<T>(
        peel::Dex::Future::catch_(
            std::move(base),
            [cb = std::move(adaptor.callback)](peel::Dex::Future *f) mutable
                -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> error;
                f->get_value(&error);
                return cb(std::move(error));
            }));
}

/* let_error: typed base — T is the output label; InT from future<InT> passes through. */
template<typename InT, typename F>
future<InT>
operator|(future<InT> base, LetErrorAdaptor<InT, F> adaptor)
{
    auto loc = base.loc();
    return future<InT>(
        peel::Dex::Future::catch_(
            peel::RefPtr<peel::Dex::Future>(std::move(base)),
            [cb = std::move(adaptor.callback)](peel::Dex::Future *f) mutable
                -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> error;
                f->get_value(&error);
                return cb(std::move(error));
            }),
        loc);
}

/* upon_error: raw base — T labels both input and output. */
template<typename T, typename F>
future<T>
operator|(peel::RefPtr<peel::Dex::Future> base, UponErrorAdaptor<T, F> adaptor)
{
    return future<T>(
        peel::Dex::Future::catch_(
            std::move(base),
            [cb = std::move(adaptor.callback)](peel::Dex::Future *f) mutable
                -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> error;
                f->get_value(&error);
                return just<T>(cb(std::move(error)));
            }));
}

/* upon_error: typed base. */
template<typename InT, typename F>
future<InT>
operator|(future<InT> base, UponErrorAdaptor<InT, F> adaptor)
{
    auto loc = base.loc();
    return future<InT>(
        peel::Dex::Future::catch_(
            peel::RefPtr<peel::Dex::Future>(std::move(base)),
            [cb = std::move(adaptor.callback)](peel::Dex::Future *f) mutable
                -> peel::RefPtr<peel::Dex::Future> {
                peel::UniquePtr<peel::GLib::Error> error;
                f->get_value(&error);
                return just<InT>(cb(std::move(error)));
            }),
        loc);
}

/* ── timeout ── *
 *
 * Pipe adaptor that wraps a future with a deadline. If the future doesn't
 * settle within the given duration it rejects with a timeout error.
 * Backed by Dex::Future::with_timeout() (libdex 1.2+, microsecond resolution).
 *
 * Usage: future | peeler::timeout(std::chrono::seconds(5)) */

struct TimeoutAdaptor { gint64 usec; };

template<typename Rep, typename Period>
TimeoutAdaptor
timeout(std::chrono::duration<Rep, Period> d) noexcept
{
    return { std::chrono::duration_cast<std::chrono::microseconds>(d).count() };
}

/* Typed base — preserves element type. */
template<typename T>
future<T>
operator|(future<T> base, TimeoutAdaptor a)
{
    auto loc = base.loc();
    return future<T>(peel::Dex::Future::with_timeout(
        peel::RefPtr<peel::Dex::Future>(std::move(base)), a.usec), loc);
}

/* Raw base — returns untyped RefPtr since no element type is known. */
inline peel::RefPtr<peel::Dex::Future>
operator|(peel::RefPtr<peel::Dex::Future> base, TimeoutAdaptor a)
{
    return peel::Dex::Future::with_timeout(std::move(base), a.usec);
}

/* ── finally ── */

template<typename T, typename F>
struct FinallyAdaptor {
    F callback;
    [[no_unique_address]] peeler::loc_t loc;
};

template<typename T = void, typename F>
FinallyAdaptor<T, std::decay_t<F>>
finally(F &&callback, std::source_location loc = std::source_location::current())
{
    return { std::forward<F>(callback), peeler::loc_t{loc} };
}

template<typename T, typename F>
peel::RefPtr<peel::Dex::Future>
operator|(peel::RefPtr<peel::Dex::Future> base, FinallyAdaptor<T, F> adaptor)
{
    return peel::Dex::Future::finally_(
        std::move(base),
        [cb = std::move(adaptor.callback), loc = adaptor.loc]
        (peel::Dex::Future *f) mutable -> peel::RefPtr<peel::Dex::Future> {
            peel::UniquePtr<peel::GLib::Error> error;
            const peel::GObject::Value *value = f->get_value(&error);
            auto expected = [&]() -> detail::dex_expected_t<T> {
                if (!value)
                    return std::unexpected(std::move(error));
                if constexpr (std::is_void_v<T>)
                    return {};
                else
                    return detail::extract_value<T>(value, loc);
            }();
            return cb(std::move(expected));
        });
}

template<typename A, typename T, typename F>
peel::RefPtr<peel::Dex::Future>
operator|(future<A> base, FinallyAdaptor<T, F> adaptor)
{
    return peel::RefPtr<peel::Dex::Future>(std::move(base)) | std::move(adaptor);
}

/* ── into_future (type-retag) ── *
 *
 * Direct call: into_future<T>(promise/future) — typed entry point from a raw
 * Dex future or promise. Covariant RefPtr lets into_future<T>(promise) work
 * without an explicit cast.
 *
 * Pipe form: into_future<T>() — wraps a future pipeline in a typed future<T>.
 * Use to give a raw RefPtr<Dex::Future> a type label, or to retag a future<A>
 * as future<B> when the callback returns a different-typed raw future.
 *
 * Prefer let_value<T> or the deducing let_value() form for pipeline steps;
 * use into_future<T> for entry points and edge cases. */

template<typename T>
future<T>
into_future(peel::RefPtr<peel::Dex::Future> f,
            std::source_location loc = std::source_location::current()) noexcept
{
    return future<T>(std::move(f), peeler::loc_t{loc});
}

template<typename T>
struct IntoFutureAdaptor {};

template<typename T>
IntoFutureAdaptor<T>
into_future() noexcept { return {}; }

template<typename T>
future<T>
operator|(peel::RefPtr<peel::Dex::Future> f, IntoFutureAdaptor<T>) noexcept
{
    return future<T>(std::move(f));
}

template<typename A, typename T>
future<T>
operator|(future<A> f, IntoFutureAdaptor<T>) noexcept
{
    auto loc = f.loc();
    return future<T>(peel::RefPtr<peel::Dex::Future>(std::move(f)), loc);
}

/* into_task(fut) — fire a typed future as a SimpleTask, discarding the result.
 * Non-cancelled rejections are logged at the call site via warn_if_fail. */
template<typename T>
peel::coro::SimpleTask
into_task(future<T> fut, peeler::loc_t loc = std::source_location::current())
{
    auto r = co_await std::move(fut);
    if (!r && !r.error()->matches(G_IO_ERROR, G_IO_ERROR_CANCELLED))
        peeler::warn_if_fail(false, loc, "into_task: {}", r.error()->message);
}

} /* namespace peeler */

/* `co_await raw_dex_future` → the settled RefPtr<Dex::Future> for manual
 * inspection. Placed in namespace peel for ADL from any call-site namespace. */
namespace peel {

inline peeler::DexAwaiter
operator co_await(peel::RefPtr<peel::Dex::Future> future) noexcept
{
    return peeler::DexAwaiter(std::move(future));
}

} /* namespace peel */
