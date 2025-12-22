/*
    monday-night-net: An amateur radio net monitoring utility in gtk4
    Copyright (C) 2025  Andrew Potter

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

#include <print>
#include <iostream>
#include <peel/Adw/Adw.h>
#include "mnn.hpp"
#include "mnn_application.hpp"
#include "mnn_application_window.hpp"
#include "mnn_callsign_list_view_cell.hpp"
#include "station.hpp"
#include "config.hpp"

#if MNN_HAS_BACKTRACE
#include <csignal>
#include <unistd.h>
#include <execinfo.h>
#endif

#if MNN_HAS_STACKTRACE
#include <stacktrace>
#endif

extern "C" {
#include "mnn_resources.h"
}

namespace
{
    #if MNN_HAS_BACKTRACE
    /* Ensures backtrace() symbol is loaded so dynamic lookup isn't performed in
     * the signal handler. */
    void init_handlers()
    {
        int pipefd[2];
        if (auto res = pipe(pipefd); -1 == res) {
            std::println(std::cerr, "Error: Couldn't open pipe: {}", res);
            return;
        }

        void *preload_buffer[2];
        int frames = backtrace(preload_buffer, 2);

        if (frames > 0) {
            backtrace_symbols_fd(preload_buffer, frames, pipefd[1]);
        }

        close(pipefd[0]);
        close(pipefd[1]);
    }

    extern "C" void sigsegv_handler(int sig)
    {
        constexpr char msg[] = "\n------- Segmentation Fault (SIGSEGV) -------\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);

        constexpr auto max_frames = 100UZ;
        void *callstack[max_frames];
        int frames = backtrace(callstack, max_frames);

        if (frames > 0) {
            constexpr char trace_msg[] = "--------- Stack Trace (Raw Output) ---------\n";
            write(STDERR_FILENO, trace_msg, sizeof(trace_msg) - 1);

            backtrace_symbols_fd(callstack, frames, STDERR_FILENO);

            constexpr char end_msg[]   = "--------------------------------------------\n";
            write(STDERR_FILENO, end_msg, sizeof(end_msg) - 1);
        } else {
            const char fail_msg[] = "Failed to generate backtrace\n";
            write(STDERR_FILENO, fail_msg, sizeof(fail_msg) - 1);
        }
        std::println(std::cerr, "Sorry for the inconvenience! This is certainly a programming error. Please report these messages as an issue at {}", REPO_ISSUES_LINK);
        std::abort();
    }
    #endif

    void terminate_handler() noexcept
    {
        auto eptr = std::current_exception();
        if (eptr) {
            try {
                std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                std::println(std::cerr, "FATAL ERROR: Uncaught exception: {}", e.what());
            } catch (...) {
                std::println(std::cerr, "FATAL ERROR: Uncaught exception, but its not std::exception");
            }
        } else {
            std::println(std::cerr, "FATAL ERROR: std::terminate called (no exception)");
        }
        #if MNN_HAS_STACKTRACE
        std::println(std::cerr,  R"(--------------- Stack Trace ----------------
{}--------------------------------------------)",
                     std::stacktrace::current());
        #elif MNN_HAS_BACKTRACE
        constexpr auto max_frames = 100UZ;
        void *callstack[max_frames];
        int frames = backtrace(callstack, max_frames);
        std::unique_ptr<char*, decltype([](char **ptr) static { free(ptr); })> syms {backtrace_symbols(callstack, frames)};
        if (syms) {
            for (auto const [index, sym] : std::views::enumerate(std::span{ syms.get(), syms.get() + frames })) {
                std::println(std::cerr, "{:3}# {}", index, sym);
            }
        }
        #endif
        std::println(std::cerr, "Sorry for the inconvenience! This is likely a programming error. Please report these messages as an issue at {}", REPO_ISSUES_LINK);
        std::abort();
    }

} // anonymous namespace

namespace mnn
{
    using namespace peel;

    void
    init()
    {
        #if MNN_HAS_BACKTRACE
        init_handlers();
        std::signal(SIGSEGV, sigsegv_handler);
        #endif
        std::set_terminate(terminate_handler);

        Adw::init();
        Type::of<mnn::Application>().ensure();
        Type::of<mnn::ApplicationWindow>().ensure();
        Type::of<mnn::Station>().ensure();
        Type::of<mnn::CallsignListViewCell>().ensure();
    }

    nlohmann::json
    get_default_net()
    {
        auto net_resource = reinterpret_cast<Gio::Resource*>(mnn_resources_get_resource());
        auto net_bytes    = net_resource->lookup_data("/radio/ki6kvz/MondayNightNet/default-net.json",
                                                      Gio::Resource::LookupFlags::NONE,
                                                      nullptr);
        return nlohmann::json::parse(net_bytes->get_data());
    }

} // namespace mnn
