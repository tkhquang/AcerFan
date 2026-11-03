/**
 * @file curve.hpp
 * @brief Piecewise-linear temperature to fan-speed curve.
 */
#ifndef ACERFAN_CURVE_HPP
#define ACERFAN_CURVE_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AcerFan
{
    /// One curve point: at `temp_c` degrees Celsius the fans run at `percent`.
    struct CurvePoint
    {
        int temp_c{};
        int percent{};
    };

    /**
     * @brief Maps a temperature to a fan percentage, linear between points.
     * @details Below the first point the curve asks for nothing (0), which leaves the fans to the firmware's own
     *          Auto mode. Above the last point it asks for the last point's percentage.
     */
    class FanCurve
    {
    public:
        FanCurve() = default;

        /// Takes points already validated by parse_curve(): two or more, temperatures strictly increasing.
        explicit FanCurve(std::vector<CurvePoint> points) : m_points(std::move(points)) {}

        /// Fan percentage requested at `temp_c`; 0 below the first point.
        [[nodiscard]] int percent_at(double temp_c) const noexcept;

        /// Percentage of the first point, the lowest speed the curve ever asks for.
        [[nodiscard]] int floor_percent() const noexcept;

        /// Percentage of the last point, the highest speed the curve ever asks for.
        [[nodiscard]] int ceiling_percent() const noexcept;

        /// The "80:40, 84:60" form parse_curve() reads.
        [[nodiscard]] std::string to_string() const;

        [[nodiscard]] const std::vector<CurvePoint> &points() const noexcept { return m_points; }

    private:
        std::vector<CurvePoint> m_points;
    };

    /**
     * @brief Parses a curve written as "80:40, 84:60, 87:80, 89:100" (temperature : percent).
     * @return The curve, or std::nullopt unless there are at least two points, temperatures strictly increase,
     *         percentages lie in 1..100 and never decrease.
     */
    [[nodiscard]] std::optional<FanCurve> parse_curve(std::string_view text);
} // namespace AcerFan

#endif // ACERFAN_CURVE_HPP
