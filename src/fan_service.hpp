/**
 * @file fan_service.hpp
 * @brief The worker thread that reads the sensors, runs FanController and drives the fans.
 */
#ifndef ACERFAN_FAN_SERVICE_HPP
#define ACERFAN_FAN_SERVICE_HPP

#include "config.hpp"
#include "fan_controller.hpp"
#include "logger.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace AcerFan
{
    /**
     * @brief A snapshot of the fan service for the tray icon and menu.
     */
    struct FanStatus
    {
        /// True while the firmware interface answers.
        bool connected{false};
        /// Holds the latest connection or read error, empty while everything works.
        std::string error;
        /// Latest CPU temperature in degrees Celsius.
        std::optional<int> cpu_c;
        /// Latest GPU temperature in degrees Celsius.
        std::optional<int> gpu_c;
        /// CPU baseline temperature that the curves act on.
        int cpu_baseline_c{0};
        /// GPU baseline temperature that the curves act on.
        int gpu_baseline_c{0};
        /// Latest CPU fan speed in rpm.
        std::optional<int> cpu_rpm;
        /// Latest GPU fan speed in rpm.
        std::optional<int> gpu_rpm;
        /// Names the mode that the user picked in the tray menu.
        UserMode mode{UserMode::Curve};
        /// Holds the setting that AcerFan last applied to the fans.
        FanCommand command;
        /// True while the fail-safe speed applies.
        bool failsafe{false};
    };

    /**
     * @brief The worker thread that reads the sensors, runs FanController and drives the fans.
     * @details Any thread can call the public methods. The worker thread owns the firmware connection and makes every
     *          firmware call.
     */
    class FanService
    {
    public:
        /**
         * @brief Starts the worker thread.
         * @param log Receives the decisions and errors. It must outlive the service.
         * @param config Holds the initial settings.
         * @param on_status Runs on the worker thread after every poll. It must return fast, for example after a
         *                  PostMessage call.
         */
        FanService(Logger &log, AppConfig config, std::function<void()> on_status);

        /** @brief Calls stop(). */
        ~FanService() noexcept;

        FanService(const FanService &) = delete;
        FanService &operator=(const FanService &) = delete;
        FanService(FanService &&) = delete;
        FanService &operator=(FanService &&) = delete;

        /**
         * @brief Hands the fans back to firmware Auto and joins the worker thread.
         * @details A second call does nothing.
         */
        void stop() noexcept;

        /** @brief Switches to `mode` at the next poll. */
        void set_mode(UserMode mode) noexcept;

        /** @brief Replaces every setting with `config` at the next poll. */
        void reload(AppConfig config);

        /** @brief Reconnects and re-applies the fan setting at the next poll, after sleep or hibernation. */
        void resumed() noexcept;

        /** @brief Returns a copy of the latest status. */
        [[nodiscard]] FanStatus status() const;

    private:
        // Requests from other threads. The worker takes them at the start of each poll.
        struct Requests
        {
            std::optional<UserMode> mode;
            std::optional<AppConfig> config;
            bool resumed{false};

            [[nodiscard]] bool any() const noexcept { return mode || config || resumed; }
        };

        // The state of the worker thread. fan_service.cpp defines it.
        class Loop;

        void run(std::stop_token stop) noexcept;
        [[nodiscard]] Requests take_requests();
        void publish(const FanStatus &status);
        void wait(const std::stop_token &stop, std::chrono::milliseconds duration);

        Logger &m_log;
        std::function<void()> m_on_status;
        AppConfig m_initial_config;

        mutable std::mutex m_mutex;
        std::condition_variable_any m_wake;
        Requests m_requests;
        FanStatus m_status;

        // The last member: the thread joins before the state above is destroyed.
        std::jthread m_thread;
    };
} // namespace AcerFan

#endif // ACERFAN_FAN_SERVICE_HPP
