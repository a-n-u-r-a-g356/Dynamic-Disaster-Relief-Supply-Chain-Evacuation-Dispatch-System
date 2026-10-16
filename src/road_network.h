#pragma once
#include "config.h"
#include "graph.h"
#include <algorithm>
#include <vector>

struct Road {
    int u, v;
    double km;
    bool blocked = false;          // set manually by the coordinator
    bool weatherClosed = false;    // set automatically by heavy rain
    double factor = 1.0;           // weather slow-down multiplier
    bool open() const { return !blocked && !weatherClosed; }
};

struct Paths {
    std::vector<double> dist;
    std::vector<int> prev;
};

struct Route {
    std::vector<int> nodes;
    double cost = 0;
    bool ok = false;
};

// Keeps the road list and a C graph built from it. The graph is rebuilt lazily
// whenever a road is added, blocked or re-weighted.
class RoadNetwork {
    std::vector<Road> roads_;
    Graph *g_ = nullptr;
    bool dirty_ = true;
    int nodeCount_ = 0;

    void rebuild() {
        graph_free(g_);
        g_ = graph_create(nodeCount_);
        for (const Road &r : roads_)
            if (r.open()) graph_add_road(g_, r.u, r.v, r.km * r.factor);
        dirty_ = false;
    }
    Road *find(int u, int v) {
        for (Road &r : roads_)
            if ((r.u == u && r.v == v) || (r.u == v && r.v == u)) return &r;
        return nullptr;
    }

public:
    RoadNetwork() = default;
    ~RoadNetwork() { graph_free(g_); }
    RoadNetwork(const RoadNetwork &) = delete;
    RoadNetwork &operator=(const RoadNetwork &) = delete;

    void clear() { roads_.clear(); dirty_ = true; }
    void setNodeCount(int n) { if (n != nodeCount_) { nodeCount_ = n; dirty_ = true; } }
    const std::vector<Road> &all() const { return roads_; }

    bool addRoad(int u, int v, double km, bool blocked = false) {
        if (u == v || km < 0) return false;
        if (Road *r = find(u, v)) { r->km = km; r->blocked = blocked; }
        else roads_.push_back({u, v, km, blocked});
        dirty_ = true;
        return true;
    }
    bool removeRoad(int u, int v) {
        Road *r = find(u, v);
        if (!r) return false;
        roads_.erase(roads_.begin() + (r - roads_.data()));
        dirty_ = true;
        return true;
    }
    bool setBlocked(int u, int v, bool b) {
        Road *r = find(u, v);
        if (!r) return false;
        r->blocked = b;
        dirty_ = true;
        return true;
    }

    // Rain and flooding slow a road down; extreme rain closes it.
    void applyWeather(const std::vector<double> &rainPerNode, const std::vector<double> &floodPerNode, const Config &cfg) {
        for (Road &r : roads_) {
            double rain = std::max(rainPerNode[(size_t)r.u], rainPerNode[(size_t)r.v]);
            double flood = std::max(floodPerNode[(size_t)r.u], floodPerNode[(size_t)r.v]);
            r.factor = std::min(1.0 + rain * cfg.get("road.rainSlow") + flood * cfg.get("road.floodSlow"),
                                cfg.get("road.maxFactor"));
            r.weatherClosed = rain >= cfg.get("road.closeRainMmH");
        }
        dirty_ = true;
    }

    Paths from(int src) {
        if (dirty_) rebuild();
        Paths p;
        p.dist.assign((size_t)std::max(nodeCount_, 1), INF_DIST);
        p.prev.assign((size_t)std::max(nodeCount_, 1), -1);
        if (nodeCount_ > 0) dijkstra(g_, src, p.dist.data(), p.prev.data());
        return p;
    }
    Route route(const Paths &p, int dst) {
        Route r;
        if (dst < 0 || dst >= nodeCount_) return r;
        std::vector<int> buf((size_t)nodeCount_);
        int len = graph_path(g_, p.prev.data(), p.dist.data(), dst, buf.data());
        if (len == 0) return r;
        r.nodes.assign(buf.begin(), buf.begin() + len);
        r.cost = p.dist[(size_t)dst];
        r.ok = true;
        return r;
    }
};
