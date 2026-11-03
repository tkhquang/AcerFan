/**
 * @file app.hpp
 * @brief The tray application: hidden window, tray icon, menu and the fan service.
 */
#ifndef ACERFAN_APP_HPP
#define ACERFAN_APP_HPP

#include "config.hpp"
#include "fan_service.hpp"
#include "logger.hpp"
#include "tray_icon.hpp"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>

namespace AcerFan
{
    class App
    {
    public:
        App(HINSTANCE instance, std::filesystem::path exe);
        ~App();

        App(const App &) = delete;
        App &operator=(const App &) = delete;
        App(App &&) = delete;
        App &operator=(App &&) = delete;

        /// Creates the window and tray icon, starts the fan service and runs the message loop.
        int run();

    private:
        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
        LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

        void show_menu(POINT at);
        void on_command(UINT id);
        void refresh_tray();
        void set_mode(UserMode mode);
        void reload_settings();
        void toggle_autostart();
        void shutdown();

        HINSTANCE m_instance;
        std::filesystem::path m_exe;
        std::filesystem::path m_ini;
        Logger m_log;
        AppConfig m_config;
        HWND m_window{nullptr};
        UINT m_taskbar_created{0};
        std::unique_ptr<TrayIcon> m_tray;
        std::unique_ptr<FanService> m_service;
        UniqueIcon m_icon;
        std::wstring m_icon_key; ///< Text and colour of m_icon, to redraw only on change.
    };
} // namespace AcerFan

#endif // ACERFAN_APP_HPP
