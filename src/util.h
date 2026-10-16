#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace util {

inline std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Splits on a delimiter, keeping empty fields, trimming each field.
inline std::vector<std::string> split(const std::string &s, char d = ',') {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == d) { out.push_back(trim(cur)); cur.clear(); }
        else cur += c;
    }
    out.push_back(trim(cur));
    return out;
}

inline std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

inline bool toDouble(const std::string &s, double &out) {
    if (s.empty()) return false;
    char *end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || std::isnan(v)) return false;
    out = v;
    return true;
}

inline bool toInt(const std::string &s, int &out) {
    double d;
    if (!toDouble(s, d) || d != std::floor(d)) return false;
    out = (int)d;
    return true;
}

// Great-circle distance in km (used by vehicles that do not follow roads).
inline double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0;
const double k = 3.14159265358979323846 / 180.0;
    double dLat = (lat2 - lat1) * k, dLon = (lon2 - lon1) * k;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * k) * std::cos(lat2 * k) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * R * std::asin(std::sqrt(a));
}

} // namespace util
