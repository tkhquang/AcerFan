/**
 * @file fan_controller.hpp
 * @brief Decides the fan mode and speed from temperature readings. Pure logic: no I/O.
 */
#ifndef ACERFAN_FAN_CONTROLLER_HPP
#define ACERFAN_FAN_CONTROLLER_HPP

#include "curve.hpp"

#include <deque>

namespace AcerFan
{
    /// Who runs the fans: the firmware's own Auto curve, or a fixed percentage set by AcerFan.
    enum class FanMode
    {
        firmware_auto,
        custom,
    };

    /// What the fans should be set to. `percent` only matters in FanMode::custom.
    struct FanCommand
    {
        FanMode mode{FanMode::firmware_auto};
        int percent{0};

        friend bool operator==(const FanCommand &, const FanCommand &) = default;
    };

    /// One reading of both chips, in degrees Celsius.
    struct Temperatures
    {
        int cpu_c{};
        int gpu_c{};
    };

    /// Tuning of FanController. Counts are in polls, one poll per on_reading() / on_read_failure() call.
    struct ControllerSettings
    {
        FanCurve cpu_curve;
        FanCurve gpu_curve;
        int baseline_polls{30};          ///< Readings the baseline temperature is taken from.
        double baseline_percentile{0.25}; ///< Baseline = this percentile of those readings.
        int min_samples{5};              ///< Readings needed before taking over from Auto.
        int enter_polls{2};              ///< Polls in a row with a curve request before taking over from Auto.
        int cpu_exit_c{74};              ///< CPU baseline at or below which control may return to Auto ...
        int gpu_exit_c{70};              ///< ... together with the GPU baseline at or below this ...
        int exit_hold_polls{15};         ///< ... for this many polls in a row.
        int up_step_percent{10};         ///< Largest increase per poll.
        int down_step_percent{5};        ///< Largest decrease per poll.
        int down_hold_polls{3};          ///< Every request of this many recent polls must be lower before slowing.
        int min_change_percent{3};       ///< Smaller changes are skipped, except reaching the floor or the ceiling.
        int failsafe_percent{100};       ///< Speed used when temperatures cannot be read ...
        int failsafe_polls{5};           ///< ... for this many polls in a row.
    };

    /**
     * @brief Temperature-driven fan control with hysteresis.
     * @details The curves act on each chip's baseline temperature, a low percentile of its recent readings. Under
     *          light, bursty work a laptop CPU flashes to 85-90 C for a second and drops straight back; fans cannot
     *          help with those flashes, only with heat that stays, and a flash does not move the baseline.
     *
     *          While both baselines are cool the firmware's Auto mode runs the fans. When either baseline enters
     *          its curve, the fans switch to custom speed and follow whichever curve asks for more, never below
     *          the lowest curve point. Speed changes in limited steps; it only falls once every recent request is
     *          lower. Control returns to Auto after both baselines stay at or below their exit temperatures for
     *          exit_hold_polls.
     */
    class FanController
    {
    public:
        explicit FanController(ControllerSettings settings);

        /// Feeds one reading and returns the command for the fans after this poll.
        FanCommand on_reading(Temperatures temps);

        /// Feeds one failed read. After failsafe_polls failures in a row the fans go to failsafe_percent.
        FanCommand on_read_failure();

        /// Back to firmware Auto with empty histories.
        void reset();

        [[nodiscard]] FanCommand command() const noexcept { return m_command; }
        [[nodiscard]] bool in_failsafe() const noexcept { return m_failsafe; }
        [[nodiscard]] int cpu_baseline() const { return baseline(m_cpu); }
        [[nodiscard]] int gpu_baseline() const { return baseline(m_gpu); }
        [[nodiscard]] const ControllerSettings &settings() const noexcept { return m_settings; }

    private:
        [[nodiscard]] int baseline(const std::deque<int> &values) const;
        void update_custom(int demand);

        ControllerSettings m_settings;
        int m_floor_percent;
        int m_ceiling_percent;
        std::deque<int> m_cpu;
        std::deque<int> m_gpu;
        std::deque<int> m_demand;
        FanCommand m_command;
        int m_samples{0}; ///< Readings since construction or reset(); the windows above are capped, this is not.
        int m_hot_polls{0};
        int m_cool_polls{0};
        int m_failed_polls{0};
        bool m_failsafe{false};
    };
} // namespace AcerFan

#endif // ACERFAN_FAN_CONTROLLER_HPP
