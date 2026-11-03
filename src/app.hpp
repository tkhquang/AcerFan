/**
 * @file app.hpp
 * @brief The tray application: a hidden window, the tray icon and menu, and the fan service.
 */
#ifndef ACERFAN_APP_HPP
#define ACERFAN_APP_HPP

#include "config.hpp"
#include "fan_service.hpp"
#include "logger.hpp"
#include "tray_icon.hpp"
#include "win_handle.hpp"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AcerFan
{
    /**
     * @brief The tray application: a hidden window, the tray icon and menu, and the fan service.
     */
    class App
    {
    public:
        /** @brief Loads AcerFan.ini and opens AcerFan.log next to `exe`. */
        App(HINSTANCE instance, std::filesystem::path exe);

        /** @brief Calls shutdown(). */
        ~App() noexcept;

        App(const App &) = delete;
        App &operator=(const App &) = delete;
        App(App &&) = delete;
        App &operator=(App &&) = delete;

        /**
         * @brief Creates the window and the tray icon, starts the fan service and runs the message loop.
         * @return The exit code of the message loop.
         */
        [[nodiscard]] int run();

    private:
        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
        LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

        void show_menu(POINT at);
        void on_command(UINT id);
        void refresh_tray();
        void set_mode(UserMode mode);
        void reload_settings();
        void log_warnings(const std::vector<std::string> &warnings);
        void toggle_autostart();
        void shutdown() noexcept;

        HINSTANCE m_instance{nullptr};
        std::filesystem::path m_exe;
        std::filesystem::path m_ini;
        Logger m_log;
        AppConfig m_config;
        HWND m_window{nullptr};
        UINT m_taskbar_created{0};
        std::unique_ptr<TrayIcon> m_tray;
        std::unique_ptr<FanService> m_service;
        UniqueIcon m_icon;
        // Text and color of m_icon. refresh_tray() draws a new icon only when they change.
        std::wstring m_icon_key;
    };
} // namespace AcerFan

#endif // ACERFAN_APP_HPP
