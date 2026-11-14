#include "MultiACE.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem/path.hpp>

#include <wx/progdlg.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "Http.hpp"
#include <set>
#include "nlohmann/json.hpp"
#include <map>      // SNORCA
#include <cmath>    // SNORCA
#include <cctype>   // SNORCA
#include <chrono>   // SNORCA
#include <thread>   // SNORCA
#include <regex>    // SNORCA
#include <fstream>  // SNORCA
#include <cstdio>   // SNORCA (2026-11-13)
#include <boost/algorithm/string.hpp> // SNORCA
#include <boost/filesystem/operations.hpp> // SNORCA

namespace fs = boost::filesystem;

namespace Slic3r {

MultiACE::MultiACE(DynamicPrintConfig *config) : OctoPrint(config) {}

const char* MultiACE::get_name() const { return "multiACE"; }

wxString MultiACE::get_test_ok_msg() const
{
    return _(L("Connection to multiACE is working correctly."));
}

wxString MultiACE::get_test_failed_msg(wxString &msg) const
{
    return GUI::format_wxstr("%s: %s\n\n%s"
        , _L("Could not connect to multiACE")
        , msg
        , _L("Note: the address must be the printer's LAN IP, and multiACE must be installed on it."));
}

bool MultiACE::test(wxString &msg) const
{
    const char *name = get_name();
    bool res = true;
    auto url = make_url(health_path());

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Checking health at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting health: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Health answer: %2%") % name % body;
            // multiACE answers {"status":"ok","version":...}. Anything else on
            // this path means we reached some other service on that address.
            if (body.find("\"status\"") == std::string::npos) {
                res = false;
                msg = _L("The address answered, but not with multiACE. Check that multiACE is installed on this printer.");
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

// SNORCA: automatic plan + start (user request 2026-11-12: "It should just automatically send it to multi-ace and print").
namespace {                                                                                                   // SNORCA
std::string snorca_low(std::string s)                                                                           // SNORCA
{                                                                                                             // SNORCA
    while (!s.empty() && std::isspace((unsigned char) s.back())) s.pop_back();                               // SNORCA
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char) s[i])) ++i;                            // SNORCA
    s.erase(0, i);                                                                                            // SNORCA
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });   // SNORCA
    return s;                                                                                                 // SNORCA
}                                                                                                             // SNORCA
bool snorca_rgb(std::string h, int rgb[3])                                                                      // SNORCA
{                                                                                                             // SNORCA
    h = snorca_low(h); if (!h.empty() && h[0] == '#') h.erase(0, 1);                                            // SNORCA
    if (h.size() < 6 || h.find_first_not_of("0123456789abcdef") < 6) return false;                           // SNORCA
    for (int k = 0; k < 3; ++k) rgb[k] = std::stoi(h.substr(2 * k, 2), nullptr, 16);                         // SNORCA
    return true;                                                                                              // SNORCA
}                                                                                                             // SNORCA
std::string snorca_hex(const std::string &h)                                                                    // SNORCA
{                                                                                                             // SNORCA
    int c[3]; if (!snorca_rgb(h, c)) return "(no colour)";                                                      // SNORCA
    char b[8]; snprintf(b, sizeof(b), "#%02X%02X%02X", c[0], c[1], c[2]); return b;                           // SNORCA
}                                                                                                             // SNORCA
std::string snorca_upper(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::toupper); return s; } // SNORCA
// Minimum total cost, one column per row, rows <= cols (Hungarian method). Returns the column of each row.  // SNORCA
std::vector<int> snorca_min_cost(const std::vector<std::vector<double>> &a)                                     // SNORCA
{                                                                                                             // SNORCA
    const int n = (int) a.size(), m = n ? (int) a[0].size() : 0; const double INF = 1e18;                    // SNORCA
    std::vector<double> u(n + 1, 0), v(m + 1, 0); std::vector<int> p(m + 1, 0), way(m + 1, 0);               // SNORCA
    for (int i = 1; i <= n; ++i) {                                                                            // SNORCA
        p[0] = i; int j0 = 0; std::vector<double> minv(m + 1, INF); std::vector<char> used(m + 1, 0);        // SNORCA
        do {                                                                                                  // SNORCA
            used[j0] = 1; int i0 = p[j0], j1 = 0; double delta = INF;                                         // SNORCA
            for (int j = 1; j <= m; ++j) if (!used[j]) {                                                      // SNORCA
                double cur = a[i0 - 1][j - 1] - u[i0] - v[j];                                                 // SNORCA
                if (cur < minv[j]) { minv[j] = cur; way[j] = j0; }                                            // SNORCA
                if (minv[j] < delta) { delta = minv[j]; j1 = j; }                                             // SNORCA
            }                                                                                                 // SNORCA
            for (int j = 0; j <= m; ++j) if (used[j]) { u[p[j]] += delta; v[j] -= delta; } else minv[j] -= delta; // SNORCA
            j0 = j1;                                                                                          // SNORCA
        } while (p[j0] != 0);                                                                                 // SNORCA
        do { int j1 = way[j0]; p[j0] = p[j1]; j0 = j1; } while (j0);                                          // SNORCA
    }                                                                                                         // SNORCA
    std::vector<int> col(n, -1);                                                                              // SNORCA
    for (int j = 1; j <= m; ++j) if (p[j] > 0) col[p[j] - 1] = j - 1;                                         // SNORCA
    return col;                                                                                               // SNORCA
}                                                                                                             // SNORCA
std::string snorca_err(const std::string &body, const std::string &error, unsigned status)                      // SNORCA
{                                                                                                             // SNORCA
    std::string d;                                                                                            // SNORCA
    try { auto j = nlohmann::json::parse(body); if (j.contains("detail")) d = j["detail"].is_string() ? j["detail"].get<std::string>() : j["detail"].dump(); } catch (...) {} // SNORCA
    std::string s = status ? "HTTP " + std::to_string(status) : error;                                        // SNORCA
    if (!d.empty()) s += ": " + d.substr(0, 200);                                                             // SNORCA
    else if (status && !error.empty()) s += ", " + error;                                                     // SNORCA
    return s;                                                                                                 // SNORCA
}                                                                                                             // SNORCA
} // namespace                                                                                                // SNORCA

bool MultiACE::snorca_assign_head_targets(const std::string &report_json, int base,                             // SNORCA
                                        std::map<std::string, std::string> &asn, std::string &reason, std::string &summary, // SNORCA
                                        std::vector<std::vector<std::string>> *plan)                  // SNORCA
{                                                                                                             // SNORCA
    using nlohmann::json;                                                                                     // SNORCA
    json r;                                                                                                   // SNORCA
    try { r = json::parse(report_json); } catch (...) { reason = "multiACE sent a plan the slicer cannot read."; return false; } // SNORCA
    if (!r.is_object() || !r.value("head_mode", false)) {                                                     // SNORCA
        reason = "multiACE is not in head mode on this printer; the automatic start is only built for head mode."; return false; } // SNORCA
    const json cols = r.value("slicer_colors", json::array()), tgts = r.value("targets", json::array());     // SNORCA
    if (!cols.is_array() || cols.empty()) { reason = "multiACE found no filament colours in the file."; return false; } // SNORCA
    if (!tgts.is_array()) { reason = "multiACE sent no spool list."; return false; }                         // SNORCA
    // Mixed nozzle sizes: a filament may only go to a head carrying the diameter it was sliced for (as the page). // SNORCA
    const bool mixed = r.value("forca", false) && r.value("nozzles_mixed", false);                           // SNORCA
    json hn = r.value("head_nozzles", json::object()); if (!hn.is_object() || hn.empty()) hn = r.value("nozzles", json::object()); // SNORCA
    const json fn = r.value("nozzles", json::object());                                                       // SNORCA
    auto allowed = [&](int t, int head) {                                                                     // SNORCA
        if (!mixed || !fn.is_object() || !fn.contains(std::to_string(t)) || !fn[std::to_string(t)].is_number()) return true; // SNORCA
        const double want = fn[std::to_string(t)].get<double>();                                              // SNORCA
        const std::string k = std::to_string(head);                                                           // SNORCA
        return hn.is_object() && hn.contains(k) && hn[k].is_number() && std::abs(hn[k].get<double>() - want) < 0.001; // SNORCA
    };                                                                                                        // SNORCA
    std::set<std::string> loaded;                                                                             // SNORCA
    for (const auto &g : tgts) { std::string m = snorca_low(g.value("material", "")); if (!m.empty()) loaded.insert(snorca_upper(m)); } // SNORCA
    std::string loaded_s; for (const auto &m : loaded) loaded_s += (loaded_s.empty() ? "" : ", ") + m;       // SNORCA
    if (loaded_s.empty()) loaded_s = "none";                                                                  // SNORCA
    std::map<std::string, std::vector<json>> by_mat;                                                          // SNORCA
    for (const auto &c : cols) {                                                                              // SNORCA
        const std::string m = snorca_low(c.value("material", ""));                                              // SNORCA
        if (m.empty()) { reason = "The file does not name the filament type of filament " + std::to_string(c.value("t", 0) + 1) + ", so the spool type cannot be checked."; return false; } // SNORCA
        by_mat[m].push_back(c);                                                                               // SNORCA
    }                                                                                                         // SNORCA
    std::map<int, json> chosen;                                                                               // SNORCA
    for (const auto &kv : by_mat) {                                                                           // SNORCA
        const std::string M = snorca_upper(kv.first);                                                           // SNORCA
        std::vector<json> cand;                                                                               // SNORCA
        for (const auto &g : tgts) if (snorca_low(g.value("material", "")) == kv.first && g.contains("id")) cand.push_back(g); // SNORCA
        std::string fil; for (const auto &c : kv.second) fil += (fil.empty() ? "" : ", ") + std::to_string(c.value("t", 0) + 1); // SNORCA
        if (cand.empty()) { reason = "No " + M + " spool is loaded for filament " + fil + " (loaded: " + loaded_s + ")."; return false; } // SNORCA
        if (kv.second.size() > cand.size()) {                                                                 // SNORCA
            reason = "The job needs " + std::to_string(kv.second.size()) + " " + M + " colours (filaments " + fil + ") but only " // SNORCA
                   + std::to_string(cand.size()) + " " + M + " spool" + (cand.size() == 1 ? " is" : "s are") + " loaded."; return false; } // SNORCA
        std::vector<std::vector<double>> cost(kv.second.size(), std::vector<double>(cand.size(), 0.));        // SNORCA
        for (size_t i = 0; i < kv.second.size(); ++i) for (size_t j = 0; j < cand.size(); ++j) {             // SNORCA
            int a[3], b[3];                                                                                   // SNORCA
            const bool ok = snorca_rgb(kv.second[i].value("hex", ""), a) && snorca_rgb(cand[j].value("color", ""), b); // SNORCA
            cost[i][j] = ok ? std::sqrt(double((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2]))) : 1000.; // SNORCA
            if (!allowed(kv.second[i].value("t", 0), cand[j].value("head", -1))) cost[i][j] = 1e9;           // SNORCA
        }                                                                                                     // SNORCA
        const std::vector<int> col = snorca_min_cost(cost);                                                     // SNORCA
        for (size_t i = 0; i < kv.second.size(); ++i) {                                                       // SNORCA
            if (col[i] < 0 || cost[i][col[i]] >= 1e9) {                                                       // SNORCA
                reason = "No " + M + " spool sits on a head with the nozzle size filament " + std::to_string(kv.second[i].value("t", 0) + 1) + " was sliced for."; return false; } // SNORCA
            chosen[kv.second[i].value("t", 0)] = cand[col[i]];                                                // SNORCA
        }                                                                                                     // SNORCA
    }                                                                                                         // SNORCA
    summary.clear();                                                                                          // SNORCA
    if (plan) plan->clear();                                                                                  // SNORCA
    for (const auto &c : cols) {                                                                              // SNORCA
        const int t = c.value("t", 0); const json &g = chosen[t];                                             // SNORCA
        asn[std::to_string(t)] = g["id"].is_string() ? g["id"].get<std::string>() : g["id"].dump();          // SNORCA
        std::string where = g.value("kind", "") == "pin" ? "head " + std::to_string(g.value("head", 0) + base) + " feeder" // SNORCA
            : "ACE " + std::to_string(g.value("ace", 0) + base) + ", slot " + std::to_string(g.value("slot", 0) + base); // SNORCA
        summary += (summary.empty() ? "" : "; ") + std::string("filament ") + std::to_string(t + 1) + " " + snorca_hex(c.value("hex", "")) // SNORCA
                 + " -> " + where + " " + snorca_hex(g.value("color", ""));                                    // SNORCA
        if (plan) plan->push_back({"Filament " + std::to_string(t + 1), snorca_upper(c.value("material", "")), snorca_hex(c.value("hex", "")), // SNORCA
                                   where, snorca_upper(g.value("material", "")), snorca_hex(g.value("color", ""))}); // SNORCA
    }                                                                                                         // SNORCA
    const json miss = r.value("missing_materials", json::array());                                           // SNORCA
    if (miss.is_array() && !miss.empty()) { reason = "multiACE reports a missing material: " + miss.dump() + "."; return false; } // SNORCA
    return true;                                                                                              // SNORCA
}                                                                                                             // SNORCA

// SNORCA: multiACE puts its initial ACE load + a forced pre-extrude at the section boundary just before the first
// extruding move. The U1 start G-code heats to print temperature only AFTER that boundary (";===== 画起始线"  SNORCA
// then "M109 S<print temp>"), so the pre-extrude ran at the 170 C probing temperature and Klipper cancelled the  SNORCA
// print ("extruder3 below minimum temp", 2026-11-12). Copy that M109 to just before the boundary, so whatever  SNORCA
// multiACE inserts there runs at print temperature. The original M109 stays (then a no-op). Rewrites the temp  SNORCA
// export in place; returns the inserted line or "" when the pattern is not found (file left unchanged).  SNORCA
static std::string snorca_heat_before_initial_load(const boost::filesystem::path &path)                         // SNORCA
{                                                                                                             // SNORCA
    std::ifstream in(path.string(), std::ios::binary); if (!in) return "";                                    // SNORCA
    std::vector<std::string> head; std::string l; int ext = -1;                                               // SNORCA
    static const std::regex e_re("\\bE(-?[0-9]*\\.?[0-9]+)"), m109_re("^M109\\s+S([0-9]+(\\.[0-9]+)?)\\s*(;.*)?$"); // SNORCA
    while (ext < 0 && head.size() < 20000 && std::getline(in, l)) {                                          // SNORCA
        head.push_back(l);                                                                                    // SNORCA
        std::string t = l; if (!t.empty() && t.back() == '\r') t.pop_back();                                 // SNORCA
        size_t a = t.find_first_not_of(" \t"); if (a == std::string::npos) continue; t = t.substr(a);         // SNORCA
        std::smatch m;                                                                                        // SNORCA
        if ((t.rfind("G1", 0) == 0 || t.rfind("G0", 0) == 0) && std::regex_search(t, m, e_re) && std::stod(m[1]) > 0) // SNORCA
            ext = (int) head.size() - 1;                                                                      // SNORCA
    }                                                                                                         // SNORCA
    if (ext < 0) return "";                                                                                   // SNORCA
    int bnd = -1;                                                                                             // SNORCA
    for (int j = ext - 1; j >= 0 && j > ext - 200; --j) {                                                     // SNORCA
        std::string t = boost::algorithm::trim_copy(head[j]);                                                 // SNORCA
        if (t.empty() || (t[0] == ';' && t.find("=====") != std::string::npos)) { bnd = j; break; }           // SNORCA
    }                                                                                                         // SNORCA
    if (bnd < 0) return "";                                                                                   // SNORCA
    std::string heat;                                                                                         // SNORCA
    for (int j = bnd + 1; j < ext && heat.empty(); ++j) {                                                     // SNORCA
        std::string t = boost::algorithm::trim_copy(head[j]); std::smatch m;                                  // SNORCA
        if (std::regex_match(t, m, m109_re)) heat = "M109 S" + m[1].str();                                    // SNORCA
    }                                                                                                         // SNORCA
    if (heat.empty()) return "";                                                                              // SNORCA
    const boost::filesystem::path tmp = path.string() + ".snorca-heat";                                         // SNORCA
    {                                                                                                         // SNORCA
        std::ofstream out(tmp.string(), std::ios::binary); if (!out) return "";                              // SNORCA
        for (int j = 0; j < (int) head.size(); ++j) {                                                         // SNORCA
            if (j == bnd) out << heat << " ; SNORCA: print temperature before multiACE's initial load and pre-extrude\n"; // SNORCA
            out << head[j] << '\n';                                                                          // SNORCA
        }                                                                                                     // SNORCA
        out << in.rdbuf();                                                                                    // SNORCA
        if (!out) return "";                                                                                  // SNORCA
    }                                                                                                         // SNORCA
    in.close();                                                                                               // SNORCA
    boost::system::error_code ec; boost::filesystem::rename(tmp, path, ec);                                   // SNORCA
    return ec ? std::string() : heat;                                                                         // SNORCA
}                                                                                                             // SNORCA

// SNORCA (2026-11-13): the print options window (Plater.cpp snorca_ask_print_options) puts the user's choice in the  SNORCA
// upload's extended_info. Missing keys (a send that did not pass that window) keep the earlier behaviour:      SNORCA
// bed mesh off, flow calibration off, file unchanged.                                                          SNORCA
static std::string snorca_choice(const PrintHostUpload &up, const char *key)                                    // SNORCA
{                                                                                                             // SNORCA
    const auto it = up.extended_info.find(key);                                                               // SNORCA
    return it == up.extended_info.end() ? std::string() : it->second;                                         // SNORCA
}                                                                                                             // SNORCA

// SNORCA (2026-11-13): "Print area only" vs "Full bed". multiACE only switches the bed mesh on or off            SNORCA
// (SET_PRINT_PREFERENCES BED_LEVEL=..). The mesh itself is the start G-code's BED_MESH_CALIBRATE line; the    SNORCA
// printer's Klipper (bed_mesh.py set_adaptive_mesh) probes only the area of the defined objects when the line  SNORCA
// carries ADAPTIVE=1, and the whole bed with ADAPTIVE=0 or no parameter. So, before upload:                    SNORCA
//   full: every BED_MESH_CALIBRATE before the first layer gets ADAPTIVE=0 (explicit, so a printer-side          SNORCA
//         "force adaptive" tweak, which respects an explicit ADAPTIVE, cannot turn it into a partial mesh);     SNORCA
//   area: those lines get ADAPTIVE=1. Klipper needs object outlines for that. When the file has no            SNORCA
//         EXCLUDE_OBJECT_DEFINE of its own (the U1 process default exclude_object = 0), one outline of        SNORCA
//         everything this print extrudes (all layers, start/end G-code excluded, arcs as full circles) is     SNORCA
//         defined just before the mesh line and removed again right after it (EXCLUDE_OBJECT_DEFINE RESET=1), SNORCA
//         so the printer's object list is empty again for the print itself;                                    SNORCA
//   none: unchanged (BED_LEVEL=0 makes the printer skip the mesh line while printing).                         SNORCA
// Returns a one-line note for the log. On any problem the file is left unchanged.                             SNORCA
static std::string snorca_apply_bed_choice(const boost::filesystem::path &path, const std::string &bed)         // SNORCA
{                                                                                                             // SNORCA
    if (bed != "area" && bed != "full") return "bed '" + bed + "': file unchanged";                           // SNORCA
    auto strip = [](std::string t) { const size_t c = t.find(';'); if (c != std::string::npos) t.erase(c);   // SNORCA
        if (!t.empty() && t.back() == '\r') t.pop_back(); boost::algorithm::trim(t); return t; };              // SNORCA
    auto upper = [](std::string t) { for (auto &ch : t) ch = char(std::toupper((unsigned char) ch)); return t; }; // SNORCA
    auto word = [&](const std::string &code) { const size_t s = code.find_first_of(" \t"); return upper(code.substr(0, s)); }; // SNORCA
    std::vector<long> mesh; long first_layer = -1; bool has_define = false;                                  // SNORCA
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;                                              // SNORCA
    {                                                                                                         // SNORCA
        std::ifstream in(path.string(), std::ios::binary); if (!in) return "could not read the file: unchanged"; // SNORCA
        std::string l, type; long n = -1; bool abs_xy = true, abs_e = false; double x = 0, y = 0, e = 0;     // SNORCA
        auto add = [&](double px, double py) { minx = std::min(minx, px); miny = std::min(miny, py); maxx = std::max(maxx, px); maxy = std::max(maxy, py); }; // SNORCA
        while (std::getline(in, l)) {                                                                         // SNORCA
            ++n;                                                                                              // SNORCA
            if (!l.empty() && l[0] == ';') {                                                                  // SNORCA
                if (first_layer < 0 && l.rfind(";LAYER_CHANGE", 0) == 0) first_layer = n;                     // SNORCA
                else if (l.rfind(";TYPE:", 0) == 0) { type = l.substr(6); boost::algorithm::trim(type); }      // SNORCA
                continue;                                                                                     // SNORCA
            }                                                                                                 // SNORCA
            const std::string code = strip(l); if (code.empty()) continue;                                    // SNORCA
            const std::string w = word(code);                                                                 // SNORCA
            if (w == "BED_MESH_CALIBRATE") { if (first_layer < 0) mesh.push_back(n); continue; }              // SNORCA
            if (w == "EXCLUDE_OBJECT_DEFINE") { has_define = true; continue; }                                // SNORCA
            if (w == "G90") { abs_xy = true; continue; } if (w == "G91") { abs_xy = false; continue; }        // SNORCA
            if (w == "M82") { abs_e = true; continue; }  if (w == "M83") { abs_e = false; continue; }         // SNORCA
            const bool g01 = w == "G0" || w == "G1", arc = w == "G2" || w == "G3";                            // SNORCA
            if (!g01 && !arc && w != "G92") continue;                                                         // SNORCA
            std::map<char, double> p; std::stringstream ss(code.substr(w.size())); std::string tok;          // SNORCA
            while (ss >> tok) if (tok.size() > 1 && std::isalpha((unsigned char) tok[0]))                     // SNORCA
                try { p[char(std::toupper((unsigned char) tok[0]))] = std::stod(tok.substr(1)); } catch (...) {} // SNORCA
            if (w == "G92") { if (p.count('E')) e = p['E']; if (p.count('X')) x = p['X']; if (p.count('Y')) y = p['Y']; continue; } // SNORCA
            const double nx = p.count('X') ? (abs_xy ? p['X'] : x + p['X']) : x;                              // SNORCA
            const double ny = p.count('Y') ? (abs_xy ? p['Y'] : y + p['Y']) : y;                              // SNORCA
            bool extrude = false;                                                                             // SNORCA
            if (p.count('E')) { extrude = abs_e ? p['E'] > e + 1e-9 : p['E'] > 0; if (abs_e) e = p['E']; }    // SNORCA
            if (extrude && first_layer >= 0 && type != "Custom" && (nx != x || ny != y)) {                    // SNORCA
                add(x, y); add(nx, ny);                                                                       // SNORCA
                if (arc && (p.count('I') || p.count('J'))) {                                                  // SNORCA
                    const double cx = x + p['I'], cy = y + p['J'], r = std::hypot(p['I'], p['J']);           // SNORCA
                    add(cx - r, cy - r); add(cx + r, cy + r);                                                 // SNORCA
                }                                                                                             // SNORCA
            }                                                                                                 // SNORCA
            x = nx; y = ny;                                                                                   // SNORCA
        }                                                                                                     // SNORCA
    }                                                                                                         // SNORCA
    if (mesh.empty()) return "no BED_MESH_CALIBRATE before the first layer: file unchanged";                  // SNORCA
    const bool area = bed == "area";                                                                          // SNORCA
    const bool define = area && !has_define && maxx >= minx && maxy >= miny;                                  // SNORCA
    if (area && !has_define && !define) return "no extrusion found: file unchanged";                          // SNORCA
    char poly[512] = "";                                                                                      // SNORCA
    if (define)                                                                                               // SNORCA
        std::snprintf(poly, sizeof(poly), "EXCLUDE_OBJECT_DEFINE NAME=SNORCA_PRINT_AREA CENTER=%.3f,%.3f POLYGON=[[%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f],[%.3f,%.3f]]", // SNORCA
                      (minx + maxx) / 2, (miny + maxy) / 2, minx, miny, maxx, miny, maxx, maxy, minx, maxy); // SNORCA
    const boost::filesystem::path tmp = path.string() + ".snorca-bed";                                          // SNORCA
    {                                                                                                         // SNORCA
        std::ifstream in(path.string(), std::ios::binary); std::ofstream out(tmp.string(), std::ios::binary); // SNORCA
        if (!in || !out) return "could not rewrite the file: unchanged";                                      // SNORCA
        std::string l; long n = -1; size_t k = 0;                                                             // SNORCA
        while (std::getline(in, l)) {                                                                         // SNORCA
            ++n;                                                                                              // SNORCA
            if (k < mesh.size() && n == mesh[k]) {                                                            // SNORCA
                ++k;                                                                                          // SNORCA
                std::string code = strip(l);                                                                  // SNORCA
                static const std::regex adaptive_re("\\s+ADAPTIVE=\\S*", std::regex::icase);                  // SNORCA
                code = std::regex_replace(code, adaptive_re, "");                                             // SNORCA
                out << "; SNORCA print options: bed calibration = " << (area ? "print area only" : "full bed") << "\n"; // SNORCA
                if (define) out << poly << "\n";                                                              // SNORCA
                out << code << (area ? " ADAPTIVE=1" : " ADAPTIVE=0") << "\n";                                // SNORCA
                if (define) out << "EXCLUDE_OBJECT_DEFINE RESET=1\n";                                         // SNORCA
                continue;                                                                                     // SNORCA
            }                                                                                                 // SNORCA
            out << l << '\n';                                                                                 // SNORCA
        }                                                                                                     // SNORCA
        if (!out) { out.close(); boost::system::error_code ec2; boost::filesystem::remove(tmp, ec2); return "could not rewrite the file: unchanged"; } // SNORCA
    }                                                                                                         // SNORCA
    boost::system::error_code ec; boost::filesystem::rename(tmp, path, ec);                                   // SNORCA
    if (ec) return "could not replace the file: unchanged";                                                   // SNORCA
    return std::string(area ? "print area only: ADAPTIVE=1" : "full bed: ADAPTIVE=0") + " on " + std::to_string(mesh.size()) + " line(s)" // SNORCA
         + (define ? std::string("; outline ") + poly : std::string(has_define && area ? "; the file's own object outlines are used" : "")); // SNORCA
}                                                                                                             // SNORCA

// SNORCA (2026-11-13, chamber options): writes the chamber choice of the print options window into the exported   SNORCA
// G-code before upload (same technique as snorca_apply_bed_choice). Only when the window reported a device; on any  SNORCA
// problem the file is left unchanged. Commands, all read from the stored U1 firmware image:                       SNORCA
//  Heater (Panda Breath / DragonBreath; both firmwares give M141/M191 macros):                                     SNORCA
//    start  M141 S<t> (heating starts at once); "print after": M191 S<t> at the wait point (where the U1 start   SNORCA
//           G-code waits for the chamber, after the bed is hot, before homing Z and the bed mesh);               SNORCA
//    end    M141 S0. M141/M191 lines the slicer itself wrote (filament chamber settings) are commented out, so   SNORCA
//           there is one owner of the chamber heater.                                                             SNORCA
//  Top cover (Klipper purifier.py; inner = circulation fan = the filter fan, exhaust = exhaust fan):              SNORCA
//    the U1 start G-code's own SET_PURIFIER_MODE block and WAIT_CHAMBER_TEMP are commented out (one owner);      SNORCA
//    filter  start SET_FAN_SPEED FAN=circulation_fan SPEED=0.6 (routed into the purifier, saves nothing, never   SNORCA
//            stops a print when the cover is missing); end, after PRINT_END (whose SET_PURIFIER_MODE MODE=0 starts  SNORCA
//            the printer's stored off-delay): the same line again, then SET_PURIFIER_MODE MODE=0                  SNORCA
//            INNER_FAN_DELAY_OFF=60 (a one-off delay: _setup_idle_mode reads it as a parameter, not saved);      SNORCA
//    exhaust start SET_FAN_SPEED FAN=exhaust_fan SPEED=0.6 (idle mode: no automatic pause on chamber alarm);       SNORCA
//    warm    SET_PURIFIER_MODE MODE=2 (wait) or 3 (no wait) DESIRE_TEMP=<t> FAN_SPEED=0.6 at the start, and for   SNORCA
//            "print after" WAIT_CHAMBER_TEMP TIMEOUT=180 at the wait point. Those modes set the stored exhaust    SNORCA
//            off-delay to 0 in the printer's purifier.json (purifier.py); the value the printer reported is      SNORCA
//            written back at the end (SET_PURIFIER FAN=exhaust DELAY_OFF=<d>) when it was not 0.                  SNORCA
//  DragonBreath filter (no top cover; also with heat, 2026-11-13): SET_PIN PIN=dragonbreath_filter VALUE=1 at the  SNORCA
//    start, BEFORE the M141 heat line (filter-on before heating is accepted and stays on through heating; it is   SNORCA
//    refused once heating); at the end M141 S0, G4 P60000, then VALUE=0 (the print stays "printing" that minute). SNORCA
//  Panda Breath: no filter command (panda_breath.py has none).                                                   SNORCA
static std::string snorca_apply_chamber_choice(const boost::filesystem::path &path, const PrintHostUpload &up)   // SNORCA
{                                                                                                             // SNORCA
    const std::string heater = snorca_choice(up, "snorca_chamber_heater");                                      // SNORCA
    const bool cover = snorca_choice(up, "snorca_chamber_cover") == "1";                                        // SNORCA
    if (heater != "panda_breath" && heater != "dragonbreath" && !cover) return "no chamber device: file unchanged"; // SNORCA
    const bool dragon = heater == "dragonbreath";                                                             // SNORCA
    auto on  = [&](const char *k) { return snorca_choice(up, k) == "1"; };                                      // SNORCA
    auto num = [&](const char *k, int def, int lo, int hi) { int v = def; try { v = std::stoi(snorca_choice(up, k)); } catch (...) {} return std::max(lo, std::min(hi, v)); }; // SNORCA
    const bool heat      = !heater.empty() && on("snorca_print_chamber_heat");                                // SNORCA
    const int  heat_t    = num("snorca_print_chamber_heat_temperature", 45, dragon ? 1 : 15, dragon ? 70 : 80); // SNORCA
    const bool heat_wait = on("snorca_print_chamber_heat_wait");                                              // SNORCA
    const bool exhaust   = cover && on("snorca_print_chamber_exhaust") && !heat;                              // SNORCA: never with the heater heating
    const bool warm      = cover && heater.empty() && on("snorca_print_chamber_warm") && !exhaust;            // SNORCA
    const int  warm_t    = num("snorca_print_chamber_warm_temperature", 42, 1, 45);                           // SNORCA
    const bool warm_wait = on("snorca_print_chamber_warm_wait");                                              // SNORCA
    const bool cover_filter  = cover && (on("snorca_print_chamber_filter") || warm);                          // SNORCA
    const bool dragon_filter = !cover && dragon && on("snorca_print_chamber_filter");                         // SNORCA (2026-11-13): also with heat
    int delay = -1; try { delay = std::stoi(snorca_choice(up, "snorca_chamber_cover_exhaust_delay")); } catch (...) {} // SNORCA
    std::vector<std::string> L;                                                                               // SNORCA
    {                                                                                                         // SNORCA
        std::ifstream in(path.string(), std::ios::binary); if (!in) return "could not read the file: unchanged"; // SNORCA
        std::string l; while (std::getline(in, l)) L.push_back(l);                                            // SNORCA
    }                                                                                                         // SNORCA
    auto strip = [](std::string t) { const size_t c = t.find(';'); if (c != std::string::npos) t.erase(c);   // SNORCA
        if (!t.empty() && t.back() == '\r') t.pop_back(); boost::algorithm::trim(t); return t; };              // SNORCA
    auto word = [](const std::string &code) { std::string w = code.substr(0, code.find_first_of(" \t")); for (auto &c : w) c = char(std::toupper((unsigned char) c)); return w; }; // SNORCA
    long first_layer = -1, print_start = -1, first_mesh = -1, block_end = -1, last_print_end = -1;           // SNORCA
    std::vector<long> purifier, waits, slicer_chamber;                                                        // SNORCA
    for (long i = 0; i < (long) L.size(); ++i) {                                                              // SNORCA
        const std::string &l = L[i];                                                                          // SNORCA
        if (!l.empty() && l[0] == ';') {                                                                      // SNORCA
            if (first_layer < 0 && l.rfind(";LAYER_CHANGE", 0) == 0) first_layer = i;                         // SNORCA
            if (boost::algorithm::trim_copy(l) == "; EXECUTABLE_BLOCK_END") block_end = i;                    // SNORCA
            continue;                                                                                         // SNORCA
        }                                                                                                     // SNORCA
        const std::string code = strip(l); if (code.empty()) continue;                                        // SNORCA
        const std::string w = word(code); const bool start = first_layer < 0;                                 // SNORCA
        if (start && w == "PRINT_START" && print_start < 0) print_start = i;                                  // SNORCA
        if (start && w == "BED_MESH_CALIBRATE" && first_mesh < 0) first_mesh = i;                             // SNORCA
        // The U1 start G-code's purifier block, including its stray "DELAY_OFF=0" line.                       SNORCA
        if (start && cover && (w == "SET_PURIFIER_MODE" || (w.rfind("DELAY_OFF=", 0) == 0 && code.find_first_of(" \t") == std::string::npos))) purifier.push_back(i); // SNORCA
        if (start && w == "WAIT_CHAMBER_TEMP") waits.push_back(i);                                            // SNORCA
        if (heat && (w == "M141" || w == "M191")) slicer_chamber.push_back(i);                                // SNORCA
        if (w == "PRINT_END") last_print_end = i;                                                             // SNORCA
    }                                                                                                         // SNORCA
    const long start_at = cover && !purifier.empty() ? purifier.front() : (print_start >= 0 ? print_start + 1 : -1); // SNORCA
    if (start_at < 0) return "no PRINT_START and no purifier block in the start G-code: file unchanged";      // SNORCA
    const bool need_wait = (heat && heat_wait) || (warm && warm_wait);                                        // SNORCA
    const long wait_at = !waits.empty() ? waits.front() : (first_mesh >= 0 ? first_mesh : first_layer);       // SNORCA
    if (need_wait && (wait_at < 0 || wait_at < start_at)) return "no place to wait for the chamber: file unchanged"; // SNORCA
    const bool need_end = heat || cover_filter || dragon_filter || (warm && delay > 0);                       // SNORCA
    const long end_at = block_end >= 0 ? block_end : (last_print_end >= 0 ? last_print_end + 1 : -1);         // SNORCA
    if (need_end && end_at < 0) return "no end of the print found: file unchanged";                           // SNORCA
    std::map<long, std::vector<std::string>> before;                                                          // SNORCA
    std::set<long> comment;                                                                                   // SNORCA
    if (cover) { comment.insert(purifier.begin(), purifier.end()); comment.insert(waits.begin(), waits.end()); } // SNORCA
    if (heat) comment.insert(slicer_chamber.begin(), slicer_chamber.end());                                   // SNORCA
    const std::string T = std::to_string(heat_t), W = std::to_string(warm_t);                                 // SNORCA
    auto &s = before[start_at];                                                                               // SNORCA
    s.push_back("; SNORCA print options: chamber at print start");                                              // SNORCA
    if (dragon_filter) s.push_back("SET_PIN PIN=dragonbreath_filter VALUE=1");                                // SNORCA (2026-11-13): before M141
    if (heat) s.push_back("M141 S" + T);                                                                      // SNORCA
    if (warm) s.push_back(std::string("SET_PURIFIER_MODE MODE=") + (warm_wait ? "2" : "3") + " DESIRE_TEMP=" + W + " FAN_SPEED=0.6"); // SNORCA
    else if (cover_filter) s.push_back("SET_FAN_SPEED FAN=circulation_fan SPEED=0.6");                        // SNORCA
    if (exhaust) s.push_back("SET_FAN_SPEED FAN=exhaust_fan SPEED=0.6");                                      // SNORCA
    if (need_wait) {                                                                                          // SNORCA
        auto &w = before[wait_at];                                                                            // SNORCA
        w.push_back("; SNORCA print options: wait for the chamber");                                            // SNORCA
        if (heat && heat_wait) w.push_back("M191 S" + T);                                                     // SNORCA
        if (warm && warm_wait) w.push_back("WAIT_CHAMBER_TEMP TIMEOUT=180");                                  // SNORCA
    }                                                                                                         // SNORCA
    if (need_end) {                                                                                           // SNORCA
        auto &e = before[end_at];                                                                             // SNORCA
        e.push_back("; SNORCA print options: chamber at print end");                                            // SNORCA
        if (heat) e.push_back("M141 S0");                                                                     // SNORCA
        if (cover_filter) { e.push_back("SET_FAN_SPEED FAN=circulation_fan SPEED=0.6"); e.push_back("SET_PURIFIER_MODE MODE=0 INNER_FAN_DELAY_OFF=60"); } // SNORCA
        if (warm && delay > 0) e.push_back("SET_PURIFIER FAN=exhaust DELAY_OFF=" + std::to_string(delay));    // SNORCA
        if (dragon_filter) { e.push_back("G4 P60000"); e.push_back("SET_PIN PIN=dragonbreath_filter VALUE=0"); } // SNORCA
    }                                                                                                         // SNORCA
    const boost::filesystem::path tmp = path.string() + ".snorca-chamber";                                      // SNORCA
    {                                                                                                         // SNORCA
        std::ofstream out(tmp.string(), std::ios::binary); if (!out) return "could not rewrite the file: unchanged"; // SNORCA
        for (long i = 0; i < (long) L.size(); ++i) {                                                          // SNORCA
            const auto it = before.find(i);                                                                   // SNORCA
            if (it != before.end()) for (const auto &x : it->second) out << x << '\n';                        // SNORCA
            if (comment.count(i)) out << "; SNORCA print options: replaced: " << strip(L[i]) << '\n';           // SNORCA
            else out << L[i] << '\n';                                                                         // SNORCA
        }                                                                                                     // SNORCA
        if (!out) { out.close(); boost::system::error_code ec2; boost::filesystem::remove(tmp, ec2); return "could not rewrite the file: unchanged"; } // SNORCA
    }                                                                                                         // SNORCA
    boost::system::error_code ec; boost::filesystem::rename(tmp, path, ec);                                   // SNORCA
    if (ec) return "could not replace the file: unchanged";                                                   // SNORCA
    std::string note = "heater=" + (heater.empty() ? std::string("none") : heater) + " cover=" + (cover ? "yes" : "no") // SNORCA
        + " heat=" + (heat ? T + (heat_wait ? " wait" : " no-wait") : "off") + " filter=" + (cover_filter ? "cover" : dragon_filter ? "dragonbreath" : "off") // SNORCA
        + " exhaust=" + (exhaust ? "on" : "off") + " warm=" + (warm ? W + (warm_wait ? " wait" : " no-wait") : "off") // SNORCA
        + "; lines replaced " + std::to_string(comment.size());                                               // SNORCA
    return note;                                                                                              // SNORCA
}                                                                                                             // SNORCA

// 1 = started (printer reports printing), 0 = not started (safe to queue in the inbox), -1 = unknown.       // SNORCA
int MultiACE::snorca_auto_start(const PrintHostUpload &up, ProgressFn progress_fn) const                       // SNORCA
{                                                                                                             // SNORCA
    using nlohmann::json;                                                                                     // SNORCA
    std::string &reason = m_snorca_outcome.reason;                                                              // SNORCA
    auto get = [&](const std::string &path, json &out, std::string &err) {                                    // SNORCA
        bool ok = false; auto h = Http::get(make_url(path)); set_auth(h);                                     // SNORCA
        h.timeout_connect(5).timeout_max(20)                                                                  // SNORCA
            .on_complete([&](std::string b, unsigned) { try { out = json::parse(b); ok = true; } catch (...) { err = "unreadable answer"; } }) // SNORCA
            .on_error([&](std::string b, std::string e, unsigned s) { err = snorca_err(b, e, s); }).perform_sync(); // SNORCA
        return ok; };                                                                                         // SNORCA
    auto printer_state = [&](std::string &err) { json st; return get("multiace/api/state", st, err) ? st.value("printer_state", std::string("?")) : std::string(); }; // SNORCA
    std::string err;                                                                                          // SNORCA
    {   // What the job needs, read from the G-code itself (used tools + header colour/type lists).          // SNORCA
        std::ifstream in(up.source_path.string()); std::string l; std::set<int> used;                       // SNORCA
        std::vector<std::string> colours, types;                                                              // SNORCA
        auto split = [](const std::string &v) { std::vector<std::string> o; std::stringstream ss(v); std::string x;  // SNORCA
            while (std::getline(ss, x, ';')) o.push_back(snorca_low(x)); return o; };                           // SNORCA
        static const std::regex t_re("^T([0-9]+)\\s*$"), c_re("^;\\s*filament_colour\\s*=\\s*(.*)$"), y_re("^;\\s*filament_type\\s*=\\s*(.*)$"); // SNORCA
        std::smatch m;                                                                                        // SNORCA
        while (std::getline(in, l)) {                                                                         // SNORCA
            if (!l.empty() && l.back() == '\r') l.pop_back();                                                // SNORCA
            if (l.empty() || (l[0] != 'T' && l[0] != ';')) continue;                                          // SNORCA
            if (l[0] == 'T') { if (std::regex_match(l, m, t_re)) used.insert(std::stoi(m[1])); continue; }   // SNORCA
            if (l.compare(0, 11, "; filament_") != 0) continue;                                               // SNORCA
            if (std::regex_match(l, m, c_re)) colours = split(m[1]);                                          // SNORCA
            else if (std::regex_match(l, m, y_re)) types = split(m[1]);                                       // SNORCA
        }                                                                                                     // SNORCA
        for (int t : used) m_snorca_outcome.needs.push_back({"Filament " + std::to_string(t + 1),              // SNORCA
            t < (int) types.size() ? snorca_upper(types[t]) : "?", t < (int) colours.size() ? snorca_hex(colours[t]) : "?"}); // SNORCA
    }                                                                                                         // SNORCA
    json st;                                                                                                  // SNORCA
    if (!get("multiace/api/state", st, err)) { reason = "Could not read the printer state from multiACE (" + err + "); nothing was started."; return 0; } // SNORCA
    {   // What is loaded, from the state feed (replaced below by the preflight's own spool list).           // SNORCA
        const int b0 = st.contains("display_index_base") && st["display_index_base"].is_number_integer() ? st["display_index_base"].get<int>() : 1; // SNORCA
        for (const auto &th : st.value("toolheads", json::array()))                                           // SNORCA
            if (th.is_object() && th.value("feeder", false) && th.contains("material") && th["material"].is_string() && !th["material"].get<std::string>().empty()) // SNORCA
                m_snorca_outcome.loaded.push_back({"Head " + std::to_string(th.value("idx", 0) + b0) + " feeder", snorca_upper(th["material"].get<std::string>()), // SNORCA
                                                 snorca_hex(th.contains("color") && th["color"].is_string() ? th["color"].get<std::string>() : "")}); // SNORCA
        for (const auto &a : st.value("aces", json::array())) if (a.is_object())                              // SNORCA
            for (const auto &sl : a.value("slots", json::array()))                                            // SNORCA
                if (sl.is_object() && sl.contains("material") && sl["material"].is_string() && !sl["material"].get<std::string>().empty()) // SNORCA
                    m_snorca_outcome.loaded.push_back({"ACE " + std::to_string(a.value("idx", 0) + b0) + ", slot " + std::to_string(sl.value("idx", 0) + b0), // SNORCA
                        snorca_upper(sl["material"].get<std::string>()), snorca_hex(sl.contains("color") && sl["color"].is_string() ? sl["color"].get<std::string>() : "")}); // SNORCA
    }                                                                                                         // SNORCA
    const std::string ps = st.value("printer_state", std::string("?"));                                       // SNORCA
    if (ps == "printing" || ps == "paused" || ps == "busy" || ps == "?") {   // the page's own "can start" test // SNORCA
        reason = "The printer is " + (ps == "?" ? std::string("in an unknown state") : ps) + ", so nothing was started."; return 0; } // SNORCA
    const int base = st.contains("display_index_base") && st["display_index_base"].is_number_integer() ? st["display_index_base"].get<int>() : 1; // SNORCA
    // multiACE's own server-side preflight of the raw slicer export (the page's in-printer path).            // SNORCA
    std::string rep_body; bool rep_ok = false;                                                                // SNORCA
    auto pf = Http::post(make_url("multiace/api/preflight")); set_auth(pf);                                   // SNORCA
    pf.form_add_file("file", up.source_path.string(), up.upload_path.filename().string())                    // SNORCA
        .timeout_connect(5).timeout_max(900)                                                                  // SNORCA
        .on_complete([&](std::string b, unsigned) { rep_body = std::move(b); rep_ok = true; })               // SNORCA
        .on_error([&](std::string b, std::string e, unsigned s) { err = snorca_err(b, e, s); })                 // SNORCA
        .on_progress([&](Http::Progress p, bool &cancel) { progress_fn(std::move(p), cancel); })             // SNORCA
        .perform_sync();                                                                                      // SNORCA
    if (!rep_ok) { reason = "multiACE could not plan the job (" + err + "), so nothing was started."; return 0; } // SNORCA
    try {                                                                                                     // SNORCA
        const json rr = json::parse(rep_body); const json tg = rr.value("targets", json::array());           // SNORCA
        if (tg.is_array() && !tg.empty()) {                                                                   // SNORCA
            m_snorca_outcome.loaded.clear();                                                                    // SNORCA
            for (const auto &g : tg) m_snorca_outcome.loaded.push_back({g.value("kind", "") == "pin"           // SNORCA
                ? "Head " + std::to_string(g.value("head", 0) + base) + " feeder"                             // SNORCA
                : "ACE " + std::to_string(g.value("ace", 0) + base) + ", slot " + std::to_string(g.value("slot", 0) + base), // SNORCA
                snorca_upper(g.value("material", std::string("?"))), snorca_hex(g.value("color", std::string()))}); // SNORCA
        }                                                                                                     // SNORCA
    } catch (...) {}                                                                                          // SNORCA
    std::map<std::string, std::string> asn;                                                                   // SNORCA
    if (!snorca_assign_head_targets(rep_body, base, asn, reason, m_snorca_outcome.summary, &m_snorca_outcome.plan)) { reason = "Not started: " + reason; return 0; } // SNORCA
    json rep = json::parse(rep_body);                                                                         // SNORCA
    const std::string token = rep.contains("token") && rep["token"].is_string() ? rep["token"].get<std::string>() : std::string(); // SNORCA
    if (token.empty()) { reason = "multiACE sent no job token, so nothing was started."; return 0; }          // SNORCA
    // Same body the page sends for its loadout plan with an edited colour->target table; no colour copies,  // SNORCA
    // camera off. Bed mesh and flow calibration come from the print options window (2026-11-13); without   // SNORCA
    // that choice they stay off, as before.                                                                  // SNORCA
    const std::string bed_choice = snorca_choice(up, "snorca_print_bed_calibration");                           // SNORCA
    const bool bed_mesh = bed_choice == "area" || bed_choice == "full";                                       // SNORCA
    const bool flow_cal = snorca_choice(up, "snorca_print_flow_calibration") == "1";                            // SNORCA
    const json body = {{"token", token}, {"mode", "head"}, {"head_plan", "loadout"}, {"head_assignment", asn}, // SNORCA
                       {"head_copies", json::object()}, {"bed_mesh", bed_mesh}, {"camera", false}, {"flow_cal", flow_cal}}; // SNORCA
    BOOST_LOG_TRIVIAL(warning) << "multiACE: print options bed=" << (bed_choice.empty() ? "(not asked)" : bed_choice) << " bed_mesh=" << bed_mesh << " flow_cal=" << flow_cal; // SNORCA
    std::string job_id; bool start_ok = false;                                                                // SNORCA
    auto sp = Http::post(make_url("multiace/api/preflight/print")); set_auth(sp);                             // SNORCA
    sp.header("Content-Type", "application/json").set_post_body(body.dump()).timeout_connect(5).timeout_max(120) // SNORCA
        .on_complete([&](std::string b, unsigned) { try { auto j = json::parse(b); job_id = j.at("job_id").is_string() ? j["job_id"].get<std::string>() : j["job_id"].dump(); start_ok = true; } catch (...) { err = "no job id in the answer"; } }) // SNORCA
        .on_error([&](std::string b, std::string e, unsigned s) { err = snorca_err(b, e, s); }).perform_sync(); // SNORCA
    if (!start_ok) { reason = "multiACE refused to start the job (" + err + "), so nothing was started."; return 0; } // SNORCA
    BOOST_LOG_TRIVIAL(warning) << "multiACE: automatic start requested: " << m_snorca_outcome.summary;          // SNORCA
    // multiACE now rewrites the file and hands it to the printer; follow its status like the page does.     // SNORCA
    bool done = false;                                                                                        // SNORCA
    for (int i = 0; i < 1200 && !done; ++i) {                                                                 // SNORCA
        std::this_thread::sleep_for(std::chrono::milliseconds(i == 0 ? 250 : 1000));                          // SNORCA
        json s; std::string e2; unsigned code = 0;                                                            // SNORCA
        auto h = Http::get(make_url("multiace/api/preflight/print/status?job_id=" + Http::url_encode(job_id))); set_auth(h); // SNORCA
        h.timeout_connect(5).timeout_max(20)                                                                  // SNORCA
            .on_complete([&](std::string b, unsigned) { try { s = json::parse(b); } catch (...) {} })         // SNORCA
            .on_error([&](std::string b, std::string e, unsigned c) { e2 = snorca_err(b, e, c); code = c; }).perform_sync(); // SNORCA
        if (code >= 400) { reason = "multiACE could not prepare the job (" + e2 + "), so nothing was started."; return 0; } // SNORCA
        if (!s.is_object() || !s.value("done", false)) continue;                                              // SNORCA
        if (s.contains("error") && !s["error"].is_null() && s["error"] != false && s["error"] != "") {        // SNORCA
            reason = "multiACE could not prepare the job (" + (s["error"].is_string() ? s["error"].get<std::string>() : s["error"].dump()) + "), so nothing was started."; return 0; } // SNORCA
        done = true;                                                                                          // SNORCA
    }                                                                                                         // SNORCA
    if (!done) { reason = "multiACE did not finish preparing the job within 20 minutes. Check the printer before sending again."; return -1; } // SNORCA
    for (int i = 0; i < 60; ++i) {                                                                            // SNORCA
        std::string e3; const std::string now = printer_state(e3);                                            // SNORCA
        if (now == "printing") return 1;                                                                      // SNORCA
        std::this_thread::sleep_for(std::chrono::seconds(2));                                                 // SNORCA
    }                                                                                                         // SNORCA
    reason = "multiACE handed the job to the printer, but the printer has not reported printing after 2 minutes. Check the printer before sending again."; // SNORCA
    return -1;                                                                                                // SNORCA
}                                                                                                             // SNORCA

bool MultiACE::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    (void) info_fn;
    // SNORCA: plan and start automatically first; only a job that was NOT started goes to the inbox (page).
    m_snorca_outcome = SnorcaOutcome();                                                                           // SNORCA
    const std::string snorca_heat = snorca_heat_before_initial_load(upload_data.source_path);                     // SNORCA
    BOOST_LOG_TRIVIAL(warning) << "multiACE: heat before initial load: " << (snorca_heat.empty() ? "pattern not found, file unchanged" : snorca_heat); // SNORCA
    const std::string snorca_bed = snorca_choice(upload_data, "snorca_print_bed_calibration");                    // SNORCA (2026-11-13)
    BOOST_LOG_TRIVIAL(warning) << "multiACE: bed calibration: " << snorca_apply_bed_choice(upload_data.source_path, snorca_bed); // SNORCA
    BOOST_LOG_TRIVIAL(warning) << "multiACE: chamber: " << snorca_apply_chamber_choice(upload_data.source_path, upload_data); // SNORCA (2026-11-13)
    const int snorca = snorca_auto_start(upload_data, progress_fn);                                               // SNORCA
    BOOST_LOG_TRIVIAL(warning) << "multiACE: automatic start result " << snorca << ": " << m_snorca_outcome.reason; // SNORCA
    if (snorca == 1) { m_snorca_outcome.started = true; return true; }                                            // SNORCA
    if (snorca < 0) return true;   // start state unknown: never queue a second copy                            // SNORCA

    const char *name = get_name();
    const auto upload_filename = upload_data.upload_path.filename();
    std::string url = make_url(inbox_path());
    bool res = true;

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% to %3%, filename: %4%")
        % name % upload_data.source_path % url % upload_filename.string();

    auto http = Http::post(std::move(url));
    set_auth(http);
    // Field name and shape must match multiACE's documented curl exactly.
    http.form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: File uploaded: HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading file: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            if (status == 404 || status == 405) {
                // multiACE mounts its web UI as a catch-all at /, and StaticFiles
                // answers GET/HEAD only, so an unknown path returns 405 rather
                // than 404. Either code means this build predates the inbox.
                error_fn(GUI::format_wxstr(
                    _L("This printer is running a version of multiACE that has no send-to-multiACE "
                       "inbox (HTTP %s). Update multiACE on the printer to 0.99.7b or newer, then "
                       "try again."),
                    std::to_string(status)));
            } else {
                // multiACE returns {"detail": "..."} on every other rejection;
                // format_error surfaces the body so the server's own wording
                // reaches the user -- notably the 409 for G-code that has
                // already been through multiACE's post-processor.
                error_fn(format_error(body, error, status));
            }
            res = false;
        })
        .on_progress([&](Http::Progress progress, bool &cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                BOOST_LOG_TRIVIAL(info) << name << ": Upload canceled";
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    m_snorca_outcome.stored = res;                                                                              // SNORCA
    return res;
}

// FOS: topology fetch. See TopologyReport in the header for the contract.
MultiACE::TopologyReport MultiACE::fetch_topology(std::string host)
{
    TopologyReport r;

    // mms_host was per-extruder up to v2.3.2-fos.8.5.7-beta.2 and a stored vector value
    // arrives verbatim as "a,a,a,a" - the same truncation fos_send_to_mms applies.
    const size_t comma = host.find(',');
    if (comma != std::string::npos)
        host.erase(comma);
    while (!host.empty() && (host.front() == ' ' || host.front() == '\t')) host.erase(host.begin());
    while (!host.empty() && (host.back()  == ' ' || host.back()  == '\t')) host.pop_back();
    if (host.empty()) {
        r.error = "no printer LAN IP set";
        return r;
    }

    std::string url;
    if (host.rfind("http://", 0) == 0 || host.rfind("https://", 0) == 0)
        url = host + (host.back() == '/' ? "" : "/") + livedata_path();
    else
        url = "http://" + host + "/" + livedata_path();

    BOOST_LOG_TRIVIAL(info) << boost::format("MultiACE: fetching topology from %1%") % url;

    std::string body;
    long        status = 0;
    bool        done   = false;
    auto http = Http::get(std::move(url));
    http.timeout_connect(3)
        .timeout_max(6)
        .on_complete([&](std::string b, unsigned s) { body = std::move(b); status = s; done = true; })
        .on_error([&](std::string b, std::string e, unsigned s) { body = std::move(b); r.error = std::move(e); status = s; })
        .perform_sync();

    if (status == 409) {
        // "preflight is disabled while a head is set to manual" - by design, not an error.
        r.manual_hold = true;
        return r;
    }
    if (!done) {
        if (r.error.empty())
            r.error = "no response";
        if (status != 0)
            r.error += " (HTTP " + std::to_string(status) + ")";
        return r;
    }

    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        const nlohmann::json &ctx = j.at("head_ctx");

        r.mode = ctx.value("mode", "normal");
        if (r.mode == "single")
            r.mode = "multi";   // ace.py rewrites single to multi; mirror it
        if (r.mode != "normal" && r.mode != "multi" && r.mode != "head") {
            r.error = "unknown supply mode \"" + r.mode + "\"";
            r.mode.clear();
            return r;
        }

        // Which heads a unit drives. ace_heads is the list; the legacy single-ACE field
        // ace_head is the same fallback head_maps() itself applies. head_ace is identity-
        // padded outside head mode and deliberately never read here.
        std::vector<int> ace_heads;
        if (ctx.contains("ace_heads") && ctx["ace_heads"].is_array())
            for (const auto &v : ctx["ace_heads"])
                ace_heads.push_back(v.get<int>());
        if (ace_heads.empty() && r.mode == "head" && ctx.contains("ace_head"))
            ace_heads.push_back(ctx["ace_head"].get<int>());
        int head_n = 4;
        for (int h : ace_heads)
            head_n = std::max(head_n, h + 1);
        r.ace_driven_heads.assign(size_t(head_n), false);
        for (int h : ace_heads)
            if (h >= 0)
                r.ace_driven_heads[size_t(h)] = true;

        // Supply, head-indexed, read off Klipper's extruder objects. String keys.
        if (ctx.contains("head_nozzles") && ctx["head_nozzles"].is_object())
            for (const auto &el : ctx["head_nozzles"].items()) {
                const int idx = std::atoi(el.key().c_str());
                if (idx < 0 || idx > 15)
                    continue;
                if (int(r.head_nozzles.size()) <= idx)
                    r.head_nozzles.resize(size_t(idx) + 1, 0.);
                r.head_nozzles[size_t(idx)] = el.value().get<double>();
            }

        // Units seen. live_slots drops empty and non-identified slots, so this is a
        // LOWER BOUND on the connected count - the caller's dialog says so.
        std::set<int> units;
        if (j.contains("live_slots") && j["live_slots"].is_array())
            for (const auto &s : j["live_slots"])
                if (s.contains("ace") && s["ace"].is_number())
                    units.insert(s["ace"].get<int>());
        r.units_seen = int(units.size());

        r.ok = true;
    } catch (const std::exception &e) {
        r.error = std::string("unexpected reply: ") + e.what();
    }
    return r;
}

} // namespace Slic3r
