#include "slic3r/Utils/SnorcaFilamentSync.hpp"
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
