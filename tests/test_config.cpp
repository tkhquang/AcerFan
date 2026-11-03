/**
 * @file tests/test_config.cpp
 * @brief AcerFan.ini: the written defaults, invalid values, the mode round trip and the number parsers.
 */
#include "check.hpp"

#include "config.hpp"
#include "text.hpp"

#include <process.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>
#include <system_error>

using namespace AcerFan;

namespace
{
    constexpr std::string_view CPU_CURVE = "80:40, 84:60, 87:80, 89:100";
    constexpr std::string_view GPU_CURVE = "75:40, 80:60, 84:80, 86:100";

    int s_file_counter = 0;

    // A unique INI path in the temp folder. The destructor removes the file.
    class TempIni
    {
    public:
        TempIni()
            : m_path(
                  std::filesystem::temp_directory_path() /
                  std::format("acerfan_test_{}_{}.ini", _getpid(), ++s_file_counter)
              )
        {
        }

        ~TempIni() noexcept
        {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }

        TempIni(const TempIni &) = delete;
        TempIni &operator=(const TempIni &) = delete;
        TempIni(TempIni &&) = delete;
        TempIni &operator=(TempIni &&) = delete;

        void write(std::string_view text) const { std::ofstream(m_path, std::ios::binary) << text; }

        [[nodiscard]] const std::filesystem::path &path() const noexcept { return m_path; }

    private:
        std::filesystem::path m_path;
    };

    AppConfig built_in_defaults()
    {
        AppConfig config;
        config.controller.cpu_curve = *parse_curve(CPU_CURVE);
        config.controller.gpu_curve = *parse_curve(GPU_CURVE);
        return config;
    }

    void test_number_parsers()
    {
        CHECK(trim("  a b \t") == "a b");
        CHECK(trim(" \t ").empty());
        CHECK(parse_int("42") == 42);
        CHECK(parse_int("  -7\t") == -7);
        CHECK(!parse_int(""));
        CHECK(!parse_int("12x"));
        CHECK(!parse_int("x12"));
        CHECK(!parse_int("99999999999"));
        CHECK(parse_u64("18446744073709551615") == UINT64_MAX);
        CHECK(!parse_u64("-1"));
        CHECK(!parse_u64("18446744073709551616"));
    }

    void test_missing_file_gets_defaults()
    {
        const TempIni ini;
        const LoadedConfig loaded = load_config(ini.path());
        CHECK(std::filesystem::exists(ini.path()));
        CHECK(loaded.warnings.empty());
        // The written file and the built-in defaults agree. The check covers the seconds that become polls.
        CHECK(loaded.config == built_in_defaults());

        const LoadedConfig reloaded = load_config(ini.path());
        CHECK(reloaded.warnings.empty());
        CHECK(reloaded.config == loaded.config);
    }

    void test_invalid_values_keep_defaults()
    {
        const TempIni ini;
        ini.write(
            "[curve]\r\n"
            "cpu = 80:40\r\n"
            "gpu = 70:30, 90:100\r\n"
            "[control]\r\n"
            "poll_ms = 1000\r\n"
            "baseline_s = 30\r\n"
            "min_samples = 0\r\n"
            "enter_polls = abc\r\n"
            "[state]\r\n"
            "mode = turbo\r\n"
        );
        const LoadedConfig loaded = load_config(ini.path());
        const ControllerSettings &controller = loaded.config.controller;
        const ControllerSettings defaults = built_in_defaults().controller;

        // The CPU curve, min_samples, enter_polls and the mode are invalid.
        CHECK(loaded.warnings.size() == 4);
        CHECK(controller.cpu_curve.to_string() == CPU_CURVE);
        CHECK(controller.gpu_curve.to_string() == "70:30, 90:100");
        CHECK(loaded.config.poll_interval == std::chrono::milliseconds{1000});
        // 30 seconds at one poll per second.
        CHECK(controller.baseline_polls == 30);
        CHECK(controller.min_samples == defaults.min_samples);
        CHECK(controller.enter_polls == defaults.enter_polls);
        CHECK(loaded.config.mode == UserMode::Curve);
    }

    void test_mode_round_trip()
    {
        const TempIni ini;
        CHECK(load_config(ini.path()).warnings.empty());
        CHECK(save_mode(ini.path(), UserMode::FullSpeed).has_value());
        CHECK(load_config(ini.path()).config.mode == UserMode::FullSpeed);
        CHECK(save_mode(ini.path(), UserMode::FirmwareAuto).has_value());
        CHECK(load_config(ini.path()).config.mode == UserMode::FirmwareAuto);
        CHECK(mode_name(UserMode::Curve) == "curve");
    }
} // namespace

int main()
{
    test_number_parsers();
    test_missing_file_gets_defaults();
    test_invalid_values_keep_defaults();
    test_mode_round_trip();
    return AcerFan::tests::finish("test_config");
}
