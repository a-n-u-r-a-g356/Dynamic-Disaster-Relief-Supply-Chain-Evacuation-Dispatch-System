#pragma once
#include "config.h"
#include "node.h"
#include <string>

// Abstract vehicle. Subclasses decide which flood depths they can work in
// and whether they follow roads or fly straight.
class Vehicle {
protected:
    std::string id_, home_;
    double capacity_, speedKmh_;
    bool available_ = true;

public:
    Vehicle(std::string id, std::string home, double capacity, double speed)
        : id_(std::move(id)), home_(std::move(home)), capacity_(capacity), speedKmh_(speed) {}
    virtual ~Vehicle() = default;
    virtual bool canServe(const ZoneNode &z, const Config &cfg) const = 0;
    virtual bool usesRoads() const = 0;
    virtual const char *type() const = 0;

    const std::string &id() const { return id_; }
    const std::string &home() const { return home_; }
    double capacity() const { return capacity_; }
    double speedKmh() const { return speedKmh_; }
    bool available() const { return available_; }
    void setAvailable(bool a) { available_ = a; }
    double etaMinutes(double km) const { return speedKmh_ > 0 ? km / speedKmh_ * 60.0 : 0; }
};

class Truck : public Vehicle {
public:
    using Vehicle::Vehicle;
    bool canServe(const ZoneNode &z, const Config &c) const override { return z.floodDepth <= c.get("vehicle.truckMaxDepth"); }
    bool usesRoads() const override { return true; }
    const char *type() const override { return "TRUCK"; }
};

class Boat : public Vehicle {
public:
    using Vehicle::Vehicle;
    bool canServe(const ZoneNode &z, const Config &c) const override { return z.floodDepth >= c.get("vehicle.boatMinDepth"); }
    bool usesRoads() const override { return true; }
    const char *type() const override { return "BOAT"; }
};

class Helicopter : public Vehicle {
public:
    using Vehicle::Vehicle;
    bool canServe(const ZoneNode &, const Config &) const override { return true; }
    bool usesRoads() const override { return false; }
    const char *type() const override { return "HELI"; }
};
