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

        if (auto res = pipe(pipefd); res == -1) {
            std::println(std::cerr, "Error: Couldn't open pipe");
            return;
        }

        void *preload_buffer[2];
        int frames = backtrace(preload_buffer, 2);

        // Use the /dev/null descriptor to silently discard the output
        if (frames > 0) {
            backtrace_symbols_fd(preload_buffer, frames, pipefd[1]);
        }

        close(pipefd[0]);
        close(pipefd[1]);
    }

    extern "C" void sigsegv_handler(int sig)
    {
        const char msg[] = "\n--- Segmentation Fault (SIGSEGV) Detected ---\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);

        const int max_frames = 100;
        void *callstack[max_frames];
        int frames = 0;

        frames = backtrace(callstack, max_frames);

        const char trace_msg[] = "--- Stack Trace (Raw Output) ---\n";
        write(STDERR_FILENO, trace_msg, sizeof(trace_msg) - 1);

        backtrace_symbols_fd(callstack, frames, STDERR_FILENO);

        const char end_msg[] = "--------------------------------------------\n";
        write(STDERR_FILENO, end_msg, sizeof(end_msg) - 1);

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
                std::println(std::cerr, "Uncaught exception: {}", e.what());
            } catch (...) {
                std::println(std::cerr, "Uncaught exception, but its not std::exception");
            }
        } else {
            std::println(std::cerr, "std::terminate called (no exception)");
        }
        #if MNN_HAS_STACKTRACE
        std::println(std::cerr,  R"(--------------- Stack Trace ----------------
{}
--------------------------------------------)",
                     std::stacktrace::current());
        #endif
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
