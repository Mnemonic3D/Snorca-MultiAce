#include "slic3r/Utils/SnorcaVibrationCalibration.hpp"

#include <cassert>
#include <iostream>
#include <string_view>

using nlohmann::json;
using Slic3r::SnorcaVibrationCalibration::Result;
using Slic3r::SnorcaVibrationCalibration::run;

static json state(int machine, int action, const char* print, bool active, const char* shaper, const char* firmware = "ready")
{
    return {{"result",
             {{"status",
               {{"machine_state_manager", {{"main_state", machine}, {"action_code", action}}},
                {"print_stats", {{"state", print}}},
                {"virtual_sdcard", {{"is_active", active}}},
                {"resonance_tester", {{"state", shaper}}},
                {"webhooks", {{"state", firmware}}}}}}}};
}

int main()
{
    const auto idle = state(0, 0, "standby", false, "idle");
    for (const auto& busy : {state(1, 0, "printing", true, "idle"), state(0, 0, "paused", false, "idle"),
                             state(0, 1, "standby", false, "idle"), state(0, 0, "standby", true, "idle"),
                             state(5, 384, "standby", false, "shaper_calibrating"), state(0, 0, "standby", false, "idle", "shutdown")}) {
        int calls = 0;
        assert(run([&](const char*, const json* body, long) {
                   assert(!body);
                   ++calls;
                   return busy;
               }) == Result::busy);
        assert(calls == 1);
    }
    assert(run([](const char*, const json* body, long) {
               assert(!body);
               return json{};
           }) == Result::unavailable);

    for (Result expected : {Result::completed, Result::unverified, Result::unavailable}) {
        int        queries = 0, posts = 0;
        const auto result = run([&](std::string_view path, const json* body, long timeout) -> json {
            if (path == "printer/gcode/help") {
                assert(!body);
                return {{"result", expected == Result::unavailable ? json::object() : json{{"SHAPER_CALIBRATE", "G-code macro"}}}};
            }
            if (path == "printer/gcode/script") {
                assert(body && body->at("script") == "SET_MAIN_STATE MAIN_STATE=SHAPER_CALIBRATE\nSHAPER_CALIBRATE" && timeout == 600);
                ++posts;
                return {{"result", "ok"}};
            }
            assert(!body && path == Slic3r::SnorcaVibrationCalibration::status_path);
            if (++queries == 1)
                return idle;
            return state(0, 0, "standby", false, expected == Result::completed ? "completed" : "failed");
        });
        assert(result == expected);
        assert(posts == (expected == Result::unavailable ? 0 : 1));
    }

    int attempted = 0;
    assert(run([&](std::string_view path, const json* body, long) -> json {
               if (path == "printer/gcode/help")
                   return {{"result", {{"SHAPER_CALIBRATE", "macro"}}}};
               if (body) {
                   assert(++attempted == 1);
                   return {}; // Lost reply after submission; never resend.
               }
               return idle;
           }) == Result::unverified);
    assert(attempted == 1);
    std::cout << "PASS: idle-only guard, unsupported firmware, failed calibration, completed calibration and lost reply without retry\n";
}
