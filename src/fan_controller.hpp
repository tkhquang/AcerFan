/**
 * @file fan_controller.hpp
 * @brief The decision of the fan mode and speed from temperature readings, without I/O.
 */
#ifndef ACERFAN_FAN_CONTROLLER_HPP
#define ACERFAN_FAN_CONTROLLER_HPP

#include "curve.hpp"

#include <deque>

namespace AcerFan
{
    /**
     * @brief The owner of the fan speed.
     */
    enum class FanMode
    {
        /// The Auto curve of the firmware runs the fans.
        FirmwareAuto,
        /// AcerFan holds the fans at FanCommand::percent.
        Custom,
    };

    /**
     * @brief The setting of both fans.
     */
    struct FanCommand
    {
        /// Names the owner of the fan speed.
        FanMode mode{FanMode::FirmwareAuto};
        /// Holds the fan speed in percent. Only FanMode::Custom uses it.
        int percent{0};

        /// Equal when both fields are equal.
        friend bool operator==(const FanCommand &, const FanCommand &) = default;
    };

    /**
     * @brief One reading of both chips.
     */
    struct Temperatures
    {
        /// CPU temperature in degrees Celsius.
        int cpu_c{0};
        /// GPU temperature in degrees Celsius.
        int gpu_c{0};
    };

    /**
     * @brief The tuning of FanController.
     * @details A count of polls counts calls of FanController::on_reading() and FanController::on_read_failure().
     */
    struct ControllerSettings
    {
        /// Maps the CPU baseline temperature to a fan percent.
        FanCurve cpu_curve;
        /// Maps the GPU baseline temperature to a fan percent.
        FanCurve gpu_curve;
        /// Number of recent readings in the baseline window.
        int baseline_polls{30};
        /// Percentile of the baseline window, 0 to 1, that forms the baseline.
        double baseline_percentile{0.25};
        /// Number of readings that must exist before AcerFan takes over from Auto.
        int min_samples{5};
        /// Number of consecutive polls with a curve request before AcerFan takes over from Auto.
        int enter_polls{2};
        /// CPU baseline at or below which control can return to Auto.
        int cpu_exit_c{74};
        /// GPU baseline at or below which control can return to Auto.
        int gpu_exit_c{70};
        /// Number of consecutive polls with both baselines at or below their exit temperatures before control returns.
        int exit_hold_polls{15};
        /// Largest speed increase per poll, in percent.
        int up_step_percent{10};
        /// Largest speed decrease per poll, in percent.
        int down_step_percent{5};
        /// Number of recent requests that must all be lower before the speed falls.
        int down_hold_polls{3};
        /// Smallest speed change in percent. A step to the floor or the ceiling is exempt.
        int min_change_percent{3};
        /// Fan speed in percent while the temperatures stay unreadable.
        int failsafe_percent{100};
        /// Number of consecutive failed reads before the fail-safe speed applies.
        int failsafe_polls{5};

        /// Equal when every field is equal.
        friend bool operator==(const ControllerSettings &, const ControllerSettings &) = default;
    };

    /**
     * @brief Temperature-driven fan control with hysteresis.
     * @details The curves act on the baseline of each chip, a low percentile of its recent readings. Under light,
     *          bursty work a laptop CPU flashes to 85-90 C for a second and drops straight back. Fans cannot help with
     *          those flashes, only with heat that stays, and a flash does not move the baseline.
     *
     *          While both baselines stay cool, the Auto mode of the firmware runs the fans. When either baseline
     *          enters its curve, both fans switch to a custom speed and follow the curve that asks for more, never
     *          below the lowest curve point. The speed changes in limited steps and falls only after every recent
     *          request is lower. Control returns to Auto after both baselines stay at or below their exit temperatures
     *          for ControllerSettings::exit_hold_polls polls.
     */
    class FanController
    {
    public:
        /** @brief Starts in FanMode::FirmwareAuto with empty histories. */
        explicit FanController(ControllerSettings settings);

        /** @brief Adds one reading and updates command(). */
        void on_reading(Temperatures temperatures);

        /**
         * @brief Counts one failed read and updates command().
         * @details After ControllerSettings::failsafe_polls consecutive failures the fans go to
         *          ControllerSettings::failsafe_percent. The next reading ends the fail-safe.
         */
        void on_read_failure() noexcept;

        /** @brief Returns to FanMode::FirmwareAuto and clears every history. */
        void reset() noexcept;

        /// Returns the fan setting after the latest poll.
        [[nodiscard]] FanCommand command() const noexcept { return m_command; }

        /// Returns true while the fail-safe speed applies.
        [[nodiscard]] bool in_failsafe() const noexcept { return m_failsafe; }

        /** @brief Returns the CPU baseline temperature in degrees Celsius, 0 before the first reading. */
        [[nodiscard]] int cpu_baseline() const { return baseline(m_cpu_history); }

        /** @brief Returns the GPU baseline temperature in degrees Celsius, 0 before the first reading. */
        [[nodiscard]] int gpu_baseline() const { return baseline(m_gpu_history); }

    private:
        [[nodiscard]] int baseline(const std::deque<int> &history) const;
        void update_custom(int demand) noexcept;

        ControllerSettings m_settings;
        int m_floor_percent{0};
        int m_ceiling_percent{0};
        std::deque<int> m_cpu_history;
        std::deque<int> m_gpu_history;
        std::deque<int> m_demand_history;
        FanCommand m_command{};
        // Readings since construction or reset(), capped at ControllerSettings::min_samples.
        int m_samples{0};
        int m_hot_polls{0};
        int m_cool_polls{0};
        int m_failed_polls{0};
        bool m_failsafe{false};
    };
} // namespace AcerFan

#endif // ACERFAN_FAN_CONTROLLER_HPP
