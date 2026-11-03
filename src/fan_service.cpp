#include "fan_service.hpp"

#include "acer_wmi.hpp"
#include "com_util.hpp"

#include <chrono>
#include <format>
#include <utility>

namespace AcerFan
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr std::chrono::milliseconds reconnect_delay{5000};
        constexpr int full_speed_percent = 100;

        FanCommand wanted_for(UserMode mode, const FanController &controller) noexcept
        {
            switch (mode)
            {
            case UserMode::firmware_auto:
                return {};
            case UserMode::full_speed:
                return {FanMode::custom, full_speed_percent};
            case UserMode::curve:
                break;
            }
            return controller.command();
        }

        std::string describe(const FanCommand &command)
        {
            return command.mode == FanMode::custom ? std::format("custom {}%", command.percent)
                                                   : std::string{"firmware Auto"};
        }

        std::string optional_text(const std::optional<int> &value)
        {
            return value ? std::to_string(*value) : std::string{"?"};
        }
    } // namespace

    FanService::FanService(Logger &log, AppConfig config, std::function<void()> on_status)
        : m_log(log), m_on_status(std::move(on_status)), m_initial_config(std::move(config))
    {
        m_status.mode = m_initial_config.mode;
        m_thread = std::jthread([this](std::stop_token stop) { run(stop); });
    }

    FanService::~FanService()
    {
        stop();
    }

    void FanService::stop()
    {
        if (m_thread.joinable())
        {
            m_thread.request_stop();
            m_thread.join();
        }
    }

    void FanService::set_mode(UserMode mode)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_requests.mode = mode;
        }
        m_wake.notify_all();
    }

    void FanService::reload(AppConfig config)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_requests.config = std::move(config);
        }
        m_wake.notify_all();
    }

    void FanService::resumed()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_requests.resumed = true;
        }
        m_wake.notify_all();
    }

    FanStatus FanService::status() const
    {
        const std::lock_guard lock(m_mutex);
        return m_status;
    }

    void FanService::publish(const FanStatus &status)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_status = status;
        }
        if (m_on_status)
        {
            m_on_status();
        }
    }

    void FanService::wait(std::stop_token stop, std::chrono::milliseconds duration)
    {
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, stop, duration, [this] { return m_requests.any(); });
    }

    void FanService::run(std::stop_token stop)
    {
        try
        {
            const ComApartment apartment{COINIT_MULTITHREADED};
            control_loop(stop);
        }
        catch (const std::exception &e)
        {
            m_log.write(std::format("fan thread stopped by an unexpected error: {}", e.what()));
        }
    }

    void FanService::control_loop(std::stop_token stop)
    {
        AppConfig config = m_initial_config;
        FanController controller{config.controller};
        std::optional<AcerWmi> wmi;
        std::optional<FanCommand> applied; // What AcerFan last set; empty forces a full re-apply.
        FanStatus status;
        std::string last_error;
        auto next_reassert = Clock::now();
        auto next_status_log = Clock::now();

        m_log.write(std::format(
            "started; mode {}; CPU curve {}; GPU curve {}",
            mode_name(config.mode),
            config.controller.cpu_curve.to_string(),
            config.controller.gpu_curve.to_string()
        ));

        const auto note_error = [&](std::string_view prefix, const ComError &error) {
            status.error = error.what();
            if (status.error != last_error)
            {
                m_log.write(std::format("{}: {}", prefix, status.error));
                last_error = status.error;
            }
        };

        while (!stop.stop_requested())
        {
            Requests requests;
            {
                const std::lock_guard lock(m_mutex);
                requests = std::exchange(m_requests, {});
            }
            if (requests.config)
            {
                config = *std::move(requests.config);
                controller = FanController{config.controller};
                applied.reset();
                m_log.write(std::format(
                    "settings reloaded; mode {}; CPU curve {}; GPU curve {}",
                    mode_name(config.mode),
                    config.controller.cpu_curve.to_string(),
                    config.controller.gpu_curve.to_string()
                ));
            }
            if (requests.mode && *requests.mode != config.mode)
            {
                config.mode = *requests.mode;
                controller.reset();
                applied.reset();
                m_log.write(std::format("mode: {}", mode_name(config.mode)));
            }
            if (requests.resumed)
            {
                wmi.reset();
                applied.reset();
                m_log.write("resumed from sleep; reconnecting and re-applying the fan state");
            }
            status.mode = config.mode;

            if (!wmi)
            {
                try
                {
                    wmi.emplace();
                    if (!status.connected)
                    {
                        m_log.write("connected to the AcerGamingFunction firmware interface");
                    }
                }
                catch (const ComError &error)
                {
                    status.connected = false;
                    note_error("cannot connect", error);
                    publish(status);
                    wait(stop, reconnect_delay);
                    continue;
                }
            }
            status.connected = true;

            bool read_ok = false;
            try
            {
                const int cpu = wmi->read(Sensor::cpu_temp);
                const int gpu = wmi->read(Sensor::gpu_temp);
                status.cpu_c = cpu;
                status.gpu_c = gpu;
                (void)controller.on_reading({cpu, gpu});
                read_ok = true;
                status.error.clear();
                last_error.clear();
            }
            catch (const ComError &error)
            {
                note_error("sensor read failed", error);
                const bool was_failsafe = controller.in_failsafe();
                (void)controller.on_read_failure();
                if (controller.in_failsafe() && !was_failsafe && config.mode == UserMode::curve)
                {
                    m_log.write(std::format(
                        "fail-safe: temperatures unreadable for {} polls, fans to {}%",
                        config.controller.failsafe_polls,
                        config.controller.failsafe_percent
                    ));
                }
            }
            status.cpu_base = controller.cpu_baseline();
            status.gpu_base = controller.gpu_baseline();
            status.failsafe = controller.in_failsafe() && config.mode == UserMode::curve;

            const FanCommand wanted = wanted_for(config.mode, controller);
            try
            {
                if (!applied || applied->mode != wanted.mode)
                {
                    wmi->set_mode(wanted.mode);
                    if (wanted.mode == FanMode::custom)
                    {
                        wmi->set_percent(wanted.percent);
                    }
                    const bool curve_mode = config.mode == UserMode::curve && applied.has_value();
                    if (curve_mode && wanted.mode == FanMode::custom)
                    {
                        m_log.write(std::format(
                            "take over: CPU {} C, GPU {} C (baseline) -> {}%",
                            status.cpu_base,
                            status.gpu_base,
                            wanted.percent
                        ));
                    }
                    else if (curve_mode)
                    {
                        m_log.write(std::format(
                            "hand back to firmware Auto: CPU {} C, GPU {} C (baseline)",
                            status.cpu_base,
                            status.gpu_base
                        ));
                    }
                    else
                    {
                        m_log.write(std::format("fans: {}", describe(wanted)));
                    }
                    applied = wanted;
                    next_reassert = Clock::now() + config.reassert_interval;
                }
                else if (wanted.mode == FanMode::custom && applied->percent != wanted.percent)
                {
                    wmi->set_percent(wanted.percent);
                    applied = wanted;
                }
                else if (Clock::now() >= next_reassert)
                {
                    next_reassert = Clock::now() + config.reassert_interval;
                    if (!wmi->is_mode(applied->mode))
                    {
                        wmi->set_mode(applied->mode);
                        if (applied->mode == FanMode::custom)
                        {
                            wmi->set_percent(applied->percent);
                        }
                        m_log.write(std::format("fan mode was changed by something else; re-applied {}",
                                                describe(*applied)));
                    }
                }
                status.command = *applied;
            }
            catch (const ComError &error)
            {
                note_error("fan control failed, reconnecting", error);
                wmi.reset();
                applied.reset();
                publish(status);
                wait(stop, reconnect_delay);
                continue;
            }

            try
            {
                status.cpu_rpm = wmi->read(Sensor::cpu_fan_rpm);
                status.gpu_rpm = wmi->read(Sensor::gpu_fan_rpm);
            }
            catch (const ComError &)
            {
                status.cpu_rpm.reset();
                status.gpu_rpm.reset();
            }

            if (!read_ok)
            {
                // A stale connection (seen after resume) keeps failing; a fresh one usually works.
                wmi.reset();
            }

            if (config.status_log_interval.count() > 0 && Clock::now() >= next_status_log)
            {
                next_status_log = Clock::now() + config.status_log_interval;
                m_log.write(std::format(
                    "status: CPU {} C (base {}), GPU {} C (base {}) | {} {} | fans {}/{} rpm",
                    optional_text(status.cpu_c),
                    status.cpu_base,
                    optional_text(status.gpu_c),
                    status.gpu_base,
                    mode_name(config.mode),
                    describe(status.command),
                    optional_text(status.cpu_rpm),
                    optional_text(status.gpu_rpm)
                ));
            }

            publish(status);
            wait(stop, config.poll_interval);
        }

        // Never leave the fans at a fixed speed behind us.
        try
        {
            if (!wmi)
            {
                wmi.emplace();
            }
            wmi->set_mode(FanMode::firmware_auto);
            m_log.write("stopped; fans back to firmware Auto");
        }
        catch (const ComError &error)
        {
            m_log.write(std::format("stop: could not restore firmware Auto: {}", error.what()));
        }
    }
} // namespace AcerFan
