#include "config.hpp"

#include "com_util.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>

namespace AcerFan
{
    namespace
    {
        constexpr std::string_view default_cpu_curve = "80:40, 84:60, 87:80, 89:100";
        constexpr std::string_view default_gpu_curve = "75:40, 80:60, 84:80, 86:100";

        constexpr std::string_view default_ini = R"(; AcerFan settings. After editing, pick "Reload settings" in the tray menu.
;
; Goal: no thermal throttling, not low temperatures. Laptop chips normally run at 80-90 C.
; The i9-9980HK of the ConceptD 9 throttles at 90 C, its RTX 2080 at about 87 C.

[curve]
; baseline temperature (C) : fan percent, linear between points.
; Below the first point the firmware's own Auto mode runs the fans.
; Both fans follow whichever chip asks for more.
cpu = 80:40, 84:60, 87:80, 89:100
gpu = 75:40, 80:60, 84:80, 86:100

[control]
poll_ms = 2000
; The curves use each chip's baseline: the baseline_percent percentile of the last baseline_s seconds.
; Under light, bursty work the CPU flashes to 85-90 C for a second and drops back; fans cannot help with
; those flashes, only with heat that stays, and a flash does not move the baseline.
baseline_s = 60
baseline_percent = 25
; readings needed before AcerFan may take over, and polls in a row with a curve request before it does
min_samples = 5
enter_polls = 2
; hand back to Auto once the CPU and GPU baselines stay at or below these for exit_hold_s
cpu_exit_c = 74
gpu_exit_c = 70
exit_hold_s = 30
; speed rises by at most up_step_percent and falls by at most down_step_percent per poll,
; falling only after every request of the last down_hold_polls polls was lower
up_step_percent = 10
down_step_percent = 5
down_hold_polls = 3
min_change_percent = 3
; check this often that nothing else changed the fan mode
reassert_s = 10
; fans go to failsafe_percent when temperatures cannot be read for failsafe_polls polls
failsafe_percent = 100
failsafe_polls = 5

[log]
; status line in AcerFan.log every this many seconds, 0 = off
status_every_s = 10

[state]
; curve, auto or full. Set from the tray menu.
mode = curve
)";

        std::wstring read_string(const std::filesystem::path &ini, const wchar_t *section, const wchar_t *key)
        {
            std::array<wchar_t, 512> buffer{};
            GetPrivateProfileStringW(
                section,
                key,
                L"",
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                ini.c_str()
            );
            return buffer.data();
        }

        /// Reads an integer; missing or out-of-range values keep `fallback` and add a warning.
        int read_int(
            const std::filesystem::path &ini,
            const wchar_t *section,
            const wchar_t *key,
            int fallback,
            int min,
            int max,
            std::vector<std::string> &warnings
        )
        {
            const std::wstring text = read_string(ini, section, key);
            if (text.empty())
            {
                return fallback;
            }
            wchar_t *end = nullptr;
            const long value = std::wcstol(text.c_str(), &end, 10);
            if (end == text.c_str() || value < min || value > max)
            {
                warnings.push_back(
                    std::format("[{}] {} = {} is invalid (allowed {}..{}), using {}", to_utf8(section), to_utf8(key),
                                to_utf8(text), min, max, fallback)
                );
                return fallback;
            }
            return static_cast<int>(value);
        }

        FanCurve read_curve(
            const std::filesystem::path &ini,
            const wchar_t *key,
            std::string_view fallback,
            std::vector<std::string> &warnings
        )
        {
            const std::string text = to_utf8(read_string(ini, L"curve", key));
            if (!text.empty())
            {
                if (auto curve = parse_curve(text))
                {
                    return *std::move(curve);
                }
                warnings.push_back(std::format("[curve] {} = {} is invalid, using {}", to_utf8(key), text, fallback));
            }
            return *parse_curve(fallback);
        }

        UserMode parse_mode(std::wstring_view text) noexcept
        {
            if (text == L"auto")
            {
                return UserMode::firmware_auto;
            }
            if (text == L"full")
            {
                return UserMode::full_speed;
            }
            return UserMode::curve;
        }
    } // namespace

    std::string_view mode_name(UserMode mode) noexcept
    {
        switch (mode)
        {
        case UserMode::firmware_auto:
            return "auto";
        case UserMode::full_speed:
            return "full";
        case UserMode::curve:
            break;
        }
        return "curve";
    }

    AppConfig load_config(const std::filesystem::path &ini, std::vector<std::string> &warnings)
    {
        if (std::error_code error; !std::filesystem::exists(ini, error))
        {
            std::ofstream(ini, std::ios::binary) << default_ini;
        }

        AppConfig config;
        ControllerSettings &c = config.controller;
        c.cpu_curve = read_curve(ini, L"cpu", default_cpu_curve, warnings);
        c.gpu_curve = read_curve(ini, L"gpu", default_gpu_curve, warnings);

        const int poll_ms = read_int(ini, L"control", L"poll_ms", 2000, 500, 10000, warnings);
        config.poll_interval = std::chrono::milliseconds{poll_ms};
        const int baseline_s = read_int(ini, L"control", L"baseline_s", 60, 2, 600, warnings);
        c.baseline_polls = std::max(1, (baseline_s * 1000 + poll_ms - 1) / poll_ms);
        c.baseline_percentile = read_int(ini, L"control", L"baseline_percent", 25, 0, 100, warnings) / 100.0;
        c.min_samples = read_int(ini, L"control", L"min_samples", 5, 1, 300, warnings);
        c.enter_polls = read_int(ini, L"control", L"enter_polls", 2, 1, 60, warnings);
        c.cpu_exit_c = read_int(ini, L"control", L"cpu_exit_c", 74, 30, 100, warnings);
        c.gpu_exit_c = read_int(ini, L"control", L"gpu_exit_c", 70, 30, 100, warnings);
        const int exit_hold_s = read_int(ini, L"control", L"exit_hold_s", 30, 0, 3600, warnings);
        c.exit_hold_polls = std::max(1, (exit_hold_s * 1000 + poll_ms - 1) / poll_ms);
        c.up_step_percent = read_int(ini, L"control", L"up_step_percent", 10, 1, 100, warnings);
        c.down_step_percent = read_int(ini, L"control", L"down_step_percent", 5, 1, 100, warnings);
        c.down_hold_polls = read_int(ini, L"control", L"down_hold_polls", 3, 1, 60, warnings);
        c.min_change_percent = read_int(ini, L"control", L"min_change_percent", 3, 1, 50, warnings);
        c.failsafe_percent = read_int(ini, L"control", L"failsafe_percent", 100, 50, 100, warnings);
        c.failsafe_polls = read_int(ini, L"control", L"failsafe_polls", 5, 1, 60, warnings);
        config.reassert_interval = std::chrono::seconds{read_int(ini, L"control", L"reassert_s", 10, 2, 600, warnings)};
        config.status_log_interval =
            std::chrono::seconds{read_int(ini, L"log", L"status_every_s", 10, 0, 3600, warnings)};
        config.mode = parse_mode(read_string(ini, L"state", L"mode"));
        return config;
    }

    void save_mode(const std::filesystem::path &ini, UserMode mode)
    {
        WritePrivateProfileStringW(L"state", L"mode", to_wide(mode_name(mode)).c_str(), ini.c_str());
    }
} // namespace AcerFan
