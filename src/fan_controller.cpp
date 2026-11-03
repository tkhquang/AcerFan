#include "fan_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace AcerFan
{
    namespace
    {
        void push_bounded(std::deque<int> &history, int value, int limit)
        {
            history.push_back(value);
            while (std::cmp_greater(history.size(), std::max(limit, 1)))
            {
                history.pop_front();
            }
        }
    } // namespace

    FanController::FanController(ControllerSettings settings)
        : m_settings(std::move(settings)),
          m_floor_percent(std::min(m_settings.cpu_curve.floor_percent(), m_settings.gpu_curve.floor_percent())),
          m_ceiling_percent(std::max(m_settings.cpu_curve.ceiling_percent(), m_settings.gpu_curve.ceiling_percent()))
    {
    }

    int FanController::baseline(const std::deque<int> &history) const
    {
        if (history.empty())
        {
            return 0;
        }
        std::vector<int> sorted(history.begin(), history.end());
        const double fraction = std::clamp(m_settings.baseline_percentile, 0.0, 1.0);
        const auto index = static_cast<std::size_t>(std::floor(static_cast<double>(sorted.size() - 1) * fraction));
        std::ranges::nth_element(sorted, sorted.begin() + static_cast<std::ptrdiff_t>(index));
        return sorted[index];
    }

    void FanController::on_reading(Temperatures temperatures)
    {
        m_failed_polls = 0;
        m_failsafe = false;
        if (m_samples < m_settings.baseline_polls)
        {
            ++m_samples;
        }
        push_bounded(m_cpu_history, temperatures.cpu_c, m_settings.baseline_polls);
        push_bounded(m_gpu_history, temperatures.gpu_c, m_settings.baseline_polls);

        const int cpu = cpu_baseline();
        const int gpu = gpu_baseline();
        const int demand = std::max(m_settings.cpu_curve.percent_at(cpu), m_settings.gpu_curve.percent_at(gpu));
        push_bounded(m_demand_history, demand, m_settings.down_hold_polls);

        if (m_command.mode == FanMode::FirmwareAuto)
        {
            m_hot_polls = demand > 0 ? m_hot_polls + 1 : 0;
            if (m_hot_polls >= m_settings.enter_polls && m_samples >= m_settings.baseline_polls)
            {
                m_command = {FanMode::Custom, std::max(demand, m_floor_percent)};
                m_cool_polls = 0;
            }
            return;
        }

        const bool cool = cpu <= m_settings.cpu_exit_c && gpu <= m_settings.gpu_exit_c;
        m_cool_polls = cool ? m_cool_polls + 1 : 0;
        if (m_cool_polls >= m_settings.exit_hold_polls)
        {
            m_command = {};
            m_hot_polls = 0;
            m_cool_polls = 0;
            return;
        }
        update_custom(demand);
    }

    void FanController::update_custom(int demand) noexcept
    {
        const int current = m_command.percent;
        const int wanted = std::max(demand, m_floor_percent);
        int target = current;
        if (wanted > current)
        {
            target = std::min(wanted, current + m_settings.up_step_percent);
        }
        else if (std::cmp_greater_equal(m_demand_history.size(), m_settings.down_hold_polls))
        {
            const int recent_peak = std::max(*std::ranges::max_element(m_demand_history), m_floor_percent);
            if (recent_peak < current)
            {
                target = std::max(recent_peak, current - m_settings.down_step_percent);
            }
        }

        const bool big_enough = std::abs(target - current) >= m_settings.min_change_percent;
        const bool at_limit = target == m_floor_percent || target >= m_ceiling_percent;
        if (target != current && (big_enough || at_limit))
        {
            m_command.percent = target;
        }
    }

    void FanController::on_read_failure() noexcept
    {
        ++m_failed_polls;
        if (m_failed_polls >= m_settings.failsafe_polls)
        {
            m_command = {FanMode::Custom, m_settings.failsafe_percent};
            m_failsafe = true;
            m_cool_polls = 0;
        }
    }

    void FanController::reset() noexcept
    {
        m_cpu_history.clear();
        m_gpu_history.clear();
        m_demand_history.clear();
        m_command = {};
        m_samples = 0;
        m_hot_polls = 0;
        m_cool_polls = 0;
        m_failed_polls = 0;
        m_failsafe = false;
    }
} // namespace AcerFan
