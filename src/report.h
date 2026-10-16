#pragma once
#include "config.h"
#include "queue.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// Abstract report: each source type decides how much it is trusted.
class Report {
protected:
    std::string zone_;

public:
    explicit Report(std::string zone) : zone_(std::move(zone)) {}
    virtual ~Report() = default;
    virtual double confidence(const Config &cfg) const = 0;
    virtual const char *source() const = 0;
    virtual int supporters() const { return 0; }
    const std::string &zoneId() const { return zone_; }
};

class SensorReport : public Report {
public:
    using Report::Report;
    double confidence(const Config &c) const override { return c.get("trust.sensor"); }
    const char *source() const override { return "SENSOR"; }
};

class CitizenReport : public Report {
    int supporters_;

public:
    CitizenReport(std::string zone, int supporters) : Report(std::move(zone)), supporters_(supporters) {}
    double confidence(const Config &c) const override {
        double v = c.get("trust.citizenBase") + c.get("trust.citizenPerSupporter") * supporters_;
        return std::min(v, c.get("trust.citizenMax"));
    }
    const char *source() const override { return "CITIZEN"; }
    int supporters() const override { return supporters_; }
};

// Created on the fly from live rainfall; never stored.
class WeatherReport : public Report {
    double rain24_;

public:
    WeatherReport(std::string zone, double rain24) : Report(std::move(zone)), rain24_(rain24) {}
    double confidence(const Config &c) const override {
        return rain24_ >= c.get("weather.strongRain24h") ? c.get("trust.weatherStrong")
                                                         : c.get("trust.weatherWeak");
    }
    const char *source() const override { return "WEATHER"; }
};

// New reports wait in a C FIFO queue (intake) and are accepted in arrival order.
class ReportManager {
    Queue *incoming_;
    std::vector<std::unique_ptr<Report>> accepted_;

public:
    ReportManager() : incoming_(queue_create()) {}
    ~ReportManager() { drain(); queue_free(incoming_); }
    ReportManager(const ReportManager &) = delete;
    ReportManager &operator=(const ReportManager &) = delete;

    void submit(std::unique_ptr<Report> r) { queue_push(incoming_, r.release()); }
    int pending() const { return queue_size(incoming_); }
    void drain() {
        while (void *p = queue_pop(incoming_)) accepted_.emplace_back(static_cast<Report *>(p));
    }
    const std::vector<std::unique_ptr<Report>> &accepted() {
        drain();
        return accepted_;
    }
    size_t clearZone(const std::string &zone) {
        drain();
        size_t before = accepted_.size();
        accepted_.erase(std::remove_if(accepted_.begin(), accepted_.end(),
                                       [&](const std::unique_ptr<Report> &r) { return r->zoneId() == zone; }),
                        accepted_.end());
        return before - accepted_.size();
    }
    void clearAll() { drain(); accepted_.clear(); }
};
