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

#include "peeler/coro.hpp"
#include <chrono>
#include <deque>
#include <memory>
#include <peel/Dex/Future.h>
#include <peel/Dex/Promise.h>
#include <peel/Gio/Cancellable.h>
#include <peel/RefPtr.h>

namespace mnn
{
    /* Serializes requests to a rate-limited service: at most one holder at a
     * time, each turn starting at least `gap` after the previous turn's
     * request finished, handed out in FIFO order.
     *
     *   auto ticket = cooldown.enter();          // place in line fixed here
     *   if (!co_await ticket.wait(cancel)) ...   // rejects on cancel
     *   ... send exactly one request ...
     *   // ~Ticket: next turn begins `gap` from now
     *
     * Main-context only: enter() and the hand-offs run on one thread, so the
     * queue needs no locking. A ticket that is cancelled while queued (or
     * destroyed before waiting) leaves the line without costing a gap.
     *
     * Hold a ticket for exactly one request, in its own coroutine frame:
     * calling enter() again while holding a turn waits on yourself forever. */
    class Cooldown
    {
        struct State
        {
            std::chrono::milliseconds                     gap;
            bool                                          busy = false;
            std::deque<peel::RefPtr<peel::Dex::Promise>>  queue;

            void hand_off();
        };

        /* Shared with tickets and the gap timer, which may outlive the owner. */
        std::shared_ptr<State> state;

    public:
        class Ticket
        {
            friend class Cooldown;

            std::shared_ptr<State>           state;
            peel::RefPtr<peel::Dex::Future>  go;    /* resolved when it's our turn */
            peel::RefPtr<peel::Dex::Promise> queued; /* our queue entry, if we had to wait */
            bool                             used = false;

            Ticket(std::shared_ptr<State> s, peel::RefPtr<peel::Dex::Future> go,
                   peel::RefPtr<peel::Dex::Promise> queued) noexcept;

        public:
            Ticket(const Ticket &) = delete;
            Ticket &operator=(const Ticket &) = delete;
            ~Ticket();

            /* Resolves true when it's this ticket's turn; rejects with
             * mnn::error::cancelled if cancel fires first. */
            peeler::future<bool> wait(peel::RefPtr<peel::Gio::Cancellable> cancel);
        };

        explicit Cooldown(std::chrono::milliseconds gap);

        Ticket enter();
    };

} // namespace mnn
