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

#include "cooldown.hpp"
#include "mnn_gerror.hpp"
#include <algorithm>
#include <utility>
#include <peel/Dex/Cancellable.h>
#include <peel/GLib/functions.h>

using namespace peel;

namespace mnn
{
    void
    Cooldown::State::hand_off()
    {
        busy = false;
        if (queue.empty())
            return;
        auto next = std::move(queue.front());
        queue.pop_front();
        busy = true;
        next->resolve_boolean(true);
    }

    Cooldown::Cooldown(std::chrono::milliseconds gap)
        : state(std::make_shared<State>(State { .gap = gap }))
    {
    }

    Cooldown::Ticket
    Cooldown::enter()
    {
        if (!state->busy && state->queue.empty()) {
            state->busy = true;
            return Ticket(state, Dex::Future::create_for_boolean(true), nullptr);
        }
        auto queued = Dex::Promise::create();
        state->queue.push_back(queued);
        return Ticket(state, RefPtr<Dex::Future>(queued), queued);
    }

    Cooldown::Ticket::Ticket(std::shared_ptr<State> s, RefPtr<Dex::Future> go,
                             RefPtr<Dex::Promise> queued) noexcept
        : state(std::move(s)), go(std::move(go)), queued(std::move(queued))
    {
    }

    Cooldown::Ticket::~Ticket()
    {
        if (go->is_resolved()) {
            /* Our turn. If a request went out, the next one waits the gap
             * from now; otherwise pass the turn straight on. The timer keeps
             * the state alive even if the Cooldown's owner is gone. */
            if (used)
                GLib::timeout_add_once(static_cast<unsigned>(state->gap.count()),
                                       [s = state] { s->hand_off(); });
            else
                state->hand_off();
        } else {
            /* Still in line: leave it. */
            std::erase(state->queue, queued);
        }
    }

    peeler::future<bool>
    Cooldown::Ticket::wait(RefPtr<Gio::Cancellable> cancel)
    {
        if (!go->is_resolved()) {
            if (cancel) {
                auto cancelled = Dex::Cancellable::create_from_cancellable(cancel);
                Dex::Future *either[] = { go, cancelled };
                co_await Dex::Future::firstv(either);
            } else {
                co_await RefPtr<Dex::Future>(go);
            }
        }

        if (cancel && cancel->is_cancelled()) {
            /* If the turn arrived anyway, ~Ticket passes it on unused. */
            if (!go->is_resolved())
                std::erase(state->queue, queued);
            co_return std::unexpected(make_gerror(error::cancelled));
        }

        used = true;
        co_return true;
    }

} // namespace mnn
