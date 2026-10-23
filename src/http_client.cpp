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

#include "http_client.hpp"
#include "config.hpp"
#include <format>
#include <string>
#include <peel/Dex/Promise.h>
#include <peel/GLib/Error.h>
#include <peel/GObject/Type.h>
#include <peel/Gio/AsyncResult.h>

PEEL_CLASS_IMPL(mnn::HttpClient, "MnnHttpClient", peel::GObject::Object)

namespace mnn
{
    using namespace peel;

    /* Sends the message and resolves to the response body, or rejects with a
     * GLib::Error. */
    static peeler::future<GLib::Bytes>
    soup_send_and_read(RefPtr<Soup::Session> session, RefPtr<Soup::Message> msg,
                       RefPtr<Gio::Cancellable> cancel)
    {
        auto promise = Dex::Promise::create();

        session->send_and_read_async(msg, G_PRIORITY_DEFAULT, cancel,
            [promise, session](peel::GObject::Object *, Gio::AsyncResult *result) {
                UniquePtr<GLib::Error> err;
                auto bytes = session->send_and_read_finish(result, &err);
                if (err) {
                    promise->reject(std::move(err));
                    return;
                }
                promise->resolve_boxed(peel::GObject::Type::of<GLib::Bytes>(), std::move(bytes).release_ref());
            });

        return peeler::into_future<GLib::Bytes>(promise);
    }

    void
    HttpClient::Class::init()
    {
        override_vfunc_dispose<HttpClient>();
    }

    void
    HttpClient::vfunc_dispose()
    {
        m.transport = {};
        parent_vfunc_dispose<HttpClient>();
    }

    RefPtr<HttpClient>
    HttpClient::create()
    {
        auto self = peel::GObject::Object::create<HttpClient>();
        auto session = Soup::Session::create();
        auto user_agent = std::format("monday-night-net/{} (+{})", PROJECT_VERSION, REPO_ISSUES_LINK);
        session->set_user_agent(user_agent.c_str());
        self->m.transport = std::move(session);
        return self;
    }

    RefPtr<HttpClient>
    HttpClient::create_for_test(HttpMockFn mock_fn)
    {
        auto self = peel::GObject::Object::create<HttpClient>();
        self->m.transport = std::move(mock_fn);
        return self;
    }

    peeler::future<HttpReply>
    HttpClient::send_and_read(RefPtr<Soup::Message> msg, RefPtr<Gio::Cancellable> cancel)
    {
        RefPtr<HttpClient> keep { this };

        if (auto *sp = std::get_if<RefPtr<Soup::Session>>(&m.transport)) {
            auto body = co_await soup_send_and_read(*sp, msg, std::move(cancel));
            if (!body)
                co_return std::unexpected(std::move(body.error()));
            co_return HttpReply { msg->get_status(), std::move(*body) };
        } else {
            auto &fn = std::get<HttpMockFn>(m.transport);
            auto r   = co_await fn(std::move(msg));
            if (!r)
                co_return std::unexpected(std::move(r.error()));
            co_return std::move(*r);
        }
    }

} // namespace mnn
