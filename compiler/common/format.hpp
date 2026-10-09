#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// Shortest decimal spelling that round-trips through a 32-bit float, always
// containing a '.' or exponent so it reads (and parses in WGSL) as a float.
inline std::string format_float(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    float f = static_cast<float>(v);
    char buf[64];
    for (int prec = 1; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, static_cast<double>(f));
        if (std::strtof(buf, nullptr) == f) break;
    }
    std::string s = buf;
    // Prefer plain positional notation for everyday magnitudes: 20.0, not 2e+01.
    double a = std::fabs(static_cast<double>(f));
    if (s.find('e') != std::string::npos && a >= 1e-4 && a < 1e9) {
        for (int decimals = 0; decimals <= 12; ++decimals) {
            std::snprintf(buf, sizeof buf, "%.*f", decimals, static_cast<double>(f));
            if (std::strtof(buf, nullptr) == f) break;
        }
        s = buf;
    }
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    // "1e+10" → "1e10"; WGSL accepts both, this reads better.
    auto plus = s.find("e+");
    if (plus != std::string::npos) s.erase(plus + 1, 1);
    return s;
}
