/**
 * @file tests/controller_tests.cpp
 * @brief FanCurve parsing and FanController decisions.
 */
#include "check.hpp"

#include "curve.hpp"
#include "fan_controller.hpp"

#include <initializer_list>

using namespace AcerFan;

namespace
{
    ControllerSettings default_settings()
    {
        ControllerSettings s;
        s.cpu_curve = *parse_curve("80:40, 84:60, 87:80, 89:100");
        s.gpu_curve = *parse_curve("75:40, 80:60, 84:80, 86:100");
        return s;
    }

    /// Settings whose baseline is the latest reading, to test stepping without the percentile window.
    ControllerSettings instant_settings()
    {
        ControllerSettings s = default_settings();
        s.baseline_polls = 1;
        return s;
    }

    FanCommand feed(FanController &controller, Temperatures temps, int count)
    {
        FanCommand last = controller.command();
        for (int i = 0; i < count; ++i)
        {
            last = controller.on_reading(temps);
        }
        return last;
    }

    void test_curve()
    {
        const auto curve = parse_curve("80:40, 84:60, 87:80, 89:100");
        CHECK(curve.has_value());
        CHECK(curve->percent_at(79.9) == 0);
        CHECK(curve->percent_at(80) == 40);
        CHECK(curve->percent_at(82) == 50);
        CHECK(curve->percent_at(88) == 90);
        CHECK(curve->percent_at(95) == 100);
        CHECK(curve->floor_percent() == 40);
        CHECK(curve->ceiling_percent() == 100);
        CHECK(curve->to_string() == "80:40, 84:60, 87:80, 89:100");

        CHECK(!parse_curve("80:40").has_value());              // one point
        CHECK(!parse_curve("84:60, 80:40").has_value());       // temperatures not increasing
        CHECK(!parse_curve("80:60, 84:40").has_value());       // percent decreasing
        CHECK(!parse_curve("80:40, 84:140").has_value());      // percent out of range
        CHECK(!parse_curve("80:40, x:60").has_value());        // not a number
        CHECK(!parse_curve("").has_value());
        CHECK(parse_curve(" 70 : 30 ,90:100 ").has_value()); // spaces are fine
    }

    void test_cool_stays_auto()
    {
        FanController controller{default_settings()};
        CHECK(feed(controller, {60, 50}, 40).mode == FanMode::firmware_auto);
    }

    void test_spikes_ignored()
    {
        // Light, bursty work as logged on the ConceptD 9: the CPU flashes to 85-92 C and drops straight back.
        FanController controller{default_settings()};
        for (int round = 0; round < 10; ++round)
        {
            for (const int cpu : {85, 71, 87, 63, 56, 88, 60, 92, 66, 54})
            {
                CHECK(controller.on_reading({cpu, 52}).mode == FanMode::firmware_auto);
            }
        }
        CHECK(controller.cpu_baseline() < 80);
    }

    void test_sustained_heat_takes_over()
    {
        FanController controller{default_settings()};
        CHECK(feed(controller, {88, 50}, 4).mode == FanMode::firmware_auto); // not enough samples yet
        const FanCommand command = controller.on_reading({88, 50});
        CHECK(command.mode == FanMode::custom);
        CHECK(command.percent == 90);
    }

    void test_gpu_alone_takes_over()
    {
        FanController controller{default_settings()};
        const FanCommand command = feed(controller, {60, 80}, 5);
        CHECK(command.mode == FanMode::custom);
        CHECK(command.percent == 60);
    }

    void test_rise_is_stepped()
    {
        FanController controller{instant_settings()};
        CHECK(feed(controller, {80, 50}, 5) == (FanCommand{FanMode::custom, 40}));
        CHECK(controller.on_reading({95, 50}).percent == 50);
        CHECK(controller.on_reading({95, 50}).percent == 60);
        CHECK(feed(controller, {95, 50}, 4).percent == 100);
    }

    void test_fall_and_hand_back()
    {
        FanController controller{instant_settings()};
        feed(controller, {80, 50}, 5);
        feed(controller, {95, 50}, 6);
        CHECK(controller.command().percent == 100);

        // Cool readings: two polls hold (recent requests still high), then 5% per poll; Auto after 15 cool polls.
        CHECK(feed(controller, {70, 50}, 2).percent == 100);
        CHECK(controller.on_reading({70, 50}).percent == 95);
        CHECK(feed(controller, {70, 50}, 7).percent == 60);
        CHECK(feed(controller, {70, 50}, 4) == (FanCommand{FanMode::custom, 40}));
        CHECK(controller.on_reading({70, 50}).mode == FanMode::firmware_auto);
    }

    void test_warm_but_not_cool_keeps_floor()
    {
        // Between the exit temperature and the curve start the fans stay at the floor instead of flapping.
        FanController controller{instant_settings()};
        feed(controller, {80, 50}, 5);
        CHECK(feed(controller, {77, 50}, 60) == (FanCommand{FanMode::custom, 40}));
    }

    void test_failsafe()
    {
        FanController controller{default_settings()};
        for (int i = 0; i < 4; ++i)
        {
            CHECK(controller.on_read_failure().mode == FanMode::firmware_auto);
        }
        CHECK(controller.on_read_failure() == (FanCommand{FanMode::custom, 100}));
        CHECK(controller.in_failsafe());

        controller.on_reading({60, 50});
        CHECK(!controller.in_failsafe());
        CHECK(controller.command().mode == FanMode::custom); // ramps down normally from here
    }

    void test_reset()
    {
        FanController controller{instant_settings()};
        feed(controller, {95, 50}, 5);
        CHECK(controller.command().mode == FanMode::custom);
        controller.reset();
        CHECK(controller.command().mode == FanMode::firmware_auto);
        CHECK(feed(controller, {95, 50}, 4).mode == FanMode::firmware_auto); // min_samples counts again
    }
} // namespace

int main()
{
    test_curve();
    test_cool_stays_auto();
    test_spikes_ignored();
    test_sustained_heat_takes_over();
    test_gpu_alone_takes_over();
    test_rise_is_stepped();
    test_fall_and_hand_back();
    test_warm_but_not_cool_keeps_floor();
    test_failsafe();
    test_reset();
    return AcerFan::tests::finish("controller_tests");
}
