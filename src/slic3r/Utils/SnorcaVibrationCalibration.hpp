#ifndef slic3r_SnorcaVibrationCalibration_hpp_
#define slic3r_SnorcaVibrationCalibration_hpp_

#include "nlohmann/json.hpp"

namespace Slic3r::SnorcaVibrationCalibration {
enum class Result { unavailable, busy, unverified, completed };

inline constexpr const char* status_path =
    "printer/objects/query?machine_state_manager&resonance_tester&print_stats&virtual_sdcard&webhooks";

template<class Request> Result run(Request request)
{
    using nlohmann::json;
    bool submitted = false;
    try {
        const json  before         = request(status_path, nullptr, 5);
        const auto& status         = before.at("result").at("status");
        const auto& machine        = status.at("machine_state_manager");
        const auto& main_state     = machine.at("main_state");
        const auto& action         = machine.at("action_code");
        const auto& active         = status.at("virtual_sdcard").at("is_active");
        const auto& print_state    = status.at("print_stats").at("state");
        const auto& shaper_state   = status.at("resonance_tester").at("state");
        const auto& firmware_state = status.at("webhooks").at("state");
        if (!main_state.is_number_integer() || !action.is_number_integer() || !active.is_boolean() || !print_state.is_string() ||
            !shaper_state.is_string() || !firmware_state.is_string())
            return Result::unavailable;
        if (main_state != 0 || action != 0 || active != false || firmware_state != "ready" ||
            (print_state != "standby" && print_state != "complete" && print_state != "cancelled") ||
            (shaper_state != "idle" && shaper_state != "completed" && shaper_state != "failed"))
            return Result::busy;

        const json help = request("printer/gcode/help", nullptr, 5);
        if (!help.at("result").contains("SHAPER_CALIBRATE"))
            return Result::unavailable;

        // Snapmaker's state manager permits entry into SHAPER_CALIBRATE only
        // from idle. Send the reservation and calibration as one gcode script.
        const json script = {{"script", "SET_MAIN_STATE MAIN_STATE=SHAPER_CALIBRATE\nSHAPER_CALIBRATE"}};
        submitted         = true;
        const json reply  = request("printer/gcode/script", &script, 600);
        if (reply.at("result") != "ok")
            return Result::unverified;

        // Moonraker returns after execution. Check the actual result as well:
        // an HTTP response alone cannot confirm successful calibration.
        const json  after        = request(status_path, nullptr, 5);
        const auto& final_status = after.at("result").at("status");
        if (final_status.at("resonance_tester").at("state") == "completed" &&
            final_status.at("machine_state_manager").at("main_state") == 0 &&
            final_status.at("machine_state_manager").at("action_code") == 0)
            return Result::completed;
        return Result::unverified;
    } catch (const json::exception&) {
        return submitted ? Result::unverified : Result::unavailable;
    }
}
} // namespace Slic3r::SnorcaVibrationCalibration

#endif
