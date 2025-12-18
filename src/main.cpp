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

#include <clocale>
#include <format>
#include <print>
#include <iostream>
#include <glib/gi18n.h>
#include "mnn.hpp"
#include "mnn_application.hpp"
#include "config.hpp"

#if MNN_HAS_CORE_FOUNDATION
#include <CoreFoundation/CoreFoundation.h>
#endif

using namespace peel;

namespace
{
    #if MNN_HAS_CORE_FOUNDATION
    std::locale get_mac_locale() noexcept
    {
        std::unique_ptr<CFLocaleRef, decltype([](auto ref) { CFRelease(ref); })> cf_locale = CFLocaleCopyCurrent();
        if (!cf_locale) return std::locale::classic();

        auto cf_id = CFLocaleGetIdentifier(cf_locale);
        if (!cf_id) return std::locale::classic();

        const char* fast_ptr = CFStringGetCStringPtr(cf_id, kCFStringEncodingUTF8);
        std::string locale_name;
        if (fast_ptr) {
            locale_name = fast_ptr;
        } else {
            // Fallback: Manually copy the string into a buffer
            CFIndex length = CFStringGetLength(cf_id);
            CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
            locale_name.resize_and_overwrite(max_size, [&cf_id, &length](char* buf, std::size_t buf_size) -> std::size_t {
                CFIndex bytes_used = 0;
                CFRange range_all = CFRangeMake(0, length);
                CFStringGetBytes(cf_id,
                                 range_all,
                                 kCFStringEncodingUTF8,
                                 0, false,
                                 reinterpret_cast<std::uint8_t*>(buf),
                                 static_cast<CFIndex>(buf_size),
                                 &bytes_used);
                return bytes_used;
            });
            if (CFStringGetCString(cf_id, buffer.data(), max_size, kCFStringEncodingUTF8)) {
                locale_name = buffer.data();
            }
        }

        try {
            return std::locale(locale_name);
        } catch (const std::exception& e) {
            try {
                return std::locale(locale_name + ".UTF-8");
            } catch (const std::exception& e) {
                g_warning("Failed setting locale to user-preferred \"%s\" and \"%s.UTF-8\": %s",
                          locale_name.c_str(), locale_name.c_str(), e.what());
            }
        }
        return std::locale::classic();
    }

    #else

    std::locale get_nonmac_locale() noexcept
    {
        auto cstr_locale = std::setlocale(LC_ALL, "");
        if (!cstr_locale) {
            g_warning("Unable to std::setlocale() to the user-preferred locale");
            return std::locale::classic();
        }
        try {
            return std::locale(cstr_locale);
        } catch (const std::exception& e) {
            g_warning("Failed setting locale to user-preferred \"%s\": %s", cstr_locale, e.what());
        }
        return std::locale::classic();
    }
    #endif

    void manage_locale() noexcept
    {
        #if MNN_HAS_CORE_FOUNDATION
        auto locale = get_mac_locale();
        auto locale_str = locale.name();
        if (locale_str.find('.') == std::string::npos) {
            locale_str += ".UTF-8";
        }
        g_setenv("LANG", locale_str.c_str(), TRUE);
        #else
        auto locale = get_nonmac_locale();
        #endif
        try {
            g_debug("Setting locale to user-preferred \"%s\"", locale.name().c_str());
            std::locale::global(locale);
            std::cout.imbue(locale);
            std::clog.imbue(locale);
        } catch (const std::exception& e) {
            g_warning("Failed setting locale to user-preferred \"%s\": %s", locale.name().c_str(), e.what());
        }
    }
} // Anonymous Namespace

int
main (int argc, char *argv[])
{
    manage_locale();
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    mnn::init();
    GLib::set_application_name(_("Monday Night Net"));
    RefPtr<mnn::Application> app = mnn::Application::create();
    return app->run (argc, argv);
}
