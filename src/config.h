#pragma once
#include "util.h"
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

// Every tunable number lives in <data>/config.ini (key = value, '#' comments).
// The defaults below only keep the program running if a key is missing from the file.
class Config {
    std::map<std::string, double> num_;
    std::map<std::string, std::string> str_;

public:
    Config() { defaults(); }

    void defaults() {
        // impact score: weights and the value that counts as "maximum" for each factor
        num_["weight.population"] = 0.40; num_["weight.depth"] = 0.35; num_["weight.rain"] = 0.25;
        num_["norm.population"] = 10000;  num_["norm.depth"] = 3;      num_["norm.rain"] = 100;
        num_["rain.nowFactor"] = 3;
        // classification thresholds (score 0-100)
        num_["level.moderate"] = 20; num_["level.high"] = 40; num_["level.critical"] = 60;
        // report trust
        num_["trust.sensor"] = 0.90; num_["trust.citizenBase"] = 0.35;
        num_["trust.citizenPerSupporter"] = 0.15; num_["trust.citizenMax"] = 0.90;
        num_["trust.weatherStrong"] = 0.80; num_["trust.weatherWeak"] = 0.30;
        num_["weather.strongRain24h"] = 20;
        // roads
        num_["road.rainSlow"] = 0.20; num_["road.floodSlow"] = 0.25;
        num_["road.maxFactor"] = 4;   num_["road.closeRainMmH"] = 30;
        // vehicles
        num_["vehicle.truckMaxDepth"] = 0.5; num_["vehicle.boatMinDepth"] = 0.5;
        // supplies and evacuation
        num_["supply.perPersonPerDay"] = 3; num_["supply.days"] = 1;
        num_["affect.fullDepth"] = 2.0; num_["evac.minScore"] = 40;
       str_["weather.command"] = "py tools/fetch_weather.py";
    }

    bool load(const std::string &path) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            size_t h = line.find('#');
            if (h != std::string::npos) line = line.substr(0, h);
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = util::trim(line.substr(0, eq)), val = util::trim(line.substr(eq + 1));
            if (key.empty()) continue;
            double d;
            if (util::toDouble(val, d)) num_[key] = d; else str_[key] = val;
        }
        return true;
    }

    double get(const std::string &key) const {
        auto it = num_.find(key);
        if (it == num_.end()) throw std::runtime_error("missing config key: " + key);
        return it->second;
    }
    std::string getStr(const std::string &key) const {
        auto it = str_.find(key);
        if (it == str_.end()) throw std::runtime_error("missing config key: " + key);
        return it->second;
    }
    void set(const std::string &key, double v) { num_[key] = v; }
    bool has(const std::string &key) const { return num_.count(key) > 0; }
    const std::map<std::string, double> &numbers() const { return num_; }
    const std::map<std::string, std::string> &strings() const { return str_; }
};
