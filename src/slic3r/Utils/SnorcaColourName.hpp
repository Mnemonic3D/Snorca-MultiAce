// SNORCA (user request 2026-11-12): plain English colour names for the multiACE windows, so a row reads "Black ASA"  SNORCA
// instead of "#000000". Local only: a fixed list of everyday names, nearest by CIEDE2000 in CIE Lab (D65).  SNORCA
// Header-only and free of wx, so it is unit-tested on its own. The slicer's ColorSpaceConvert has Lab and     SNORCA
// DeltaE helpers but no colour names, and that file pulls in wx, so the short sRGB->Lab step is repeated here. SNORCA
#ifndef slic3r_Utils_SnorcaColourName_hpp_                                                                       // SNORCA
#define slic3r_Utils_SnorcaColourName_hpp_                                                                       // SNORCA
#include <cctype>                                                                                              // SNORCA
#include <cmath>                                                                                               // SNORCA
#include <string>                                                                                              // SNORCA
                                                                                                               // SNORCA
namespace Slic3r { namespace SnorcaColour {                                                                      // SNORCA
// "#RRGGBB", "RRGGBB" or "#RRGGBBAA" (alpha ignored), any case. False for anything else.                      // SNORCA
inline bool parse(const std::string &in, int rgb[3])                                                           // SNORCA
{                                                                                                              // SNORCA
    std::string h = in; while (!h.empty() && (h[0] == '#' || h[0] == ' ')) h.erase(0, 1);                     // SNORCA
    while (!h.empty() && h.back() == ' ') h.pop_back();                                                        // SNORCA
    if (h.size() != 6 && h.size() != 8) return false;                                                         // SNORCA
    for (char c : h) if (!std::isxdigit((unsigned char) c)) return false;                                      // SNORCA
    for (int k = 0; k < 3; ++k) rgb[k] = std::stoi(h.substr(2 * k, 2), nullptr, 16);                           // SNORCA
    return true;                                                                                               // SNORCA
}                                                                                                              // SNORCA
inline void lab(const int rgb[3], double out[3])                                                               // SNORCA
{                                                                                                              // SNORCA
    double c[3];                                                                                               // SNORCA
    for (int k = 0; k < 3; ++k) { const double n = rgb[k] / 255.0; c[k] = n > 0.04045 ? std::pow((n + 0.055) / 1.055, 2.4) : n / 12.92; } // SNORCA
    const double x = (0.4124 * c[0] + 0.3576 * c[1] + 0.1805 * c[2]) / 0.95047;                                // SNORCA
    const double y =  0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];                                           // SNORCA
    const double z = (0.0193 * c[0] + 0.1192 * c[1] + 0.9505 * c[2]) / 1.08883;                                // SNORCA
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };                  // SNORCA
    out[0] = 116.0 * f(y) - 16.0; out[1] = 500.0 * (f(x) - f(y)); out[2] = 200.0 * (f(y) - f(z));             // SNORCA
}                                                                                                              // SNORCA
// CIEDE2000 colour difference. About 2 is the smallest difference most people notice.                        // SNORCA
inline double delta_e(const int p[3], const int q[3])                                                          // SNORCA
{                                                                                                              // SNORCA
    const double PI = 3.14159265358979323846, D = PI / 180.0;                                                 // SNORCA
    double a[3], b[3]; lab(p, a); lab(q, b);                                                                   // SNORCA
    const double C1 = std::hypot(a[1], a[2]), C2 = std::hypot(b[1], b[2]), Cb = (C1 + C2) / 2;                // SNORCA
    const double G = 0.5 * (1 - std::sqrt(std::pow(Cb, 7) / (std::pow(Cb, 7) + std::pow(25.0, 7))));          // SNORCA
    const double a1 = a[1] * (1 + G), a2 = b[1] * (1 + G);                                                     // SNORCA
    const double C1p = std::hypot(a1, a[2]), C2p = std::hypot(a2, b[2]);                                       // SNORCA
    auto hue = [&](double y, double x) { double h = std::atan2(y, x) / D; return h < 0 ? h + 360 : h; };       // SNORCA
    const double h1 = hue(a[2], a1), h2 = hue(b[2], a2);                                                       // SNORCA
    double dh = 0; if (C1p * C2p != 0) { dh = h2 - h1; if (dh > 180) dh -= 360; else if (dh < -180) dh += 360; } // SNORCA
    const double dL = b[0] - a[0], dC = C2p - C1p, dH = 2 * std::sqrt(C1p * C2p) * std::sin(dh / 2 * D);       // SNORCA
    const double Lb = (a[0] + b[0]) / 2, Cbp = (C1p + C2p) / 2;                                                // SNORCA
    double hb = h1 + h2;                                                                                       // SNORCA
    if (C1p * C2p != 0) hb = std::abs(h1 - h2) <= 180 ? hb / 2 : (hb < 360 ? (hb + 360) / 2 : (hb - 360) / 2); // SNORCA
    const double T = 1 - 0.17 * std::cos((hb - 30) * D) + 0.24 * std::cos(2 * hb * D) + 0.32 * std::cos((3 * hb + 6) * D) - 0.20 * std::cos((4 * hb - 63) * D); // SNORCA
    const double dt = 30 * std::exp(-std::pow((hb - 275) / 25, 2));                                            // SNORCA
    const double Rc = 2 * std::sqrt(std::pow(Cbp, 7) / (std::pow(Cbp, 7) + std::pow(25.0, 7)));               // SNORCA
    const double Sl = 1 + 0.015 * std::pow(Lb - 50, 2) / std::sqrt(20 + std::pow(Lb - 50, 2));                 // SNORCA
    const double Sc = 1 + 0.045 * Cbp, Sh = 1 + 0.015 * Cbp * T, Rt = -std::sin(2 * dt * D) * Rc;              // SNORCA
    return std::sqrt(std::pow(dL / Sl, 2) + std::pow(dC / Sc, 2) + std::pow(dH / Sh, 2) + Rt * (dC / Sc) * (dH / Sh)); // SNORCA
}                                                                                                              // SNORCA
// The nearest everyday name (35 names). Empty when the colour cannot be read.                                // SNORCA
inline std::string name(const std::string &hex)                                                                // SNORCA
{                                                                                                              // SNORCA
    static const struct { const char *name; int r, g, b; } NAMES[] = {                                        // SNORCA
        {"Black", 0x00, 0x00, 0x00}, {"Dark grey", 0x40, 0x40, 0x40}, {"Grey", 0x80, 0x80, 0x80},             // SNORCA
        {"Light grey", 0xC0, 0xC0, 0xC0}, {"White", 0xFF, 0xFF, 0xFF}, {"Cream", 0xF3, 0xEA, 0xC8},            // SNORCA
        {"Beige", 0xD8, 0xCB, 0xA6}, {"Tan", 0xC4, 0x9A, 0x6C}, {"Light brown", 0xA8, 0x78, 0x50},              // SNORCA
        {"Brown", 0x74, 0x47, 0x1F}, {"Dark brown", 0x42, 0x29, 0x1A}, {"Dark red", 0x8B, 0x10, 0x10},          // SNORCA
        {"Red", 0xE0, 0x18, 0x18}, {"Coral", 0xFF, 0x7F, 0x66}, {"Pink", 0xFF, 0xA0, 0xC0},                    // SNORCA
        {"Hot pink", 0xFF, 0x3C, 0xA0}, {"Magenta", 0xE0, 0x00, 0xE0}, {"Orange", 0xFF, 0x8C, 0x00},           // SNORCA
        {"Peach", 0xFF, 0xC5, 0x9E}, {"Gold", 0xC9, 0xA2, 0x27}, {"Yellow", 0xFF, 0xE1, 0x19},                 // SNORCA
        {"Light yellow", 0xFF, 0xF5, 0x9A}, {"Olive", 0x7A, 0x7A, 0x1E}, {"Lime green", 0x9B, 0xE0, 0x1C},      // SNORCA
        {"Light green", 0x90, 0xE0, 0x90}, {"Green", 0x1E, 0x9A, 0x2E}, {"Dark green", 0x0F, 0x4D, 0x24},       // SNORCA
        {"Teal", 0x00, 0x80, 0x80}, {"Turquoise", 0x40, 0xD0, 0xC8}, {"Light blue", 0x90, 0xC8, 0xF0},          // SNORCA
        {"Sky blue", 0x4A, 0xA8, 0xE8}, {"Blue", 0x1E, 0x4F, 0xD0}, {"Navy blue", 0x1A, 0x25, 0x60},            // SNORCA
        {"Purple", 0x70, 0x30, 0xA0}, {"Lavender", 0xC0, 0xA8, 0xE8}};                                        // SNORCA
    int c[3]; if (!parse(hex, c)) return std::string();                                                        // SNORCA
    const char *best = ""; double bd = 1e9;                                                                    // SNORCA
    for (const auto &n : NAMES) { const int q[3] = {n.r, n.g, n.b}; const double d = delta_e(c, q); if (d < bd) { bd = d; best = n.name; } } // SNORCA
    return best;                                                                                               // SNORCA
}                                                                                                              // SNORCA
// True when two colours look the same to most people (CIEDE2000 up to 2.3); false if either cannot be read.  SNORCA
inline bool same(const std::string &x, const std::string &y)                                                   // SNORCA
{                                                                                                              // SNORCA
    int a[3], b[3]; return parse(x, a) && parse(y, b) && delta_e(a, b) <= 2.3;                                 // SNORCA
}                                                                                                              // SNORCA
}} // namespace Slic3r::SnorcaColour                                                                             // SNORCA
#endif                                                                                                         // SNORCA
