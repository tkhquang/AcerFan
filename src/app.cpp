#include "app.hpp"

#include "autostart.hpp"
#include "com_util.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace AcerFan
{
    namespace
    {
        constexpr wchar_t window_class[] = L"AcerFanTrayWindow";
        constexpr UINT msg_tray = WM_APP + 1;
        constexpr UINT msg_status = WM_APP + 2;

        enum MenuId : UINT
        {
            id_mode_curve = 100,
            id_mode_auto,
            id_mode_full,
            id_autostart,
            id_edit_settings,
            id_reload_settings,
            id_open_log,
            id_exit,
        };

        constexpr COLORREF colour_idle = RGB(64, 64, 64);
        constexpr COLORREF colour_low = RGB(46, 125, 50);
        constexpr COLORREF colour_mid = RGB(239, 108, 0);
        constexpr COLORREF colour_high = RGB(198, 40, 40);
        constexpr COLORREF colour_text = RGB(255, 255, 255);
        constexpr int mid_percent = 60;
        constexpr int high_percent = 85;
        constexpr int max_display_c = 99;

        std::wstring mode_label(UserMode mode)
        {
            switch (mode)
            {
            case UserMode::firmware_auto:
                return L"Firmware Auto";
            case UserMode::full_speed:
                return L"Full speed";
            case UserMode::curve:
                break;
            }
            return L"Temperature curve";
        }

        std::wstring fan_text(const FanStatus &status)
        {
            const std::wstring speed = status.command.mode == FanMode::custom
                                           ? std::format(L"{}%", status.command.percent)
                                           : std::wstring{L"firmware Auto"};
            if (status.cpu_rpm && status.gpu_rpm)
            {
                return std::format(L"Fans {} · {} / {} rpm", speed, *status.cpu_rpm, *status.gpu_rpm);
            }
            return std::format(L"Fans {}", speed);
        }

        std::wstring temperature_text(const FanStatus &status)
        {
            if (!status.cpu_c || !status.gpu_c)
            {
                return L"Temperatures unavailable";
            }
            return std::format(
                L"CPU {} °C (baseline {}) · GPU {} °C (baseline {})",
                *status.cpu_c,
                status.cpu_base,
                *status.gpu_c,
                status.gpu_base
            );
        }

        void open_file(const std::filesystem::path &file)
        {
            ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } // namespace

    App::App(HINSTANCE instance, std::filesystem::path exe)
        : m_instance(instance),
          m_exe(std::move(exe)),
          m_ini(m_exe.parent_path() / L"AcerFan.ini"),
          m_log(m_exe.parent_path() / L"AcerFan.log")
    {
        std::vector<std::string> warnings;
        m_config = load_config(m_ini, warnings);
        for (const std::string &warning : warnings)
        {
            m_log.write("settings: " + warning);
        }
    }

    App::~App()
    {
        shutdown();
    }

    int App::run()
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &App::window_proc;
        wc.hInstance = m_instance;
        wc.lpszClassName = window_class;
        RegisterClassExW(&wc);

        // A hidden top-level window rather than a message-only one: those miss TaskbarCreated and power broadcasts.
        m_window = CreateWindowExW(0, window_class, L"AcerFan", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                   m_instance, this);
        if (m_window == nullptr)
        {
            m_log.write("could not create the tray window");
            return 1;
        }
        m_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        m_tray = std::make_unique<TrayIcon>(m_window, msg_tray);
        refresh_tray();

        m_service = std::make_unique<FanService>(m_log, m_config, [window = m_window] {
            PostMessageW(window, msg_status, 0, 0);
        });

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

    LRESULT CALLBACK App::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (app != nullptr && app->m_window == window)
        {
            return app->handle(message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT App::handle(UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == m_taskbar_created && m_taskbar_created != 0)
        {
            if (m_tray)
            {
                m_tray->recreate();
                refresh_tray();
            }
            return 0;
        }

        switch (message)
        {
        case msg_tray:
            switch (LOWORD(lparam))
            {
            case WM_CONTEXTMENU:
            case NIN_SELECT:
            case NIN_KEYSELECT:
                show_menu({GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)});
                break;
            default:
                break;
            }
            return 0;

        case msg_status:
            refresh_tray();
            return 0;

        case WM_COMMAND:
            on_command(LOWORD(wparam));
            return 0;

        case WM_POWERBROADCAST:
            if (wparam == PBT_APMRESUMEAUTOMATIC && m_service)
            {
                m_service->resumed();
            }
            return TRUE;

        case WM_ENDSESSION:
            if (wparam != FALSE)
            {
                shutdown(); // Windows ends the process after this returns; hand the fans back first.
            }
            return 0;

        case WM_DESTROY:
            shutdown();
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }
        return DefWindowProcW(m_window, message, wparam, lparam);
    }

    void App::refresh_tray()
    {
        if (!m_tray)
        {
            return;
        }
        const FanStatus status = m_service ? m_service->status() : FanStatus{};

        std::wstring text = L"--";
        COLORREF colour = colour_idle;
        if (status.connected && status.cpu_c && status.gpu_c)
        {
            text = std::to_wstring(std::clamp(std::max(*status.cpu_c, *status.gpu_c), 0, max_display_c));
            if (status.command.mode == FanMode::custom)
            {
                const int percent = status.command.percent;
                colour = percent >= high_percent ? colour_high : (percent >= mid_percent ? colour_mid : colour_low);
            }
        }
        const std::wstring key = std::format(L"{}|{}", text, static_cast<unsigned long>(colour));
        if (key != m_icon_key || m_icon.get() == nullptr)
        {
            m_icon = render_badge_icon(text, colour, colour_text);
            m_icon_key = key;
        }

        std::wstring tooltip;
        if (!status.connected)
        {
            tooltip = status.error.empty() ? L"AcerFan: starting" : L"AcerFan: " + to_wide(status.error);
        }
        else
        {
            tooltip = std::format(
                L"AcerFan · {}\n{}\n{}",
                mode_label(status.mode),
                status.cpu_c && status.gpu_c ? std::format(L"CPU {} °C · GPU {} °C", *status.cpu_c, *status.gpu_c)
                                             : std::wstring{L"Temperatures unavailable"},
                fan_text(status)
            );
            if (status.failsafe)
            {
                tooltip += L"\nFail-safe: temperatures unreadable";
            }
        }
        m_tray->update(m_icon.get(), tooltip);
    }

    void App::show_menu(POINT at)
    {
        const FanStatus status = m_service ? m_service->status() : FanStatus{};
        HMENU menu = CreatePopupMenu();
        const auto add_info = [menu](const std::wstring &text) {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, text.c_str());
        };

        if (!status.connected)
        {
            add_info(status.error.empty() ? L"Connecting to the firmware..." : L"Not connected: " + to_wide(status.error));
        }
        else
        {
            add_info(temperature_text(status));
            add_info(fan_text(status) + (status.failsafe ? L" · fail-safe" : L""));
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        const struct
        {
            UINT id;
            UserMode mode;
        } modes[] = {
            {id_mode_curve, UserMode::curve},
            {id_mode_auto, UserMode::firmware_auto},
            {id_mode_full, UserMode::full_speed},
        };
        for (const auto &entry : modes)
        {
            AppendMenuW(menu, MF_STRING, entry.id, mode_label(entry.mode).c_str());
        }
        CheckMenuRadioItem(
            menu,
            id_mode_curve,
            id_mode_full,
            m_config.mode == UserMode::firmware_auto ? id_mode_auto
                                                     : (m_config.mode == UserMode::full_speed ? id_mode_full
                                                                                              : id_mode_curve),
            MF_BYCOMMAND
        );
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(
            menu,
            MF_STRING | (autostart::is_enabled(m_exe) ? MF_CHECKED : MF_UNCHECKED),
            id_autostart,
            L"Start with Windows"
        );
        AppendMenuW(menu, MF_STRING, id_edit_settings, L"Edit settings");
        AppendMenuW(menu, MF_STRING, id_reload_settings, L"Reload settings");
        AppendMenuW(menu, MF_STRING, id_open_log, L"Open log");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, id_exit, L"Exit (fans back to firmware Auto)");

        // Required for the menu to close when the user clicks elsewhere.
        SetForegroundWindow(m_window);
        TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, at.x, at.y, m_window, nullptr);
        PostMessageW(m_window, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }

    void App::on_command(UINT id)
    {
        switch (id)
        {
        case id_mode_curve:
            set_mode(UserMode::curve);
            break;
        case id_mode_auto:
            set_mode(UserMode::firmware_auto);
            break;
        case id_mode_full:
            set_mode(UserMode::full_speed);
            break;
        case id_autostart:
            toggle_autostart();
            break;
        case id_edit_settings:
            open_file(m_ini);
            break;
        case id_reload_settings:
            reload_settings();
            break;
        case id_open_log:
            open_file(m_log.file());
            break;
        case id_exit:
            DestroyWindow(m_window);
            break;
        default:
            break;
        }
    }

    void App::set_mode(UserMode mode)
    {
        m_config.mode = mode;
        save_mode(m_ini, mode);
        if (m_service)
        {
            m_service->set_mode(mode);
        }
    }

    void App::reload_settings()
    {
        std::vector<std::string> warnings;
        AppConfig config = load_config(m_ini, warnings);
        for (const std::string &warning : warnings)
        {
            m_log.write("settings: " + warning);
        }
        m_config = config;
        if (m_service)
        {
            m_service->reload(std::move(config));
        }
        if (!warnings.empty())
        {
            const std::wstring text = std::format(
                L"{} setting(s) were invalid and use their defaults. Details are in AcerFan.log.",
                warnings.size()
            );
            MessageBoxW(m_window, text.c_str(), L"AcerFan", MB_OK | MB_ICONWARNING);
        }
    }

    void App::toggle_autostart()
    {
        try
        {
            if (autostart::is_enabled(m_exe))
            {
                autostart::disable();
                m_log.write("start with Windows: off");
            }
            else
            {
                autostart::enable(m_exe);
                m_log.write("start with Windows: on");
            }
        }
        catch (const ComError &error)
        {
            m_log.write(std::format("start with Windows failed: {}", error.what()));
            MessageBoxW(m_window, to_wide(error.what()).c_str(), L"AcerFan", MB_OK | MB_ICONERROR);
        }
    }

    void App::shutdown()
    {
        if (m_service)
        {
            m_service->stop();
            m_service.reset();
        }
        m_tray.reset();
    }
} // namespace AcerFan
