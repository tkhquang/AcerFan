#include "fan_service.hpp"

#include "acer_wmi.hpp"
#include "com_util.hpp"

#include <exception>
#include <format>
#include <system_error>
#include <utility>

namespace AcerFan
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr std::chrono::milliseconds RECONNECT_DELAY{5000};
        constexpr int FULL_SPEED_PERCENT = 100;

        FanCommand wanted_for(UserMode mode, const FanController &controller) noexcept
        {
            switch (mode)
            {
            case UserMode::FirmwareAuto:
                return {};
            case UserMode::FullSpeed:
                return {FanMode::Custom, FULL_SPEED_PERCENT};
            case UserMode::Curve:
                break;
            }
            return controller.command();
        }

        std::string describe(const FanCommand &command)
        {
            if (command.mode == FanMode::Custom)
            {
                return std::format("custom {}%", command.percent);
            }
            return "firmware Auto";
        }

        std::string optional_text(const std::optional<int> &value)
        {
            return value ? std::to_string(*value) : std::string{"?"};
        }

        void log_unexpected(Logger &log, std::string_view what) noexcept
        {
            try
            {
                log.write(std::format("fan thread stopped by an unexpected error: {}", what));
            }
            catch (...)
            {
                log.write("fan thread stopped by an unexpected error");
            }
        }
    } // namespace

    class FanService::Loop
    {
    public:
        Loop(FanService &service, std::stop_token stop)
            : m_service(service), m_log(service.m_log), m_stop(std::move(stop)), m_config(service.m_initial_config),
              m_controller(m_config.controller), m_next_reassert(Clock::now()), m_next_status_log(Clock::now())
        {
        }

        // Polls until a stop request, then hands the fans back to firmware Auto.
        void run()
        {
            m_log.write(
                std::format(
                    "AcerFan {} started; mode {}; CPU curve {}; GPU curve {}",
                    ACERFAN_VERSION,
                    mode_name(m_config.mode),
                    m_config.controller.cpu_curve.to_string(),
                    m_config.controller.gpu_curve.to_string()
                )
            );

            while (!m_stop.stop_requested())
            {
                apply_requests(m_service.take_requests());
                m_status.mode = m_config.mode;
                if (!connect())
                {
                    pause(RECONNECT_DELAY);
                    continue;
                }

                const bool read_ok = read_temperatures();
                if (const Result<void> driven = drive_fans(); !driven)
                {
                    note_error("fan control failed, reconnecting", driven.error());
                    m_wmi.reset();
                    m_applied.reset();
                    pause(RECONNECT_DELAY);
                    continue;
                }
                read_fan_speeds();
                if (!read_ok)
                {
                    // A stale connection, as seen after a resume, fails on every call. A fresh one usually works.
                    m_wmi.reset();
                }
                log_status();
                pause(m_config.poll_interval);
            }
            restore_auto();
        }

    private:
        void apply_requests(Requests requests)
        {
            if (requests.config)
            {
                m_config = *std::move(requests.config);
                m_controller = FanController{m_config.controller};
                m_applied.reset();
                m_log.write(
                    std::format(
                        "settings reloaded; mode {}; CPU curve {}; GPU curve {}",
                        mode_name(m_config.mode),
                        m_config.controller.cpu_curve.to_string(),
                        m_config.controller.gpu_curve.to_string()
                    )
                );
            }
            if (requests.mode && *requests.mode != m_config.mode)
            {
                m_config.mode = *requests.mode;
                m_controller.reset();
                m_applied.reset();
                m_log.write(std::format("mode: {}", mode_name(m_config.mode)));
            }
            if (requests.resumed)
            {
                m_wmi.reset();
                m_applied.reset();
                m_log.write("resumed from sleep; reconnecting and re-applying the fan state");
            }
        }

        [[nodiscard]] bool connect()
        {
            if (m_wmi)
            {
                return true;
            }
            Result<AcerWmi> wmi = AcerWmi::connect();
            if (!wmi)
            {
                m_status.connected = false;
                note_error("cannot connect", wmi.error());
                return false;
            }
            m_wmi.emplace(*std::move(wmi));
            if (!m_status.connected)
            {
                m_log.write("connected to the AcerGamingFunction firmware interface");
                m_status.connected = true;
            }
            return true;
        }

        [[nodiscard]] bool read_temperatures()
        {
            const Result<Temperatures> temperatures = m_wmi->read_temperatures();
            if (temperatures)
            {
                m_status.cpu_c = temperatures->cpu_c;
                m_status.gpu_c = temperatures->gpu_c;
                m_controller.on_reading(*temperatures);
                m_status.error.clear();
                m_last_error.clear();
            }
            else
            {
                note_error("sensor read failed", temperatures.error());
                const bool was_failsafe = m_controller.in_failsafe();
                m_controller.on_read_failure();
                if (m_controller.in_failsafe() && !was_failsafe && m_config.mode == UserMode::Curve)
                {
                    m_log.write(
                        std::format(
                            "fail-safe: temperatures unreadable for {} polls, fans to {}%",
                            m_config.controller.failsafe_polls,
                            m_config.controller.failsafe_percent
                        )
                    );
                }
            }
            m_status.cpu_baseline_c = m_controller.cpu_baseline();
            m_status.gpu_baseline_c = m_controller.gpu_baseline();
            m_status.failsafe = m_controller.in_failsafe() && m_config.mode == UserMode::Curve;
            return temperatures.has_value();
        }

        [[nodiscard]] Result<void> drive_fans()
        {
            const FanCommand wanted = wanted_for(m_config.mode, m_controller);
            if (!m_applied || m_applied->mode != wanted.mode)
            {
                ACERFAN_TRY_VOID(m_wmi->apply(wanted));
                log_change(wanted);
                m_applied = wanted;
                m_next_reassert = Clock::now() + m_config.reassert_interval;
            }
            else if (wanted.mode == FanMode::Custom && m_applied->percent != wanted.percent)
            {
                ACERFAN_TRY_VOID(m_wmi->set_percent(wanted.percent));
                m_applied = wanted;
            }
            else if (Clock::now() >= m_next_reassert)
            {
                m_next_reassert = Clock::now() + m_config.reassert_interval;
                ACERFAN_TRY(in_mode, m_wmi->is_mode(m_applied->mode));
                if (!in_mode)
                {
                    ACERFAN_TRY_VOID(m_wmi->apply(*m_applied));
                    m_log.write(
                        std::format("fan mode was changed by something else; re-applied {}", describe(*m_applied))
                    );
                }
            }
            m_status.command = *m_applied;
            return {};
        }

        // Runs before m_applied takes `wanted`. A curve decision logs the baselines that caused it. The first apply and
        // a mode switch only name the new setting.
        void log_change(FanCommand wanted)
        {
            const bool curve_decision = m_config.mode == UserMode::Curve && m_applied.has_value();
            if (curve_decision && wanted.mode == FanMode::Custom)
            {
                m_log.write(
                    std::format(
                        "take over: CPU {} C, GPU {} C (baseline) -> {}%",
                        m_status.cpu_baseline_c,
                        m_status.gpu_baseline_c,
                        wanted.percent
                    )
                );
            }
            else if (curve_decision)
            {
                m_log.write(
                    std::format(
                        "hand back to firmware Auto: CPU {} C, GPU {} C (baseline)",
                        m_status.cpu_baseline_c,
                        m_status.gpu_baseline_c
                    )
                );
            }
            else
            {
                m_log.write(std::format("fans: {}", describe(wanted)));
            }
        }

        void read_fan_speeds()
        {
            m_status.cpu_rpm.reset();
            m_status.gpu_rpm.reset();
            const Result<int> cpu_rpm = m_wmi->read(Sensor::CpuFanRpm);
            if (!cpu_rpm)
            {
                return;
            }
            const Result<int> gpu_rpm = m_wmi->read(Sensor::GpuFanRpm);
            if (!gpu_rpm)
            {
                return;
            }
            m_status.cpu_rpm = *cpu_rpm;
            m_status.gpu_rpm = *gpu_rpm;
        }

        void log_status()
        {
            if (m_config.status_log_interval.count() <= 0 || Clock::now() < m_next_status_log)
            {
                return;
            }
            m_next_status_log = Clock::now() + m_config.status_log_interval;
            m_log.write(
                std::format(
                    "status: CPU {} C (base {}), GPU {} C (base {}) | {} {} | fans {}/{} rpm",
                    optional_text(m_status.cpu_c),
                    m_status.cpu_baseline_c,
                    optional_text(m_status.gpu_c),
                    m_status.gpu_baseline_c,
                    mode_name(m_config.mode),
                    describe(m_status.command),
                    optional_text(m_status.cpu_rpm),
                    optional_text(m_status.gpu_rpm)
                )
            );
        }

        // The fans must never stay at a fixed speed after AcerFan stops.
        void restore_auto()
        {
            if (!connect())
            {
                m_log.write("stop: could not restore firmware Auto, no firmware connection");
                return;
            }
            if (const Result<void> restored = m_wmi->apply({}); !restored)
            {
                m_log.write("stop: could not restore firmware Auto: " + restored.error().message());
                return;
            }
            m_log.write("stopped; fans back to firmware Auto");
        }

        void note_error(std::string_view prefix, const Error &error)
        {
            m_status.error = error.message();
            if (m_status.error != m_last_error)
            {
                m_log.write(std::format("{}: {}", prefix, m_status.error));
                m_last_error = m_status.error;
            }
        }

        void pause(std::chrono::milliseconds duration)
        {
            m_service.publish(m_status);
            m_service.wait(m_stop, duration);
        }

        FanService &m_service;
        Logger &m_log;
        std::stop_token m_stop;
        AppConfig m_config;
        FanController m_controller;
        std::optional<AcerWmi> m_wmi;
        // What AcerFan last set. Empty forces a full re-apply.
        std::optional<FanCommand> m_applied;
        FanStatus m_status;
        std::string m_last_error;
        Clock::time_point m_next_reassert;
        Clock::time_point m_next_status_log;
    };

    FanService::FanService(Logger &log, AppConfig config, std::function<void()> on_status)
        : m_log(log), m_on_status(std::move(on_status)), m_initial_config(std::move(config))
    {
        m_status.mode = m_initial_config.mode;
        m_thread = std::jthread([this](std::stop_token stop) { run(std::move(stop)); });
    }

    FanService::~FanService() noexcept
    {
        stop();
    }

    void FanService::stop() noexcept
    {
        if (!m_thread.joinable())
        {
            return;
        }
        m_thread.request_stop();
        try
        {
            m_thread.join();
        }
        catch (const std::system_error &)
        {
            // join() fails only for a self-join or a thread that is gone, and neither leaves work to wait for.
        }
    }

    void FanService::set_mode(UserMode mode) noexcept
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

    void FanService::resumed() noexcept
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

    FanService::Requests FanService::take_requests()
    {
        const std::lock_guard lock(m_mutex);
        return std::exchange(m_requests, {});
    }

    void FanService::publish(const FanStatus &status)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_status = status;
        }
        // The callback runs outside the lock, so it can call status().
        if (m_on_status)
        {
            m_on_status();
        }
    }

    void FanService::wait(const std::stop_token &stop, std::chrono::milliseconds duration)
    {
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, stop, duration, [this]() noexcept { return m_requests.any(); });
    }

    void FanService::run(std::stop_token stop) noexcept
    {
        try
        {
            const ComApartment apartment{COINIT_MULTITHREADED};
            if (const Result<void> joined = apartment.status(); !joined)
            {
                log_unexpected(m_log, joined.error().message());
                return;
            }
            Loop loop{*this, std::move(stop)};
            loop.run();
        }
        catch (const std::exception &error)
        {
            log_unexpected(m_log, error.what());
        }
        catch (...)
        {
            log_unexpected(m_log, "unknown exception");
        }
    }
} // namespace AcerFan
