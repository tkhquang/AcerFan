#include "config.hpp"

#include "com_util.hpp"
#include "text.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace AcerFan
{
    namespace
    {
        constexpr std::string_view DEFAULT_CPU_CURVE = "80:40, 84:60, 87:80, 89:100";
        constexpr std::string_view DEFAULT_GPU_CURVE = "75:40, 80:60, 84:80, 86:100";
        constexpr std::chrono::seconds DEFAULT_BASELINE_WINDOW{60};
        constexpr std::chrono::seconds DEFAULT_EXIT_HOLD{30};
        constexpr double PERCENT_SCALE = 100.0;
        constexpr std::size_t MAX_VALUE_CHARS = 512;

        constexpr std::array ALL_USER_MODES{
            UserMode::Curve,
            UserMode::FirmwareAuto,
            UserMode::FullSpeed,
        };

        // Written on the first start. Every value matches the built-in default, which a test verifies.
        constexpr std::string_view DEFAULT_INI =
            R"(; AcerFan settings. Pick "Reload settings" in the tray menu after an edit.
;
; Goal: no thermal throttle, not low temperatures. Laptop chips normally run at 80-90 C.
; The i9-9980HK of the ConceptD 9 throttles at 90 C, its RTX 2080 at about 87 C.

[curve]
; Baseline temperature (C) : fan percent, linear between the points.
; Below the first point the Auto mode of the firmware runs the fans.
; Both fans follow the chip that asks for more.
cpu = 80:40, 84:60, 87:80, 89:100
gpu = 75:40, 80:60, 84:80, 86:100

[control]
; Time between two sensor reads, in milliseconds.
poll_ms = 2000
; The curves use the baseline of each chip: the baseline_percent percentile of the last baseline_s seconds.
; Under light, bursty work the CPU flashes to 85-90 C for a second and drops back. Fans cannot help with
; those flashes, only with heat that stays, and a flash does not move the baseline.
baseline_s = 60
baseline_percent = 25
; AcerFan takes over after min_samples readings and enter_polls consecutive polls with a curve request.
min_samples = 5
enter_polls = 2
; AcerFan hands the fans back to Auto when the CPU and GPU baselines stay at or below these for exit_hold_s.
cpu_exit_c = 74
gpu_exit_c = 70
exit_hold_s = 30
; Per poll the speed rises by at most up_step_percent and falls by at most down_step_percent.
; It falls only when every request of the last down_hold_polls polls was lower.
; AcerFan skips a change smaller than min_change_percent, except a step to the floor or the ceiling.
up_step_percent = 10
down_step_percent = 5
down_hold_polls = 3
min_change_percent = 3
; Seconds between two checks that no other app changed the fan mode.
reassert_s = 10
; The fans go to failsafe_percent when the temperatures stay unreadable for failsafe_polls polls.
failsafe_percent = 100
failsafe_polls = 5

[log]
; Seconds between two status lines in AcerFan.log, 0 for none.
status_every_s = 10

[state]
; curve, auto or full. The tray menu sets it.
mode = curve
)";

        /**
         * @brief An inclusive range of valid integer values.
         */
        struct Range
        {
            int min{0};
            int max{0};
        };

        /**
         * @brief One section of an INI file. Each read adds a warning for an invalid value.
         */
        class IniSection
        {
        public:
            IniSection(
                const std::filesystem::path &file,
                const wchar_t *name,
                std::vector<std::string> &warnings
            ) noexcept
                : m_file(&file), m_name(name), m_warnings(&warnings)
            {
            }

            /** @brief Replaces `value` with the integer under `key`, unless the key is missing or invalid. */
            void read(const wchar_t *key, Range range, int &value) const
            {
                const std::string text = value_of(key);
                if (text.empty())
                {
                    return;
                }
                const std::optional<int> parsed = parse_int(text);
                if (!parsed || *parsed < range.min || *parsed > range.max)
                {
                    warn(
                        key,
                        std::format("{} is invalid (allowed {}..{}), using {}", text, range.min, range.max, value)
                    );
                    return;
                }
                value = *parsed;
            }

            /** @brief Replaces `curve` with the curve under `key`, unless the key is missing or invalid. */
            void read(const wchar_t *key, FanCurve &curve) const
            {
                const std::string text = value_of(key);
                if (text.empty())
                {
                    return;
                }
                if (std::optional<FanCurve> parsed = parse_curve(text))
                {
                    curve = *std::move(parsed);
                    return;
                }
                warn(key, std::format("{} is invalid, using {}", text, curve.to_string()));
            }

            /** @brief Replaces `mode` with the mode under `key`, unless the key is missing or invalid. */
            void read(const wchar_t *key, UserMode &mode) const
            {
                const std::string text = value_of(key);
                if (text.empty())
                {
                    return;
                }
                const auto found = std::ranges::find(ALL_USER_MODES, text, mode_name);
                if (found == ALL_USER_MODES.end())
                {
                    warn(
                        key,
                        std::format("{} is invalid (allowed curve, auto, full), using {}", text, mode_name(mode))
                    );
                    return;
                }
                mode = *found;
            }

        private:
            [[nodiscard]] std::string value_of(const wchar_t *key) const
            {
                std::array<wchar_t, MAX_VALUE_CHARS> buffer{};
                GetPrivateProfileStringW(
                    m_name,
                    key,
                    L"",
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    m_file->c_str()
                );
                return std::string{trim(to_utf8(buffer.data()))};
            }

            void warn(const wchar_t *key, std::string_view problem) const
            {
                m_warnings->push_back(std::format("[{}] {} = {}", to_utf8(m_name), to_utf8(key), problem));
            }

            const std::filesystem::path *m_file{nullptr};
            const wchar_t *m_name{nullptr};
            std::vector<std::string> *m_warnings{nullptr};
        };

        /** @brief Returns the number of polls that cover `window`, at least 1. */
        int polls_for(std::chrono::seconds window, std::chrono::milliseconds poll_interval) noexcept
        {
            const auto window_ms = std::chrono::duration_cast<std::chrono::milliseconds>(window).count();
            const auto poll_ms = poll_interval.count();
            return std::max(1, static_cast<int>((window_ms + poll_ms - 1) / poll_ms));
        }

        /** @brief Reads a duration in whole seconds under `key` into `duration`. */
        void read_seconds(const IniSection &section, const wchar_t *key, Range range, std::chrono::seconds &duration)
        {
            int seconds = static_cast<int>(duration.count());
            section.read(key, range, seconds);
            duration = std::chrono::seconds{seconds};
        }
    } // namespace

    std::string_view mode_name(UserMode mode) noexcept
    {
        switch (mode)
        {
        case UserMode::FirmwareAuto:
            return "auto";
        case UserMode::FullSpeed:
            return "full";
        case UserMode::Curve:
            break;
        }
        return "curve";
    }

    LoadedConfig load_config(const std::filesystem::path &ini)
    {
        if (std::error_code error; !std::filesystem::exists(ini, error))
        {
            std::ofstream(ini, std::ios::binary) << DEFAULT_INI;
        }

        LoadedConfig loaded;
        AppConfig &config = loaded.config;
        ControllerSettings &controller = config.controller;
        controller.cpu_curve = *parse_curve(DEFAULT_CPU_CURVE);
        controller.gpu_curve = *parse_curve(DEFAULT_GPU_CURVE);

        const IniSection curve_section{ini, L"curve", loaded.warnings};
        curve_section.read(L"cpu", controller.cpu_curve);
        curve_section.read(L"gpu", controller.gpu_curve);

        const IniSection control_section{ini, L"control", loaded.warnings};
        int poll_ms = static_cast<int>(config.poll_interval.count());
        control_section.read(L"poll_ms", {500, 10000}, poll_ms);
        config.poll_interval = std::chrono::milliseconds{poll_ms};

        std::chrono::seconds baseline_window = DEFAULT_BASELINE_WINDOW;
        read_seconds(control_section, L"baseline_s", {2, 600}, baseline_window);
        controller.baseline_polls = polls_for(baseline_window, config.poll_interval);

        int baseline_percent = static_cast<int>(std::lround(controller.baseline_percentile * PERCENT_SCALE));
        control_section.read(L"baseline_percent", {0, 100}, baseline_percent);
        controller.baseline_percentile = baseline_percent / PERCENT_SCALE;

        control_section.read(L"min_samples", {1, 300}, controller.min_samples);
        control_section.read(L"enter_polls", {1, 60}, controller.enter_polls);
        control_section.read(L"cpu_exit_c", {30, 100}, controller.cpu_exit_c);
        control_section.read(L"gpu_exit_c", {30, 100}, controller.gpu_exit_c);

        std::chrono::seconds exit_hold = DEFAULT_EXIT_HOLD;
        read_seconds(control_section, L"exit_hold_s", {0, 3600}, exit_hold);
        controller.exit_hold_polls = polls_for(exit_hold, config.poll_interval);

        control_section.read(L"up_step_percent", {1, 100}, controller.up_step_percent);
        control_section.read(L"down_step_percent", {1, 100}, controller.down_step_percent);
        control_section.read(L"down_hold_polls", {1, 60}, controller.down_hold_polls);
        control_section.read(L"min_change_percent", {1, 50}, controller.min_change_percent);
        read_seconds(control_section, L"reassert_s", {2, 600}, config.reassert_interval);
        control_section.read(L"failsafe_percent", {50, 100}, controller.failsafe_percent);
        control_section.read(L"failsafe_polls", {1, 60}, controller.failsafe_polls);

        const IniSection log_section{ini, L"log", loaded.warnings};
        read_seconds(log_section, L"status_every_s", {0, 3600}, config.status_log_interval);

        const IniSection state_section{ini, L"state", loaded.warnings};
        state_section.read(L"mode", config.mode);
        return loaded;
    }

    Result<void> save_mode(const std::filesystem::path &ini, UserMode mode)
    {
        const std::wstring value = to_wide(mode_name(mode));
        if (WritePrivateProfileStringW(L"state", L"mode", value.c_str(), ini.c_str()) == FALSE)
        {
            return fail_last_error("save the mode to AcerFan.ini");
        }
        return {};
    }
} // namespace AcerFan
