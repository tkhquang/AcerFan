/**
 * @file curve.hpp
 * @brief The piecewise-linear map from a temperature to a fan speed.
 */
#ifndef ACERFAN_CURVE_HPP
#define ACERFAN_CURVE_HPP

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AcerFan
{
    /**
     * @brief One point of a FanCurve.
     */
    struct CurvePoint
    {
        /// Temperature in degrees Celsius.
        int temp_c{0};
        /// Fan speed in percent at `temp_c`.
        int percent{0};

        /// Equal when both fields are equal.
        friend bool operator==(const CurvePoint &, const CurvePoint &) = default;
    };

    /**
     * @brief Maps a temperature to a fan percent, linear between the points.
     * @details Below the first point the curve asks for 0, which leaves the fans to the Auto mode of the firmware.
     *          Above the last point it asks for the percent of the last point.
     */
    class FanCurve
    {
    public:
        /** @brief Creates an empty curve that always asks for 0. */
        FanCurve() = default;

        /**
         * @brief Takes points that parse_curve() accepted.
         * @details The points must number two or more, with strictly increasing temperatures.
         */
        explicit FanCurve(std::vector<CurvePoint> points) noexcept : m_points(std::move(points)) {}

        /** @brief Returns the fan percent at `temp_c`, 0 below the first point. */
        [[nodiscard]] int percent_at(double temp_c) const noexcept;

        /** @brief Returns the percent of the first point, the lowest speed that the curve asks for. */
        [[nodiscard]] int floor_percent() const noexcept;

        /** @brief Returns the percent of the last point, the highest speed that the curve asks for. */
        [[nodiscard]] int ceiling_percent() const noexcept;

        /** @brief Formats the curve in the "80:40, 84:60" form that parse_curve() reads. */
        [[nodiscard]] std::string to_string() const;

        /// Returns the points of the curve.
        [[nodiscard]] const std::vector<CurvePoint> &points() const noexcept { return m_points; }

        /// Equal when both curves have the same points.
        friend bool operator==(const FanCurve &, const FanCurve &) = default;

    private:
        std::vector<CurvePoint> m_points;
    };

    /**
     * @brief Parses a curve in the form "80:40, 84:60, 87:80, 89:100", each point as temperature:percent.
     * @return The curve, or std::nullopt unless it has two or more points, the temperatures strictly increase, and the
     *         percentages lie in 1..100 and never decrease.
     */
    [[nodiscard]] std::optional<FanCurve> parse_curve(std::string_view text);
} // namespace AcerFan

#endif // ACERFAN_CURVE_HPP
