/**
 * @file config.hpp
 * @brief AcerFan.ini: the fan curves, the control tuning and the selected mode.
 */
#ifndef ACERFAN_CONFIG_HPP
#define ACERFAN_CONFIG_HPP

#include "error.hpp"
#include "fan_controller.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace AcerFan
{
    /**
     * @brief The fan mode that the user picks in the tray menu.
     */
    enum class UserMode
    {
        /// AcerFan drives the fans from the temperature curves.
        Curve,
        /// The Auto mode of the firmware runs the fans, and AcerFan only watches.
        FirmwareAuto,
        /// Both fans run at 100%.
        FullSpeed,
    };

    /** @brief Returns the AcerFan.ini spelling of `mode`: "curve", "auto" or "full". */
    [[nodiscard]] std::string_view mode_name(UserMode mode) noexcept;

    /**
     * @brief Every setting of AcerFan.ini.
     */
    struct AppConfig
    {
        /// Holds the curves and the tuning of the fan controller.
        ControllerSettings controller;
        /// Time between two sensor reads.
        std::chrono::milliseconds poll_interval{2000};
        /// Time between two checks that no other app changed the fan mode.
        std::chrono::seconds reassert_interval{10};
        /// Time between two status lines in AcerFan.log, 0 for none.
        std::chrono::seconds status_log_interval{10};
        /// Names the mode that the user picked in the tray menu.
        UserMode mode{UserMode::Curve};

        /// Equal when every field is equal.
        friend bool operator==(const AppConfig &, const AppConfig &) = default;
    };

    /**
     * @brief The result of load_config().
     */
    struct LoadedConfig
    {
        /// Holds the settings. An invalid value keeps its default.
        AppConfig config;
        /// Holds one line for each invalid value.
        std::vector<std::string> warnings;
    };

    /**
     * @brief Reads `ini`.
     * @details A missing file is first created with the defaults and their comments.
     */
    [[nodiscard]] LoadedConfig load_config(const std::filesystem::path &ini);

    /**
     * @brief Stores `mode` in `ini` for the next start.
     * @return An Error when Windows cannot write the file.
     */
    [[nodiscard]] Result<void> save_mode(const std::filesystem::path &ini, UserMode mode);
} // namespace AcerFan

#endif // ACERFAN_CONFIG_HPP
