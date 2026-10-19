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
#include "peeler/props.hpp"
#include <functional>
#include <variant>
#include <peel/GLib/Bytes.h>
#include <peel/Gio/Cancellable.h>
#include <peel/GObject/Object.h>
#include <peel/Soup/Message.h>
#include <peel/Soup/Session.h>
#include <peel/Soup/Status.h>
#include <peel/RefPtr.h>
#include <peel/class.h>

namespace mnn
{
    /* Status and body of one HTTP exchange. libsoup 3 has no public way to set
     * a Soup::Message's status, so the status travels alongside the body and
     * test mocks can fake it. */
    struct HttpReply
    {
        peel::Soup::Status status = peel::Soup::Status::NONE;
        peel::RefPtr<peel::GLib::Bytes> body;
    };
}

PEELER_BOXED_TYPE(mnn::HttpReply, "mnn-HttpReply")

namespace mnn
{
    /* Injectable mock function type for tests. */
    using HttpMockFn = std::move_only_function<
        peeler::future<HttpReply>(peel::RefPtr<peel::Soup::Message>)>;

    /* HTTP transport passed around as a RefPtr. Slimmed from milfeulle's
     * HttpClient (no request logging).
     *
     * Production instances are created with create(): a Soup::Session with
     * mnn's User-Agent is owned internally. Test instances use
     * create_for_test(fn): fn is called instead of real HTTP. */
    class HttpClient final : public peel::GObject::Object
    {
        PEEL_SIMPLE_CLASS(HttpClient, peel::GObject::Object);

        struct Members {
            std::variant<peel::RefPtr<peel::Soup::Session>, HttpMockFn> transport;
        } m;

        void init(Class *) { new (&m) Members; }
        void vfunc_dispose();

    public:
        static peel::RefPtr<HttpClient> create();
        static peel::RefPtr<HttpClient> create_for_test(HttpMockFn mock_fn);

        /* Resolves with the reply (any HTTP status) or rejects with the
         * transport's GLib::Error. Does not interpret the status. */
        peeler::future<HttpReply>
        send_and_read(peel::RefPtr<peel::Soup::Message> msg,
                      peel::RefPtr<peel::Gio::Cancellable> cancel = {});
    };

} // namespace mnn
