#include "app.hpp"

#include "autostart.hpp"
#include "com_util.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace AcerFan
{
    namespace
    {
        constexpr const wchar_t *WINDOW_CLASS = L"AcerFanTrayWindow";
        constexpr UINT TRAY_MESSAGE = WM_APP + 1;
        constexpr UINT STATUS_MESSAGE = WM_APP + 2;

        constexpr UINT MENU_MODE_CURVE = 100;
        constexpr UINT MENU_MODE_AUTO = 101;
        constexpr UINT MENU_MODE_FULL = 102;
        constexpr UINT MENU_AUTOSTART = 103;
        constexpr UINT MENU_EDIT_SETTINGS = 104;
        constexpr UINT MENU_RELOAD_SETTINGS = 105;
        constexpr UINT MENU_OPEN_LOG = 106;
        constexpr UINT MENU_EXIT = 107;

        constexpr COLORREF BADGE_IDLE = RGB(64, 64, 64);
        constexpr COLORREF BADGE_LOW = RGB(46, 125, 50);
        constexpr COLORREF BADGE_MID = RGB(239, 108, 0);
        constexpr COLORREF BADGE_HIGH = RGB(198, 40, 40);
        constexpr COLORREF BADGE_TEXT = RGB(255, 255, 255);
        constexpr int MID_PERCENT = 60;
        constexpr int HIGH_PERCENT = 85;
        constexpr int MAX_BADGE_C = 99;

        /**
         * @brief A mode entry of the tray menu.
         */
        struct ModeItem
        {
            UINT id{0};
            UserMode mode{UserMode::Curve};
        };

        // The radio group of the menu. CheckMenuRadioItem needs consecutive ids in this order.
        constexpr std::array MODE_ITEMS{
            ModeItem{MENU_MODE_CURVE, UserMode::Curve},
            ModeItem{MENU_MODE_AUTO, UserMode::FirmwareAuto},
            ModeItem{MENU_MODE_FULL, UserMode::FullSpeed},
        };

        UINT menu_id(UserMode mode) noexcept
        {
            const auto found = std::ranges::find(MODE_ITEMS, mode, &ModeItem::mode);
            return found == MODE_ITEMS.end() ? MENU_MODE_CURVE : found->id;
        }

        std::wstring mode_label(UserMode mode)
        {
            switch (mode)
            {
            case UserMode::FirmwareAuto:
                return L"Firmware Auto";
            case UserMode::FullSpeed:
                return L"Full speed";
            case UserMode::Curve:
                break;
            }
            return L"Temperature curve";
        }

        std::wstring fan_text(const FanStatus &status)
        {
            const std::wstring speed = status.command.mode == FanMode::Custom
                                           ? std::format(L"{}%", status.command.percent)
                                           : std::wstring{L"firmware Auto"};
            if (status.cpu_rpm && status.gpu_rpm)
            {
                return std::format(L"Fans {} \u00B7 {} / {} rpm", speed, *status.cpu_rpm, *status.gpu_rpm);
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
                L"CPU {} \u00B0C (baseline {}) \u00B7 GPU {} \u00B0C (baseline {})",
                *status.cpu_c,
                status.cpu_baseline_c,
                *status.gpu_c,
                status.gpu_baseline_c
            );
        }

        COLORREF badge_color(const FanStatus &status) noexcept
        {
            if (status.command.mode != FanMode::Custom)
            {
                return BADGE_IDLE;
            }
            const int percent = status.command.percent;
            if (percent >= HIGH_PERCENT)
            {
                return BADGE_HIGH;
            }
            return percent >= MID_PERCENT ? BADGE_MID : BADGE_LOW;
        }

        void open_file(const std::filesystem::path &file) noexcept
        {
            ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    } // namespace

    App::App(HINSTANCE instance, std::filesystem::path exe)
        : m_instance(instance), m_exe(std::move(exe)), m_ini(m_exe.parent_path() / L"AcerFan.ini"),
          m_log(m_exe.parent_path() / L"AcerFan.log")
    {
        LoadedConfig loaded = load_config(m_ini);
        log_warnings(loaded.warnings);
        m_config = std::move(loaded.config);
    }

    App::~App() noexcept
    {
        shutdown();
    }

    int App::run()
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &App::window_proc;
        window_class.hInstance = m_instance;
        window_class.lpszClassName = WINDOW_CLASS;
        RegisterClassExW(&window_class);

        // A hidden top-level window, because a message-only window misses TaskbarCreated and the power broadcasts.
        m_window =
            CreateWindowExW(0, WINDOW_CLASS, L"AcerFan", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, m_instance, this);
        if (m_window == nullptr)
        {
            m_log.write("could not create the tray window");
            return 1;
        }
        m_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        m_tray = std::make_unique<TrayIcon>(m_window, TRAY_MESSAGE);
        refresh_tray();

        m_service = std::make_unique<FanService>(
            m_log,
            m_config,
            [window = m_window]() noexcept { PostMessageW(window, STATUS_MESSAGE, 0, 0); }
        );

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
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
        case TRAY_MESSAGE:
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

        case STATUS_MESSAGE:
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
                // Windows ends the process after this message returns, so the fans go back to Auto first.
                shutdown();
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
        const bool has_reading = status.connected && status.cpu_c && status.gpu_c;

        const std::wstring text =
            has_reading ? std::to_wstring(std::clamp(std::max(*status.cpu_c, *status.gpu_c), 0, MAX_BADGE_C)) : L"?";
        const COLORREF color = has_reading ? badge_color(status) : BADGE_IDLE;
        const std::wstring key = std::format(L"{}|{}", text, static_cast<unsigned long>(color));
        if (key != m_icon_key || !m_icon)
        {
            m_icon = render_badge_icon(text, {color, BADGE_TEXT});
            m_icon_key = key;
        }

        std::wstring tooltip;
        if (!status.connected)
        {
            tooltip = status.error.empty() ? L"AcerFan: starting" : L"AcerFan: " + to_wide(status.error);
        }
        else
        {
            const std::wstring temperatures =
                status.cpu_c && status.gpu_c
                    ? std::format(L"CPU {} \u00B0C \u00B7 GPU {} \u00B0C", *status.cpu_c, *status.gpu_c)
                    : std::wstring{L"Temperatures unavailable"};
            tooltip =
                std::format(L"AcerFan \u00B7 {}\n{}\n{}", mode_label(status.mode), temperatures, fan_text(status));
            if (status.failsafe)
            {
                tooltip += L"\nFail-safe: temperatures unreadable";
            }
        }
        m_tray->update(m_icon.get(), tooltip);
    }

    void App::show_menu(POINT at)
    {
        const UniqueMenu menu{CreatePopupMenu()};
        if (!menu)
        {
            return;
        }
        HMENU handle = menu.get();
        const auto add_info = [handle](const std::wstring &text) noexcept
        { AppendMenuW(handle, MF_STRING | MF_GRAYED, 0, text.c_str()); };

        const FanStatus status = m_service ? m_service->status() : FanStatus{};
        if (!status.connected)
        {
            add_info(
                status.error.empty() ? L"Connecting to the firmware..." : L"Not connected: " + to_wide(status.error)
            );
        }
        else
        {
            add_info(temperature_text(status));
            add_info(fan_text(status) + (status.failsafe ? L" \u00B7 fail-safe" : L""));
        }
        AppendMenuW(handle, MF_SEPARATOR, 0, nullptr);

        for (const ModeItem &item : MODE_ITEMS)
        {
            AppendMenuW(handle, MF_STRING, item.id, mode_label(item.mode).c_str());
        }
        CheckMenuRadioItem(handle, MODE_ITEMS.front().id, MODE_ITEMS.back().id, menu_id(m_config.mode), MF_BYCOMMAND);
        AppendMenuW(handle, MF_SEPARATOR, 0, nullptr);

        const UINT autostart_state = autostart::is_enabled(m_exe) ? MF_CHECKED : MF_UNCHECKED;
        AppendMenuW(handle, MF_STRING | autostart_state, MENU_AUTOSTART, L"Start with Windows");
        AppendMenuW(handle, MF_STRING, MENU_EDIT_SETTINGS, L"Edit settings");
        AppendMenuW(handle, MF_STRING, MENU_RELOAD_SETTINGS, L"Reload settings");
        AppendMenuW(handle, MF_STRING, MENU_OPEN_LOG, L"Open log");
        AppendMenuW(handle, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(handle, MF_STRING, MENU_EXIT, L"Exit (fans back to firmware Auto)");

        // The menu closes on a click elsewhere only while its window is in the foreground.
        SetForegroundWindow(m_window);
        TrackPopupMenuEx(handle, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, at.x, at.y, m_window, nullptr);
        PostMessageW(m_window, WM_NULL, 0, 0);
    }

    void App::on_command(UINT id)
    {
        if (const auto item = std::ranges::find(MODE_ITEMS, id, &ModeItem::id); item != MODE_ITEMS.end())
        {
            set_mode(item->mode);
            return;
        }
        switch (id)
        {
        case MENU_AUTOSTART:
            toggle_autostart();
            break;
        case MENU_EDIT_SETTINGS:
            open_file(m_ini);
            break;
        case MENU_RELOAD_SETTINGS:
            reload_settings();
            break;
        case MENU_OPEN_LOG:
            open_file(m_log.file());
            break;
        case MENU_EXIT:
            DestroyWindow(m_window);
            break;
        default:
            break;
        }
    }

    void App::set_mode(UserMode mode)
    {
        m_config.mode = mode;
        if (const Result<void> saved = save_mode(m_ini, mode); !saved)
        {
            m_log.write("settings: " + saved.error().message());
        }
        if (m_service)
        {
            m_service->set_mode(mode);
        }
    }

    void App::reload_settings()
    {
        LoadedConfig loaded = load_config(m_ini);
        log_warnings(loaded.warnings);
        m_config = loaded.config;
        if (m_service)
        {
            m_service->reload(std::move(loaded.config));
        }
        if (!loaded.warnings.empty())
        {
            const std::wstring text = std::format(
                L"{} setting(s) were invalid and use their defaults. AcerFan.log has the details.",
                loaded.warnings.size()
            );
            MessageBoxW(m_window, text.c_str(), L"AcerFan", MB_OK | MB_ICONWARNING);
        }
    }

    void App::log_warnings(const std::vector<std::string> &warnings)
    {
        for (const std::string &warning : warnings)
        {
            m_log.write("settings: " + warning);
        }
    }

    void App::toggle_autostart()
    {
        const bool enable = !autostart::is_enabled(m_exe);
        const Result<void> changed = enable ? autostart::enable(m_exe) : autostart::disable();
        if (!changed)
        {
            const std::string message = changed.error().message();
            m_log.write("start with Windows failed: " + message);
            MessageBoxW(m_window, to_wide(message).c_str(), L"AcerFan", MB_OK | MB_ICONERROR);
            return;
        }
        m_log.write(enable ? "start with Windows: on" : "start with Windows: off");
    }

    void App::shutdown() noexcept
    {
        if (m_service)
        {
            m_service->stop();
            m_service.reset();
        }
        m_tray.reset();
    }
} // namespace AcerFan
