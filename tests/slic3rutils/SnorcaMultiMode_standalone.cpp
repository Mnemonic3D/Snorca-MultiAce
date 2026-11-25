#include "slic3r/Utils/SnorcaFilamentPlan.hpp"
#include <cassert>
#include <iostream>
#include <locale>
#include <sstream>
#include <cmath>
#include <cstdio>
using nlohmann::json;
using Slic3r::SnorcaFilamentSync::normalize;
#include "matcher.inc"
#include "request.inc"
int main()
{
    json st = {{"mode", "multi"}, {"aces", json::array()}, {"toolheads", json::array()}};
    for (int h = 0; h < 4; ++h)
        st["toolheads"].push_back({{"idx", h}, {"ace", 0}, {"slot", h}, {"manual", false}});
    for (int a = 0; a < 2; ++a) {
        json slots = json::array();
        for (int s = 0; s < 4; ++s)
            slots.push_back({{"idx", s}, {"state", "ready"}, {"material", "PETG"}, {"color", "#008000"}, {"source", "rfid"}});
        st["aces"].push_back({{"idx", a}, {"connected", true}, {"slots", slots}});
    }
    auto rows = normalize(st, "TEST", "192.168.4.30")["rows"];
    assert(rows.size() == 8);
    const int expected_nozzles[] = {0, 1, 2, 3, 0, 1, 2, 3}, expected_pins[] = {0, 0, 0, 0, 1, 1, 1, 1};
    for (int i = 0; i < 8; ++i) {
        assert(rows[i]["nozzle"] == expected_nozzles[i]);
        assert(rows[i]["mms_slot"] == expected_pins[i]);
        assert(rows[i]["available"] == true);
    }
    auto single    = st;
    single["mode"] = "single";
    assert(normalize(single, "TEST", "192.168.4.30")["rows"] == rows);
    auto stale           = st;
    stale["head_feeder"] = {{"0", true}};
    stale["head_ace"]    = {{"0", 3}};
    assert(normalize(stale, "TEST", "192.168.4.30")["rows"] == rows);
    auto bad                    = st;
    bad["toolheads"][0]["slot"] = 2;
    bool rejected               = false;
    try {
        normalize(bad, "TEST", "192.168.4.30");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    auto manual            = st;
    manual["toolheads"][2] = {{"idx", 2},          {"manual", true},     {"ace", nullptr},       {"slot", nullptr},
                              {"material", "TPU"}, {"color", "#FFFFFF"}, {"module_exist", true}, {"filament_detected", true}};
    auto mr                = normalize(manual, "TEST", "192.168.4.30")["rows"];
    assert(mr.size() == 7);
    assert(mr.back()["key"] == "TEST|EXTERNAL:2");
    auto empty                    = st;
    empty["aces"][1]["connected"] = false;
    auto er                       = normalize(empty, "TEST", "192.168.4.30")["rows"];
    assert(er.size() == 8);
    assert(er[4]["available"] == false);
    auto head           = st;
    head["mode"]        = "head";
    head["ace_heads"]   = {1, 3};
    head["head_feeder"] = {{"0", true}, {"1", false}, {"2", true}, {"3", false}};
    head["head_ace"]    = {{"0", 0}, {"1", 0}, {"2", 0}, {"3", 1}};
    for (int h = 0; h < 4; ++h)
        head["toolheads"][h] = {{"idx", h}, {"ace", nullptr}, {"slot", nullptr}};
    auto hr = normalize(head, "TEST", "192.168.4.30")["rows"];
    assert(hr.size() == 10);
    assert(hr[0]["nozzle"] == 1);
    assert(hr[4]["nozzle"] == 3);
    assert(hr[7]["mms_slot"] == 3);
    // Firmware preserves sparse MMU places across unplugging, removal and mixed driver setups.
    auto one_unit = st;
    one_unit["aces"].erase(1);
    assert(normalize(one_unit, "TEST", "192.168.4.30")["rows"].size() == 4);
    auto four_units = st;
    for (int a = 2; a < 4; ++a) {
        auto unit = st["aces"][0];
        unit["idx"] = a;
        unit["kind"] = a == 2 ? "ace2k" : "cfs_nano";
        four_units["aces"].push_back(unit);
        const auto all = normalize(four_units, "TEST", "192.168.4.30")["rows"];
        assert(all.size() == 4 * (a + 1));
        assert(all.back()["key"] == "TEST|ACE:" + std::to_string(a) + ":3");
        assert(all.back()["nozzle"] == 3 && all.back()["mms_slot"] == a);
    }
    auto sparse = four_units;
    sparse["aces"][2]["connected"] = false;
    sparse["aces"][2]["unit_present"] = false;
    sparse["aces"][2]["unit_key"] = "";
    sparse["aces"][3]["unit_present"] = false;
    sparse["aces"][3]["unit_key"] = "saved-cfs";
    const auto sparse_rows = normalize(sparse, "TEST", "192.168.4.30")["rows"];
    assert(sparse_rows.size() == 12);
    assert(sparse_rows.back()["key"] == "TEST|ACE:3:3");
    assert(sparse_rows.back()["available"] == false);
    auto spare = head;
    spare["aces"] = four_units["aces"];
    assert(normalize(spare, "TEST", "192.168.4.30")["rows"] == hr);
    spare["head_ace"]["1"] = 3;
    spare["head_ace"]["3"] = 2;
    const auto swapped = normalize(spare, "TEST", "192.168.4.30")["rows"];
    assert(swapped.size() == 10);
    assert(swapped[0]["key"] == "TEST|ACE:2:0" && swapped[0]["nozzle"] == 3);
    assert(swapped[4]["key"] == "TEST|ACE:3:0" && swapped[4]["nozzle"] == 1);
    auto stock = st;
    stock["mode"] = "normal";
    rejected = false;
    try {
        normalize(stock, "TEST", "192.168.4.30");
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    manual["toolheads"][2]["filament_at_extruder"] = false;
    assert(normalize(manual, "TEST", "192.168.4.30")["rows"].back()["available"] == false);
    for (int h = 0; h < 4; ++h)
        manual["toolheads"][h] = {{"idx", h}, {"manual", true}, {"ace", nullptr}, {"slot", nullptr}};
    const auto all_manual = normalize(manual, "TEST", "192.168.4.30")["rows"];
    assert(all_manual.size() == 4 && all_manual.back()["key"] == "TEST|EXTERNAL:3");

    // An empty lane can keep a saved swatch while a different lane holds a real spool.
    auto partly_loaded = st;
    partly_loaded["aces"][0]["slots"][0]["state"] = "empty";
    partly_loaded["aces"][0]["slots"][0]["color"] = "#008000";
    partly_loaded["aces"][1]["slots"][0]["color"] = "#0000FF";
    for (auto& unit : partly_loaded["aces"])
        for (int slot = 1; slot < 4; ++slot)
            unit["slots"][slot]["state"] = "empty";
    const json project = {{"rows", {{{"preset", "Generic PETG"}, {"color", "#008000"}}}}, {"used", {0}}};
    const json presets = {{{"name", "Generic PETG"}, {"material", "PETG"}, {"generic", true}}};
    for (bool manual_sync : {false, true}) {
        auto plan = Slic3r::SnorcaFilamentSync::plan_exact(project, normalize(partly_loaded, "TEST", "192.168.4.30"), presets,
                                                          manual_sync, 64);
        assert(plan["moved"].size() == 1 && plan["remap"][0] == 1);
    }
    auto painted_project = project;
    painted_project["protect_project_colors"] = true;
    const auto unmatched_inventory = normalize(partly_loaded, "TEST", "192.168.4.30");
    const auto manual_paint = Slic3r::SnorcaFilamentSync::plan_exact(painted_project, unmatched_inventory, presets, true, 64);
    assert(manual_paint["moved"].empty() && manual_paint["kept_not_on_printer"].size() == 1);
    const auto retained_paint = Slic3r::SnorcaFilamentSync::plan_exact(painted_project, unmatched_inventory, presets, false, 64);
    assert(retained_paint["moved"].empty() && retained_paint["kept_not_on_printer"].size() == 1);
    const auto &retained = retained_paint["rows"][retained_paint["remap"][0].get<int>()];
    assert(retained["color"] == project["rows"][0]["color"] && retained["nozzle"] == -1);
    assert(retained["ownership"].empty());
    assert(manual_paint["rows"][manual_paint["remap"][0].get<int>()] == retained);
    auto exact_inventory = partly_loaded;
    exact_inventory["aces"][1]["slots"][0]["color"] = "#008000";
    auto imported_project = painted_project;
    imported_project["rows"][0]["preset"] = "Embedded project PETG";
    imported_project["project_materials"] = {"PETG"};
    for (bool manual_sync : {false, true}) {
        const auto exact_paint = Slic3r::SnorcaFilamentSync::plan_exact(
            imported_project, normalize(exact_inventory, "TEST", "192.168.4.30"), presets, manual_sync, 64);
        assert(exact_paint["moved"].size() == 1 && exact_paint["remap"][0] == 1);
    }
    auto formerly_synced = painted_project;
    formerly_synced["rows"][0] = manual_paint["rows"][1];
    formerly_synced["rows"][0]["color"] = "#008000";
    const auto moved_spool = Slic3r::SnorcaFilamentSync::plan_exact(formerly_synced, unmatched_inventory, presets, true, 64);
    const auto &saved_color = moved_spool["rows"][moved_spool["remap"][0].get<int>()];
    assert(moved_spool["moved"].empty() && saved_color["color"] == "#008000" && saved_color["nozzle"] == -1);
    std::cout << "PASS: strict sync preserves unmatched project colors and matches imported materials\n";

    auto tester_state = st;
    for (auto &slot : tester_state["aces"][1]["slots"])
        slot["material"] = "PLA";
    tester_state["aces"][1]["slots"][0]["color"] = "#0033CC";
    tester_state["aces"][1]["slots"][1]["color"] = "#B00000";
    tester_state["aces"][1]["slots"][2]["color"] = "#EFEFEF";
    tester_state["aces"][1]["slots"][3]["color"] = "#000000";
    const json tester_project = {
        {"rows", {{{"preset", "Imported PLA"}, {"color", "#0099FF"}},
                  {{"preset", "Imported PLA Silk"}, {"color", "#FF0000"}},
                  {{"preset", "Imported PLA"}, {"color", "#FFFFFF"}}}},
        {"used", {0, 1, 2}}, {"protect_project_colors", true}, {"match_imported_colors", true},
        {"project_materials", {"PLA", "PLA SILK", "PLA"}}
    };
    auto tester_presets = presets;
    tester_presets.push_back({{"name", "Generic PLA"}, {"material", "PLA"}, {"generic", true}});
    for (bool manual_sync : {false, true}) {
        const auto matched = Slic3r::SnorcaFilamentSync::plan_exact(
            tester_project, normalize(tester_state, "TEST", "192.168.4.30"), tester_presets, manual_sync, 8);
        assert(matched["rows"].size() == 8 && matched["kept_not_on_printer"].empty());
        assert(matched["remap"] == json::array({1, 3, 5}));
        assert(matched["rows"][1]["color"] == "#0033CC" && matched["rows"][3]["nozzle"] == 1);
        assert(matched["moved"][1]["same_material"] == false);
    }
    tester_state["aces"][1]["slots"][0]["state"] = "empty";
    const auto fallback_inventory = normalize(tester_state, "TEST", "192.168.4.30");
    const auto black = Slic3r::SnorcaFilamentSync::plan_exact(tester_project, fallback_inventory, tester_presets, false, 8);
    assert(black["remap"] == json::array({7, 3, 5}) && black["rows"][7]["color"] == "#000000");
    auto unreadable_project = tester_project;
    unreadable_project["rows"][0]["color"] = "";
    const auto unreadable = Slic3r::SnorcaFilamentSync::plan_exact(unreadable_project, fallback_inventory, tester_presets, false, 8);
    assert(unreadable["remap"][0] == 7);
    tester_state["aces"][1]["slots"][3]["state"] = "empty";
    const auto without_black = Slic3r::SnorcaFilamentSync::plan_exact(
        tester_project, normalize(tester_state, "TEST", "192.168.4.30"), tester_presets, false, 8);
    assert(without_black["remap"][0] == 5 && without_black["rows"].size() == 8);
    const auto no_color_to_match = Slic3r::SnorcaFilamentSync::plan_exact(
        unreadable_project, normalize(tester_state, "TEST", "192.168.4.30"), tester_presets, false, 9);
    assert(no_color_to_match["kept_not_on_printer"].size() == 1 && no_color_to_match["rows"].back()["nozzle"] == -1);
    std::cout << "PASS: imported colors match eight loaded feeds, prioritize blue over white, and fall back to loaded black\n";

    partly_loaded["aces"][0]["slots"][0]["material"] = "UNSUPPORTED";
    auto plan_with_stale_material = Slic3r::SnorcaFilamentSync::plan_exact(project, normalize(partly_loaded, "TEST", "192.168.4.30"),
                                                                           presets, false, 64);
    assert(plan_with_stale_material["remap"][0] == 1);
    partly_loaded["aces"][1]["connected"] = false;
    auto no_target = Slic3r::SnorcaFilamentSync::plan_exact(project, normalize(partly_loaded, "TEST", "192.168.4.30"), presets, true, 64);
    assert(no_target["moved"].empty() && no_target["kept_not_on_printer"].size() == 1);
    assert(no_target["rows"].back() == project["rows"][0]);
    std::cout << "PASS: sparse places, mixed drivers, Head mode spare MMUs, stock mode guard, manual sensor and remap occupancy\n";
    json                               report = {{"plans", {{"slicer", json::object()}}},
                                                 {"slicer_colors", {{{"t", 0}, {"material", "PETG"}, {"hex", "#008000"}}}},
                                                 {"live_slots", {{{"ace", 1}, {"slot", 2}, {"material", "PETG"}, {"color", "#008000"}}}}};
    std::map<std::string, std::string> asn;
    std::string                        reason, summary;
    assert(assign(report.dump(), 1, asn, reason, summary, nullptr));
    assert(asn.at("0") == "6");
    auto body = request(report, st, asn, reason);
    assert(body["mode"] == "slicer");
    assert(body["remap"]["0"] == 6);
    assert(!body.contains("head_assignment"));
    auto mismatch    = st;
    mismatch["mode"] = "normal";
    assert(request(report, mismatch, asn, reason) == 0);
    auto duplicate = report;
    duplicate["live_slots"].push_back(duplicate["live_slots"][0]);
    assert(!assign(duplicate.dump(), 1, asn, reason, summary, nullptr));
    auto missing                            = report;
    missing["slicer_colors"][0]["material"] = "ASA";
    assert(!assign(missing.dump(), 1, asn, reason, summary, nullptr));
    auto mixed             = report;
    mixed["forca"]         = true;
    mixed["nozzles_mixed"] = true;
    mixed["nozzles"]       = {{"0", 0.4}};
    mixed["head_nozzles"]  = {{"2", 0.6}};
    assert(!assign(mixed.dump(), 1, asn, reason, summary, nullptr));
    json hp = {{"head_mode", true},
               {"slicer_colors", report["slicer_colors"]},
               {"targets",
                {{{"id", "slot-0-1"}, {"kind", "ace"}, {"ace", 0}, {"slot", 1}, {"head", 3}, {"material", "PETG"}, {"color", "#008000"}}}}};
    assert(assign(hp.dump(), 1, asn, reason, summary, nullptr));
    assert(asn.at("0") == "slot-0-1");
    assert(request(hp, head, asn, reason)["head_assignment"]["0"] == "slot-0-1");
    // Seven configured spools, but this plate consumed only filament 1.
    const auto only_head_1 = snorca_used_filaments("152.25, 0, 0, 0, 0, 0, 0");
    assert(only_head_1 == std::set<int>{0});
    assert((snorca_used_filaments("0, 15.5, 0, 1.25") == std::set<int>{1, 3}));
    assert(snorca_used_filaments("10, broken").empty());
    assert(snorca_used_filaments("0, 0").empty());
    json single_head = {{"head_mode", true}, {"slicer_colors", json::array()}, {"targets", json::array()}};
    for (int t = 0; t < 7; ++t) {
        const std::string material = t < 3 ? "PLA" : "ASA";
        const std::string color    = t < 2 ? "#000000" : "#FF0000";
        single_head["slicer_colors"].push_back({{"t", t}, {"material", material}, {"hex", color}});
        single_head["targets"].push_back({{"id", std::to_string(t)},
                                          {"kind", t < 3 ? "pin" : "ace"},
                                          {"head", std::min(t, 3)},
                                          {"ace", 0},
                                          {"slot", std::max(t - 3, 0)},
                                          {"material", material},
                                          {"color", color}});
    }
    std::vector<std::vector<std::string>> plan;
    asn.clear();
    assert(assign(single_head.dump(), 1, asn, reason, summary, &plan));
    assert(asn.size() == 7 && plan.size() == 7);
    asn.clear();
    assert(assign(single_head.dump(), 1, asn, reason, summary, &plan, only_head_1));
    assert(asn.size() == 1 && asn.at("0") == "0" && plan.size() == 1 && plan[0][3] == "head 1 feeder");
    assert(request(single_head, head, asn, reason)["head_assignment"].size() == 1);
    asn.clear();
    single_head["slicer_colors"].erase(0);
    assert(!assign(single_head.dump(), 1, asn, reason, summary, &plan, only_head_1));
    assert(asn.empty());
    std::cout << "PASS: Head 1-only confirmation and print assignment, sparse IDs, malformed usage, missing active filament\n";
    std::cout << "PASS: multi routing, legacy single, stale head metadata, invalid routing, manual heads, disconnected unit, head "
                 "regression, print remap, mismatch rejection, duplicate slot rejection, material/nozzle checks, head print regression\n";
}
