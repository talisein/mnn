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

#include "mnn_gerror.hpp"
#include "peeler/coro.hpp"
#include <coroutine>
#include <expected>
#include <system_error>
#include <utility>
#include <peel/Dex/Future.h>
#include <peel/RefPtr.h>

namespace mnn
{
    /* A peeler::future<T> whose co_await yields
     *   std::expected<dex_value_t<T>, std::error_code>
     * instead of peeler's expected<…, UniquePtr<GLib::Error>>. Internally the
     * future rejects with mnn-domain GErrors (make_gerror); the GError never
     * reaches the awaiting caller. Awaitable from any peeler::future coroutine. */
    template<typename T>
    class result_future
    {
        peeler::future<T> inner;

    public:
        using value_type = std::expected<peeler::detail::dex_value_t<T>, std::error_code>;

        explicit result_future(peeler::future<T> f) noexcept
            : inner(std::move(f)) { }

        /* For raw Dex APIs (finally_, run_coro, ...). */
        operator peel::RefPtr<peel::Dex::Future>() && noexcept
        {
            return std::move(inner);
        }

        /* IntoExpectedAwaiter is not movable (its DexAwaiter captures this in
         * await_suspend), so it is built in place and the awaiter is returned
         * as a prvalue. */
        class awaiter
        {
            peeler::IntoExpectedAwaiter<T> a;

        public:
            awaiter(peel::RefPtr<peel::Dex::Future> f, peeler::loc_t loc) noexcept
                : a(std::move(f), loc) { }
            awaiter(const awaiter &) = delete;
            awaiter &operator=(const awaiter &) = delete;

            bool await_ready() const noexcept { return a.await_ready(); }
            bool await_suspend(std::coroutine_handle<> h) noexcept { return a.await_suspend(h); }

            value_type
            await_resume() noexcept
            {
                auto r = a.await_resume();
                if (!r)
                    return std::unexpected(to_error_code(*r.error()));
                return std::move(*r);
            }
        };

        awaiter
        operator co_await() noexcept
        {
            auto loc = inner.loc();
            return awaiter(std::move(inner), loc);
        }
    };

} // namespace mnn
