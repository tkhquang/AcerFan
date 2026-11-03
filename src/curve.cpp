#include "curve.hpp"

#include "text.hpp"

#include <cmath>
#include <cstddef>
#include <format>

namespace AcerFan
{
    namespace
    {
        constexpr int MIN_PERCENT = 1;
        constexpr int MAX_PERCENT = 100;
        constexpr std::size_t MIN_POINTS = 2;
    } // namespace

    int FanCurve::percent_at(double temp_c) const noexcept
    {
        if (m_points.empty() || temp_c < m_points.front().temp_c)
        {
            return 0;
        }
        for (std::size_t i = 1; i < m_points.size(); ++i)
        {
            const CurvePoint &upper = m_points[i];
            if (temp_c <= upper.temp_c)
            {
                const CurvePoint &lower = m_points[i - 1];
                const double fraction = (temp_c - lower.temp_c) / static_cast<double>(upper.temp_c - lower.temp_c);
                return static_cast<int>(std::lround(lower.percent + fraction * (upper.percent - lower.percent)));
            }
        }
        return m_points.back().percent;
    }

    int FanCurve::floor_percent() const noexcept
    {
        return m_points.empty() ? 0 : m_points.front().percent;
    }

    int FanCurve::ceiling_percent() const noexcept
    {
        return m_points.empty() ? 0 : m_points.back().percent;
    }

    std::string FanCurve::to_string() const
    {
        std::string text;
        for (const CurvePoint &point : m_points)
        {
            if (!text.empty())
            {
                text += ", ";
            }
            text += std::format("{}:{}", point.temp_c, point.percent);
        }
        return text;
    }

    std::optional<FanCurve> parse_curve(std::string_view text)
    {
        std::vector<CurvePoint> points;
        while (!trim(text).empty())
        {
            const auto comma = text.find(',');
            const std::string_view item = text.substr(0, comma);
            text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);

            const auto colon = item.find(':');
            if (colon == std::string_view::npos)
            {
                return std::nullopt;
            }
            const std::optional<int> temp_c = parse_int(item.substr(0, colon));
            const std::optional<int> percent = parse_int(item.substr(colon + 1));
            if (!temp_c || !percent || *percent < MIN_PERCENT || *percent > MAX_PERCENT)
            {
                return std::nullopt;
            }
            if (!points.empty() && (*temp_c <= points.back().temp_c || *percent < points.back().percent))
            {
                return std::nullopt;
            }
            points.push_back({*temp_c, *percent});
        }
        if (points.size() < MIN_POINTS)
        {
            return std::nullopt;
        }
        return FanCurve{std::move(points)};
    }
} // namespace AcerFan
