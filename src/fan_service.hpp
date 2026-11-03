/**
 * @file fan_service.hpp
 * @brief Background thread that reads the sensors, runs FanController and drives the fans.
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
    /// Snapshot for the tray icon and menu.
    struct FanStatus
    {
        bool connected{false};
        std::string error; ///< Last connection or read error; empty while everything works.
        std::optional<int> cpu_c;
        std::optional<int> gpu_c;
        int cpu_base{0}; ///< Baseline temperature the curves act on.
        int gpu_base{0};
        std::optional<int> cpu_rpm;
        std::optional<int> gpu_rpm;
        UserMode mode{UserMode::curve};
        FanCommand command; ///< What the fans are set to.
        bool failsafe{false};
    };

    class FanService
    {
    public:
        /// Starts the worker thread. `on_status` runs on that thread after every poll; keep it cheap (PostMessage).
        FanService(Logger &log, AppConfig config, std::function<void()> on_status);
        ~FanService();

        FanService(const FanService &) = delete;
        FanService &operator=(const FanService &) = delete;
        FanService(FanService &&) = delete;
        FanService &operator=(FanService &&) = delete;

        /// Hands the fans back to firmware Auto and joins the thread. Safe to call more than once.
        void stop();

        void set_mode(UserMode mode);
        void reload(AppConfig config);

        /// After sleep or hibernation: reconnect and re-apply the fan state.
        void resumed();

        [[nodiscard]] FanStatus status() const;

    private:
        struct Requests
        {
            std::optional<UserMode> mode;
            std::optional<AppConfig> config;
            bool resumed{false};

            [[nodiscard]] bool any() const noexcept { return mode || config || resumed; }
        };

        void run(std::stop_token stop);
        void control_loop(std::stop_token stop);
        void publish(const FanStatus &status);

        /// Sleeps for `duration`, waking early on a stop request or a new request.
        void wait(std::stop_token stop, std::chrono::milliseconds duration);

        Logger &m_log;
        std::function<void()> m_on_status;
        AppConfig m_initial_config;

        mutable std::mutex m_mutex;
        std::condition_variable_any m_wake;
        Requests m_requests;
        FanStatus m_status;

        std::jthread m_thread; // Last member: joined before the state above is destroyed.
    };
} // namespace AcerFan

#endif // ACERFAN_FAN_SERVICE_HPP
