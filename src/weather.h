#pragma once
#include "config.h"
#include "node.h"
#include <cstdlib>
#include <fstream>
#include <string>

// Live weather arrives through an external fetcher (tools/fetch_weather.py by default,
// configurable with weather.command). The fetcher writes <data>/weather.csv and this
// class loads it into the zones. Any other source that writes the same CSV works too.
class WeatherService {
public:
    static bool fetch(const Config &cfg, const std::string &dataDir) {
        std::string cmd = cfg.getStr("weather.command") + " \"" + dataDir + "\"";
        return std::system(cmd.c_str()) == 0;
    }

    // weather.csv: id,rain_now_mm,rain_24h_mm. Returns number of zones updated, -1 if file missing.
    static int load(const std::string &path, NodeRegistry &nodes) {
        std::ifstream f(path);
        if (!f) return -1;
        std::string line;
        int updated = 0;
        bool header = true;
        while (std::getline(f, line)) {
            line = util::trim(line);
            if (line.empty()) continue;
            if (header) { header = false; continue; }
            auto c = util::split(line);
            double now, h24;
            if (c.size() < 3 || !util::toDouble(c[1], now) || !util::toDouble(c[2], h24)) continue;
            if (ZoneNode *z = nodes.as<ZoneNode>(nodes.indexOf(c[0]))) {
                z->rainNow = now;
                z->rain24h = h24;
                updated++;
            }
        }
        return updated;
    }

    static void save(const std::string &path, NodeRegistry &nodes) {
        std::ofstream f(path);
        f << "id,rain_now_mm,rain_24h_mm\n";
        for (int i : nodes.indicesOf<ZoneNode>()) {
            ZoneNode *z = nodes.as<ZoneNode>(i);
            f << z->id() << "," << z->rainNow << "," << z->rain24h << "\n";
        }
    }
};
