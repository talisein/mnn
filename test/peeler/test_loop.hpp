// SPDX-FileCopyrightText: 2026 Andrew Potter
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/* Shared test-loop helpers for driving async work to completion under a GLib
 * main loop.  Include this instead of defining local variants per test file.
 *
 * peeler::run_in_fiber(fn) — spawn fn in an 8 MiB Dex fiber and block in a
 * main loop until the fiber completes.  Use for gom/sqlite DB tests: sqlite
 * can recurse deeply (gom's own testsuite uses 8 MiB) and requires a fiber
 * stack that the app's coroutines no longer provide.
 *
 * peeler::drive(fn) — schedule captureless fn from an idle source and block
 * in a main loop until fn calls loop->quit().  Use for coroutine tests where
 * fn is a coro::SimpleTask or peeler::future lambda.
 * fn MUST be captureless — a coroutine lambda's captures dangle past the
 * first suspension point.  Starting on idle (after loop->run() is live)
 * prevents synchronous fast-path cases from quitting before run() enters.
 */

#include "database.hpp"
#include "peeler/coro.hpp"
#include "util.hpp"

#include <peel/Dex/Dex.h>
#include <peel/Dex/Future.h>
#include <peel/GLib/Dir.h>
#include <peel/GLib/Error.h>
#include <peel/GLib/GLib.h>
#include <peel/GLib/MainLoop.h>
#include <peel/GLib/functions.h>
#include <peel/Gom/Driver.h>
#include <peel/Gom/Repository.h>
#include <peel/RefPtr.h>
#include <peel/UniquePtr.h>
#include <boost/ut.hpp>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace peeler {

/* Install a GLib structured-log writer that fails the current boost.ut test
 * on any WARNING or CRITICAL message.  g_log_writer_default() is called first
 * so the message still appears on stderr with its normal domain/level prefix.
 * ERROR is already always-fatal in GLib (g_log_writer_default aborts); we do
 * not need to handle it here.  Call once at the top of each test main(). */
inline void
install_test_log_handler()
{
    g_log_set_writer_func(
        [](GLogLevelFlags log_level, const GLogField *fields, gsize n_fields,
           gpointer) -> GLogWriterOutput {
            g_log_writer_default(log_level, fields, n_fields, nullptr);
            if (log_level & (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL)) {
                std::string_view msg;
                for (gsize i = 0; i < n_fields; ++i) {
                    if (std::string_view{fields[i].key} == "MESSAGE") {
                        msg = static_cast<const char *>(fields[i].value);
                        break;
                    }
                }
                using namespace boost::ut;
                expect(false) << std::format("GLib {}: {}",
                    (log_level & G_LOG_LEVEL_CRITICAL) ? "CRITICAL" : "WARNING",
                    msg);
            }
            return G_LOG_WRITER_HANDLED;
        },
        nullptr, nullptr);
}

constexpr std::size_t db_fiber_stack_size = 8 * 1024 * 1024;

template<typename F>
void
run_in_fiber(F &&fn)
{
    auto loop = peel::GLib::MainLoop::create(nullptr, false);
    auto done = peel::Dex::Future::finally_(
        peel::Dex::Scheduler::get_default()->spawn(
            db_fiber_stack_size, std::forward<F>(fn)),
        [&loop](peel::Dex::Future *) -> peel::RefPtr<peel::Dex::Future> {
            loop->quit();
            return nullptr;
        });
    loop->run();
}

template<typename Fn>
void
drive(Fn&& fn)
{
    auto loop = peel::GLib::MainLoop::create(nullptr, false);
    peel::GLib::MainLoop *loop_ptr = loop;
    peel::GLib::idle_add_once([fn = std::forward<Fn>(fn), loop_ptr] { fn(loop_ptr); });
    loop->run();
}

/* peeler::run_coro(fn) — call fn immediately as a peeler::future<T> coroutine
 * and block until it completes.  Unlike drive(), fn is called before loop->run()
 * so [&] captures of outer-scope variables are safe: those stack frames remain
 * live while loop->run() blocks.  fn is taken by value so its closure lives on
 * this stack frame and the coroutine's implicit this pointer stays valid.
 *
 * fn must return peeler::future<T> (not coro::SimpleTask, which carries no
 * completion signal and would require explicit loop->quit() inside). */
template<typename F>
void
run_coro(F fn)
{
    auto loop = peel::GLib::MainLoop::create(nullptr, false);
    peel::GLib::MainLoop *loop_ptr = loop;
    auto done = peel::Dex::Future::finally_(
        peel::RefPtr<peel::Dex::Future>(fn(loop_ptr)),
        [loop_ptr](peel::Dex::Future *) -> peel::RefPtr<peel::Dex::Future> {
            loop_ptr->quit();
            return {};
        });
    loop->run();
}

/* RAII throwaway on-disk sqlite DB directory: created by make_tmp_db_{path,uri},
 * removed when the object goes out of scope, so tests never leak /tmp dirs.
 * Drop-in for the std::string the old per-file make_tmp_db_{path,uri} helpers
 * returned: c_str() and the implicit std::string conversion yield the file path
 * or the file:// URI (whichever factory made it). Declare it at test scope so it
 * outlives the run_coro/run_in_fiber that opens the DB. */
class ScopedTmpDb {
public:
    ScopedTmpDb() = default;
    ScopedTmpDb(std::string dir, std::string path, std::string uri, bool want_uri)
        : dir_(std::move(dir)), path_(std::move(path)), uri_(std::move(uri)),
          want_uri_(want_uri)
    {
    }
    ScopedTmpDb(ScopedTmpDb &&o) noexcept { *this = std::move(o); }
    ScopedTmpDb &operator=(ScopedTmpDb &&o) noexcept
    {
        remove();
        dir_ = std::move(o.dir_);
        path_ = std::move(o.path_);
        uri_ = std::move(o.uri_);
        want_uri_ = o.want_uri_;
        o.dir_.clear();
        return *this;
    }
    ScopedTmpDb(const ScopedTmpDb &) = delete;
    ScopedTmpDb &operator=(const ScopedTmpDb &) = delete;
    ~ScopedTmpDb() { remove(); }

    bool               ok() const { return !dir_.empty(); }
    const std::string &dir() const { return dir_; }
    const std::string &path() const { return path_; }
    const std::string &uri() const { return uri_; }
    /* The value the old helpers returned: path for make_tmp_db_path(), URI for
     * make_tmp_db_uri(). */
    const std::string &value() const { return want_uri_ ? uri_ : path_; }
    const char        *c_str() const { return value().c_str(); }
    operator const std::string &() const { return value(); }

private:
    void remove()
    {
        if (dir_.empty())
            return;
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(dir_), ec);
        dir_.clear();
    }
    std::string dir_, path_, uri_;
    bool        want_uri_ = false;
};

inline ScopedTmpDb
make_scoped_tmp_db(bool want_uri)
{
    peel::UniquePtr<peel::GLib::Error> error;
    auto d = peel::GLib::Dir::make_tmp(nullptr, &error);
    boost::ut::expect(d != nullptr) << "make_tmp_db: make_tmp failed";
    if (!d)
        return {};
    std::string dir  = std::string(::to_sview(d));
    auto        path = std::format("{}/test.db", dir);
    auto        uri  = peel::GLib::filename_to_uri(path.c_str(), nullptr, &error);
    return ScopedTmpDb{ std::move(dir), std::move(path),
                        std::string(::to_sview(uri)), want_uri };
}

/* Drop-in replacements for the per-file helpers of the same names -- now RAII,
 * so the temp dir is removed when the returned object leaves scope. */
inline ScopedTmpDb make_tmp_db_path() { return make_scoped_tmp_db(false); }
inline ScopedTmpDb make_tmp_db_uri()  { return make_scoped_tmp_db(true); }

/* run_coro against a throwaway on-disk sqlite DB, cleaned up afterward so tests
 * never leak /tmp dirs.  fn(MainLoop*, const std::string& uri): fn opens (and
 * may re-open, e.g. restart/migration tests) the DB itself via the URI. */
template<typename F>
void
run_coro_with_db(F fn)
{
    ScopedTmpDb db = make_tmp_db_uri();
    if (db.ok())
        run_coro([&](peel::GLib::MainLoop *loop) -> future<bool> {
            return fn(loop, db.uri());
        });
    /* db's destructor removes the temp dir. */
}

/* Like run_coro_with_db, but opens the repository before the loop and hands fn a
 * live, non-null RefPtr<Gom::Repository> (a repo-open failure fails the test
 * here, before fn's loop starts).  fn(MainLoop*, RefPtr<Gom::Repository>). */
template<typename F>
void
run_coro_with_repo(F fn)
{
    using namespace peel;
    /* db first so it is destroyed *after* repo (declared next): repo must drop
     * -- closing sqlite -- before the temp dir is removed. */
    ScopedTmpDb             db = make_tmp_db_uri();
    RefPtr<Gom::Repository> repo;
    if (db.ok()) {
        UniquePtr<GLib::Error> error;
        run_in_fiber([&]() -> RefPtr<Dex::Future> {
            auto driver = Gom::Driver::open(db.uri().c_str(), &error);
            if (!driver)
                return nullptr;
            repo = Dex::Future::await_object(mnn::create_repository(driver), &error)
                       .cast<Gom::Repository>();
            return nullptr;
        });
        boost::ut::expect(repo != nullptr)
            << "run_coro_with_repo: repo open failed: "
            << (error ? ::to_sview(error->message) : std::string_view{ "unknown" });
        if (repo)
            run_coro([&](GLib::MainLoop *loop) -> future<bool> {
                return fn(loop, repo);
            });
    }
}

} /* namespace peeler */
