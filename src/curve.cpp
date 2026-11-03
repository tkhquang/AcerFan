#include "curve.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <utility>

namespace AcerFan
{
    namespace
    {
        constexpr int max_percent = 100;

        std::string_view trim(std::string_view text) noexcept
        {
            const auto first = text.find_first_not_of(" \t");
            if (first == std::string_view::npos)
            {
                return {};
            }
            const auto last = text.find_last_not_of(" \t");
            return text.substr(first, last - first + 1);
        }

        std::optional<int> parse_int(std::string_view text) noexcept
        {
            text = trim(text);
            int value{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                return std::nullopt;
            }
            return value;
        }
    } // namespace

    int FanCurve::percent_at(double temp_c) const noexcept
    {
        if (m_points.empty() || temp_c < m_points.front().temp_c)
        {
            return 0;
        }
        for (std::size_t i = 1; i < m_points.size(); ++i)
        {
            const CurvePoint &hi = m_points[i];
            if (temp_c <= hi.temp_c)
            {
                const CurvePoint &lo = m_points[i - 1];
                const double t = (temp_c - lo.temp_c) / static_cast<double>(hi.temp_c - lo.temp_c);
                return static_cast<int>(std::lround(lo.percent + t * (hi.percent - lo.percent)));
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
            const std::string_view item = trim(text.substr(0, comma));
            text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);

            const auto colon = item.find(':');
            if (colon == std::string_view::npos)
            {
                return std::nullopt;
            }
            const auto temp = parse_int(item.substr(0, colon));
            const auto percent = parse_int(item.substr(colon + 1));
            if (!temp || !percent || *percent < 1 || *percent > max_percent)
            {
                return std::nullopt;
            }
            if (!points.empty() && (*temp <= points.back().temp_c || *percent < points.back().percent))
            {
                return std::nullopt;
            }
            points.push_back({*temp, *percent});
        }
        if (points.size() < 2)
        {
            return std::nullopt;
        }
        return FanCurve{std::move(points)};
    }
} // namespace AcerFan
