#pragma once
#include "config.h"
#include "heap.h"
#include "node.h"
#include "report.h"
#include "road_network.h"
#include "vehicle.h"
#include <memory>
#include <string>
#include <vector>

struct DamReleaseEvent {
    double releaseM3s = 0.0;
    double warningThresholdM3s = 0.0;
    bool configured = false;
};


// Everything the program knows about the current disaster.
struct Model {
    DamReleaseEvent damRelease;
    Config cfg;
    NodeRegistry nodes;
    RoadNetwork roads;
    std::vector<std::unique_ptr<Vehicle>> fleet;
    ReportManager reports;
    std::string dataDir = "data";
};

// Zone ranking backed by the C max-heap (score = priority).
class PriorityManager {
    Heap *h_;

public:
    explicit PriorityManager(int maxId) : h_(heap_create(maxId, 1)) {}
    ~PriorityManager() { heap_free(h_); }
    PriorityManager(const PriorityManager &) = delete;
    PriorityManager &operator=(const PriorityManager &) = delete;
    void update(int id, double score) { heap_push_or_update(h_, id, score); }
    bool pop(int &id, double &score) {
        HeapItem it;
        if (!heap_pop(h_, &it)) return false;
        id = it.id;
        score = it.key;
        return true;
    }
};

struct SupplyAssignment {
    int zone = -1;
    std::string status;            // ASSIGNED, NO EVIDENCE, NO DEMAND, NO VEHICLE
    std::string vehicleId, vehicleType, depotId;
    double demand = 0, load = 0, km = 0, etaMin = 0;
    std::vector<int> path;
};

struct EvacAssignment {
    int zone = -1, shelter = -1;   // shelter = -1 when nobody could be placed
    double people = 0, km = 0;
    std::string status;            // EVACUATE, NO SHELTER CAPACITY, UNREACHABLE
    std::vector<int> path;
};

class Engine {
public:
    // Push current rain/flood values into road speeds and closures.
    static void refreshRoads(Model &m);
    // Verify reports, compute confidence, impact score and level for every zone.
    static void assess(Model &m);
    // Zone indices ordered by score, highest first.
    static std::vector<int> priorityOrder(Model &m);
    static std::vector<SupplyAssignment> dispatchSupplies(Model &m);
    static std::vector<EvacAssignment> evacuate(Model &m);
};
