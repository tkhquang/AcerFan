/**
 * @file tests/test_controller.cpp
 * @brief FanCurve parsing and the FanController decisions.
 */
#include "check.hpp"

#include "curve.hpp"
#include "fan_controller.hpp"

#include <array>
#include <string_view>

using namespace AcerFan;

namespace
{
    constexpr std::string_view CPU_CURVE = "80:40, 84:60, 87:80, 89:100";
    constexpr std::string_view GPU_CURVE = "75:40, 80:60, 84:80, 86:100";

    // Light, bursty work as logged on the ConceptD 9: the CPU flashes to 85-92 C and drops straight back.
    constexpr std::array SPIKY_CPU_C{
        85,
        71,
        87,
        63,
        56,
        88,
        60,
        92,
        66,
        54,
    };

    ControllerSettings default_settings()
    {
        ControllerSettings settings;
        settings.cpu_curve = *parse_curve(CPU_CURVE);
        settings.gpu_curve = *parse_curve(GPU_CURVE);
        return settings;
    }

    // Settings whose baseline is the latest reading, so the steps show without the percentile window.
    ControllerSettings instant_settings()
    {
        ControllerSettings settings = default_settings();
        settings.baseline_polls = 1;
        return settings;
    }

    FanCommand step(FanController &controller, Temperatures temperatures)
    {
        controller.on_reading(temperatures);
        return controller.command();
    }

    FanCommand feed(FanController &controller, Temperatures temperatures, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            controller.on_reading(temperatures);
        }
        return controller.command();
    }

    FanCommand fail_read(FanController &controller) noexcept
    {
        controller.on_read_failure();
        return controller.command();
    }

    void test_curve()
    {
        const auto curve = parse_curve(CPU_CURVE);
        CHECK(curve.has_value());
        CHECK(curve->percent_at(79.9) == 0);
        CHECK(curve->percent_at(80) == 40);
        CHECK(curve->percent_at(82) == 50);
        CHECK(curve->percent_at(88) == 90);
        CHECK(curve->percent_at(95) == 100);
        CHECK(curve->floor_percent() == 40);
        CHECK(curve->ceiling_percent() == 100);
        CHECK(curve->to_string() == CPU_CURVE);

        // One point, temperatures not increasing, a decreasing percent, a percent out of range, not a number, empty.
        CHECK(!parse_curve("80:40").has_value());
        CHECK(!parse_curve("84:60, 80:40").has_value());
        CHECK(!parse_curve("80:60, 84:40").has_value());
        CHECK(!parse_curve("80:40, 84:140").has_value());
        CHECK(!parse_curve("80:40, x:60").has_value());
        CHECK(!parse_curve("").has_value());
        // Spaces around the numbers are fine.
        CHECK(parse_curve(" 70 : 30 ,90:100 ").has_value());
    }

    void test_cool_stays_auto()
    {
        FanController controller{default_settings()};
        CHECK(feed(controller, {60, 50}, 40).mode == FanMode::FirmwareAuto);
    }

    void test_spikes_ignored()
    {
        FanController controller{default_settings()};
        for (int repeat = 0; repeat < 10; ++repeat)
        {
            for (const int cpu_c : SPIKY_CPU_C)
            {
                CHECK(step(controller, {cpu_c, 52}).mode == FanMode::FirmwareAuto);
            }
        }
        CHECK(controller.cpu_baseline() < 80);
    }

    void test_sustained_heat_takes_over()
    {
        FanController controller{default_settings()};
        // A take-over waits for one full baseline window of 60 readings.
        CHECK(feed(controller, {88, 50}, 59).mode == FanMode::FirmwareAuto);
        const FanCommand command = step(controller, {88, 50});
        CHECK(command.mode == FanMode::Custom);
        CHECK(command.percent == 90);
    }

    void test_logon_burst_stays_auto()
    {
        // The CPU runs hot for the first 30 seconds after logon, then cools down.
        FanController controller{default_settings()};
        CHECK(feed(controller, {92, 60}, 15).mode == FanMode::FirmwareAuto);
        CHECK(feed(controller, {62, 55}, 60).mode == FanMode::FirmwareAuto);
    }

    void test_short_burst_ignored()
    {
        FanController controller{default_settings()};
        feed(controller, {62, 55}, 60);
        // One minute of heat fills half of the two-minute window, which leaves the 25th percentile cool.
        CHECK(feed(controller, {90, 55}, 30).mode == FanMode::FirmwareAuto);
        // Heat that stays for over 90 seconds moves the baseline into the curve.
        CHECK(feed(controller, {90, 55}, 20).mode == FanMode::Custom);
    }

    void test_gpu_alone_takes_over()
    {
        FanController controller{default_settings()};
        const FanCommand command = feed(controller, {60, 80}, 60);
        CHECK(command.mode == FanMode::Custom);
        CHECK(command.percent == 60);
    }

    void test_rise_is_stepped()
    {
        FanController controller{instant_settings()};
        CHECK(feed(controller, {80, 50}, 5) == (FanCommand{FanMode::Custom, 40}));
        CHECK(step(controller, {95, 50}).percent == 50);
        CHECK(step(controller, {95, 50}).percent == 60);
        CHECK(feed(controller, {95, 50}, 4).percent == 100);
    }

    void test_fall_and_hand_back()
    {
        FanController controller{instant_settings()};
        feed(controller, {80, 50}, 5);
        feed(controller, {95, 50}, 6);
        CHECK(controller.command().percent == 100);

        // Cool readings: two polls hold because recent requests are still high, then 5% per poll, Auto after 15.
        CHECK(feed(controller, {70, 50}, 2).percent == 100);
        CHECK(step(controller, {70, 50}).percent == 95);
        CHECK(feed(controller, {70, 50}, 7).percent == 60);
        CHECK(feed(controller, {70, 50}, 4) == (FanCommand{FanMode::Custom, 40}));
        CHECK(step(controller, {70, 50}).mode == FanMode::FirmwareAuto);
    }

    void test_warm_but_not_cool_keeps_floor()
    {
        // Between the exit temperature and the curve start the fans stay at the floor and do not flap.
        FanController controller{instant_settings()};
        feed(controller, {80, 50}, 5);
        CHECK(feed(controller, {77, 50}, 60) == (FanCommand{FanMode::Custom, 40}));
    }

    void test_failsafe()
    {
        FanController controller{default_settings()};
        for (int i = 0; i < 4; ++i)
        {
            CHECK(fail_read(controller).mode == FanMode::FirmwareAuto);
        }
        CHECK(fail_read(controller) == (FanCommand{FanMode::Custom, 100}));
        CHECK(controller.in_failsafe());

        controller.on_reading({60, 50});
        CHECK(!controller.in_failsafe());
        // The speed ramps down from the fail-safe speed by the normal rules.
        CHECK(controller.command().mode == FanMode::Custom);
    }

    void test_reset()
    {
        FanController controller{default_settings()};
        CHECK(feed(controller, {95, 50}, 60).mode == FanMode::Custom);
        controller.reset();
        CHECK(controller.command().mode == FanMode::FirmwareAuto);
        // The baseline window fills again from zero.
        CHECK(feed(controller, {95, 50}, 59).mode == FanMode::FirmwareAuto);
        CHECK(step(controller, {95, 50}).mode == FanMode::Custom);
    }
} // namespace

int main()
{
    test_curve();
    test_cool_stays_auto();
    test_spikes_ignored();
    test_sustained_heat_takes_over();
    test_logon_burst_stays_auto();
    test_short_burst_ignored();
    test_gpu_alone_takes_over();
    test_rise_is_stepped();
    test_fall_and_hand_back();
    test_warm_but_not_cool_keeps_floor();
    test_failsafe();
    test_reset();
    return AcerFan::tests::finish("test_controller");
}
