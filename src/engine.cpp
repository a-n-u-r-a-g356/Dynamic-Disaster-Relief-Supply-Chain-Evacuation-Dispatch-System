#include "engine.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

void Engine::refreshRoads(Model &m) {
    size_t n = m.nodes.size();
    std::vector<double> rain(n, 0.0), flood(n, 0.0);
    for (int i : m.nodes.indicesOf<ZoneNode>()) {
        ZoneNode *z = m.nodes.as<ZoneNode>(i);
        rain[(size_t)i] = z->rainNow;
        flood[(size_t)i] = z->floodDepth;
    }
    m.roads.setNodeCount((int)n);
    m.roads.applyWeather(rain, flood, m.cfg);
}

void Engine::assess(Model &m) {
    const Config &c = m.cfg;
    // Independent evidence combines as: confidence = 1 - product(1 - c_i)
    std::unordered_map<std::string, double> miss;
    for (const auto &r : m.reports.accepted()) {
        auto it = miss.emplace(r->zoneId(), 1.0).first;
        it->second *= 1.0 - r->confidence(c);
    }
    for (int i : m.nodes.indicesOf<ZoneNode>()) {
        ZoneNode *z = m.nodes.as<ZoneNode>(i);
        double missProb = 1.0;
        auto it = miss.find(z->id());
        if (it != miss.end()) missProb = it->second;
        if (z->rain24h > 0 || z->rainNow > 0) {
            WeatherReport w(z->id(), z->rain24h);
            missProb *= 1.0 - w.confidence(c);
        }
        z->confidence = 1.0 - missProb;

        double pop = std::min(z->population / c.get("norm.population"), 1.0);
        double depth = std::min(z->floodDepth / c.get("norm.depth"), 1.0);
        double rain = std::min((z->rain24h + c.get("rain.nowFactor") * z->rainNow) / c.get("norm.rain"), 1.0);
        z->score = z->confidence * (c.get("weight.population") * pop + c.get("weight.depth") * depth +
                                    c.get("weight.rain") * rain) * 100.0;
        z->level = z->score >= c.get("level.critical") ? "CRITICAL"
                 : z->score >= c.get("level.high")     ? "HIGH"
                 : z->score >= c.get("level.moderate") ? "MODERATE" : "LOW";
    }
}

std::vector<int> Engine::priorityOrder(Model &m) {
    assess(m);
    PriorityManager pm((int)m.nodes.size());
    for (int i : m.nodes.indicesOf<ZoneNode>()) pm.update(i, m.nodes.as<ZoneNode>(i)->score);
    std::vector<int> order;
    int id;
    double s;
    while (pm.pop(id, s)) order.push_back(id);
    return order;
}

std::vector<SupplyAssignment> Engine::dispatchSupplies(Model &m) {
    std::vector<int> order = priorityOrder(m);
    refreshRoads(m);

    std::unordered_map<std::string, double> stockLeft;
    for (int i : m.nodes.indicesOf<DepotNode>()) stockLeft[m.nodes.at(i).id()] = m.nodes.as<DepotNode>(i)->stock;
    std::unordered_map<std::string, bool> used;
    std::map<int, Paths> cache;      // one Dijkstra per distinct vehicle base

    std::vector<SupplyAssignment> out;
    for (int zi : order) {
        ZoneNode *z = m.nodes.as<ZoneNode>(zi);
        SupplyAssignment a;
        a.zone = zi;
        a.demand = std::ceil(z->affectedPeople(m.cfg.get("affect.fullDepth")) *
                             m.cfg.get("supply.perPersonPerDay") * m.cfg.get("supply.days"));
        if (z->score <= 0) { a.status = "NO EVIDENCE"; out.push_back(a); continue; }
        if (a.demand <= 0) { a.status = "NO DEMAND"; out.push_back(a); continue; }

        Vehicle *best = nullptr;
        double bestEta = 0, bestKm = 0;
        std::vector<int> bestPath;
        for (auto &vp : m.fleet) {
            Vehicle *v = vp.get();
            if (!v->available() || used[v->id()] || !v->canServe(*z, m.cfg)) continue;
            if (stockLeft[v->home()] <= 0) continue;
            int home = m.nodes.indexOf(v->home());
            if (home < 0) continue;
            double km;
            std::vector<int> path;
            if (v->usesRoads()) {
                if (!cache.count(home)) cache[home] = m.roads.from(home);
                Route r = m.roads.route(cache[home], zi);
                if (!r.ok) continue;
                km = r.cost;
                path = r.nodes;
            } else {
                km = util::haversineKm(m.nodes.at(home).lat(), m.nodes.at(home).lon(), z->lat(), z->lon());
                path = {home, zi};
            }
            double eta = v->etaMinutes(km);
            if (!best || eta < bestEta) { best = v; bestEta = eta; bestKm = km; bestPath = path; }
        }
        if (!best) { a.status = "NO VEHICLE"; out.push_back(a); continue; }
        used[best->id()] = true;
        a.status = "ASSIGNED";
        a.vehicleId = best->id();
        a.vehicleType = best->type();
        a.depotId = best->home();
        a.load = std::min({a.demand, best->capacity(), stockLeft[best->home()]});
        stockLeft[best->home()] -= a.load;
        a.km = bestKm;
        a.etaMin = bestEta;
        a.path = bestPath;
        out.push_back(a);
    }
    return out;
}

std::vector<EvacAssignment> Engine::evacuate(Model &m) {
    std::vector<int> order = priorityOrder(m);
    refreshRoads(m);

    std::unordered_map<int, double> shelterLeft;
    for (int i : m.nodes.indicesOf<ShelterNode>()) shelterLeft[i] = m.nodes.as<ShelterNode>(i)->capacity;

    std::vector<EvacAssignment> out;
    for (int zi : order) {
        ZoneNode *z = m.nodes.as<ZoneNode>(zi);
        if (z->score < m.cfg.get("evac.minScore")) continue;
        double left = std::ceil(z->affectedPeople(m.cfg.get("affect.fullDepth")));
        if (left <= 0) continue;

        Paths p = m.roads.from(zi);
        std::vector<std::pair<double, int>> cand;     // (distance, shelter index)
        for (auto &kv : shelterLeft)
            if (p.dist[(size_t)kv.first] < INF_DIST / 2) cand.push_back({p.dist[(size_t)kv.first], kv.first});
        std::sort(cand.begin(), cand.end());

        for (auto &c : cand) {
            if (left <= 0) break;
            double take = std::min(left, shelterLeft[c.second]);
            if (take <= 0) continue;
            EvacAssignment e;
            e.zone = zi; e.shelter = c.second; e.people = take; e.km = c.first;
            e.status = "EVACUATE";
            Route r = m.roads.route(p, c.second);
            e.path = r.nodes;
            out.push_back(e);
            shelterLeft[c.second] -= take;
            left -= take;
        }
        if (left > 0) {
            EvacAssignment e;
            e.zone = zi; e.people = left;
            e.status = cand.empty() ? "UNREACHABLE" : "NO SHELTER CAPACITY";
            out.push_back(e);
        }
    }
    return out;
}
