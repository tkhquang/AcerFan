/**
 * @file config.hpp
 * @brief AcerFan.ini: fan curves, control tuning and the selected mode.
 */
#ifndef ACERFAN_CONFIG_HPP
#define ACERFAN_CONFIG_HPP

#include "fan_controller.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace AcerFan
{
    /// What the user picked in the tray menu.
    enum class UserMode
    {
        curve,         ///< AcerFan drives the fans from the temperature curves.
        firmware_auto, ///< The firmware's own Auto mode, AcerFan only watches.
        full_speed,    ///< Both fans at 100%.
    };

    [[nodiscard]] std::string_view mode_name(UserMode mode) noexcept;

    struct AppConfig
    {
        ControllerSettings controller;
        std::chrono::milliseconds poll_interval{2000};
        std::chrono::seconds reassert_interval{10};
        std::chrono::seconds status_log_interval{10};
        UserMode mode{UserMode::curve};
    };

    /**
     * @brief Reads `ini`, writing a commented default file first when it does not exist.
     * @param warnings Receives one line per invalid value; each one falls back to its default.
     */
    [[nodiscard]] AppConfig load_config(const std::filesystem::path &ini, std::vector<std::string> &warnings);

    /// Stores the tray-menu mode so it survives a restart.
    void save_mode(const std::filesystem::path &ini, UserMode mode);
} // namespace AcerFan

#endif // ACERFAN_CONFIG_HPP
