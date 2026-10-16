#include "storage.h"
#include "util.h"
#include "weather.h"
#include <filesystem>
#include <fstream>

namespace {

using Rows = std::vector<std::vector<std::string>>;

// Reads a CSV, skipping the header, blank lines and '#' comment lines.
bool readCsv(const std::string &path, Rows &rows) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    bool header = true;
    while (std::getline(f, line)) {
        std::string t = util::trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (header) { header = false; continue; }
        rows.push_back(util::split(t));
    }
    return true;
}

double col(const std::vector<std::string> &r, size_t i, double fallback = 0) {
    double d;
    return (i < r.size() && util::toDouble(r[i], d)) ? d : fallback;
}

} // namespace

bool storage::load(Model &m, const std::string &dir, std::ostream &log) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(dir)) { log << "folder not found: " << dir << "\n"; return false; }
    m.dataDir = dir;
    m.nodes.clear();
    m.roads.clear();
    m.fleet.clear();
    m.reports.clearAll();

    if (m.cfg.load(dir + "/config.ini")) log << "loaded config.ini\n";
    else log << "no config.ini - using built-in fallback values\n";

    Rows rows;
    if (readCsv(dir + "/nodes.csv", rows)) {
        for (auto &r : rows) {
            if (r.size() < 5) { log << "skipped bad node row\n"; continue; }
            std::string type = util::upper(r[1]);
            std::unique_ptr<Node> n;
            if (type == "ZONE")         n = std::make_unique<ZoneNode>(r[0], r[2], col(r, 3), col(r, 4), (int)col(r, 5), col(r, 6));
            else if (type == "DEPOT")   n = std::make_unique<DepotNode>(r[0], r[2], col(r, 3), col(r, 4), col(r, 8));
            else if (type == "SHELTER") n = std::make_unique<ShelterNode>(r[0], r[2], col(r, 3), col(r, 4), col(r, 7));
            else { log << "skipped node " << r[0] << ": unknown type " << r[1] << "\n"; continue; }
            if (!m.nodes.add(std::move(n))) log << "skipped duplicate node id " << r[0] << "\n";
        }
    }
    m.roads.setNodeCount((int)m.nodes.size());

    rows.clear();
    if (readCsv(dir + "/roads.csv", rows)) {
        for (auto &r : rows) {
            int u = r.size() > 1 ? m.nodes.indexOf(r[0]) : -1, v = r.size() > 1 ? m.nodes.indexOf(r[1]) : -1;
            if (u < 0 || v < 0 || !m.roads.addRoad(u, v, col(r, 2), r.size() > 3 && r[3] == "1"))
                log << "skipped bad road row\n";
        }
    }

    rows.clear();
    if (readCsv(dir + "/vehicles.csv", rows)) {
        for (auto &r : rows) {
            if (r.size() < 5 || !m.nodes.as<DepotNode>(m.nodes.indexOf(r[2]))) {
                log << "skipped vehicle row (home must be an existing DEPOT)\n";
                continue;
            }
            std::string type = util::upper(r[1]);
            std::unique_ptr<Vehicle> v;
            if (type == "TRUCK")     v = std::make_unique<Truck>(r[0], r[2], col(r, 3), col(r, 4));
            else if (type == "BOAT") v = std::make_unique<Boat>(r[0], r[2], col(r, 3), col(r, 4));
            else if (type == "HELI") v = std::make_unique<Helicopter>(r[0], r[2], col(r, 3), col(r, 4));
            else { log << "skipped vehicle " << r[0] << ": unknown type " << r[1] << "\n"; continue; }
            v->setAvailable(col(r, 5, 1) != 0);
            m.fleet.push_back(std::move(v));
        }
    }

    rows.clear();
    if (readCsv(dir + "/reports.csv", rows)) {
        for (auto &r : rows) {
            if (r.size() < 2 || !m.nodes.as<ZoneNode>(m.nodes.indexOf(r[0]))) { log << "skipped bad report row\n"; continue; }
            std::string src = util::upper(r[1]);
            if (src == "SENSOR") m.reports.submit(std::make_unique<SensorReport>(r[0]));
            else if (src == "CITIZEN") m.reports.submit(std::make_unique<CitizenReport>(r[0], (int)col(r, 2, 1)));
            else log << "skipped report: unknown source " << r[1] << "\n";
        }
    }

    int w = WeatherService::load(dir + "/weather.csv", m.nodes);
    if (w >= 0) log << "loaded weather for " << w << " zone(s)\n";
    log << "loaded " << m.nodes.size() << " nodes, " << m.roads.all().size() << " roads, "
        << m.fleet.size() << " vehicles, " << m.reports.accepted().size() << " reports from " << dir << "\n";
    return true;
}

bool storage::save(Model &m, const std::string &dir, std::ostream &log) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { log << "cannot create folder " << dir << "\n"; return false; }

    {
        std::ofstream f(dir + "/nodes.csv");
        f << "id,type,name,lat,lon,population,floodDepthM,capacity,stock\n";
        for (size_t i = 0; i < m.nodes.size(); i++) {
            Node &n = m.nodes.at((int)i);
            f << n.id() << "," << n.typeName() << "," << n.name() << "," << n.lat() << "," << n.lon() << ",";
            if (auto *z = dynamic_cast<ZoneNode *>(&n))        f << z->population << "," << z->floodDepth << ",,\n";
            else if (auto *s = dynamic_cast<ShelterNode *>(&n)) f << ",," << s->capacity << ",\n";
            else if (auto *d = dynamic_cast<DepotNode *>(&n))   f << ",,," << d->stock << "\n";
        }
    }
    {
        std::ofstream f(dir + "/roads.csv");
        f << "from,to,km,blocked\n";
        for (const Road &r : m.roads.all())
            f << m.nodes.at(r.u).id() << "," << m.nodes.at(r.v).id() << "," << r.km << "," << (r.blocked ? 1 : 0) << "\n";
    }
    {
        std::ofstream f(dir + "/vehicles.csv");
        f << "id,type,home,capacity,speedKmh,available\n";
        for (auto &v : m.fleet)
            f << v->id() << "," << v->type() << "," << v->home() << "," << v->capacity() << "," << v->speedKmh() << ","
              << (v->available() ? 1 : 0) << "\n";
    }
    {
        std::ofstream f(dir + "/reports.csv");
        f << "zone,source,supporters\n";
        for (const auto &r : m.reports.accepted()) f << r->zoneId() << "," << r->source() << "," << r->supporters() << "\n";
    }
    if (!fs::exists(dir + "/config.ini")) {          // never overwrite a hand-edited (commented) config
        std::ofstream f(dir + "/config.ini");
        f << "# Tunable numbers (written from the values in use; edit freely)\n";
        for (auto &kv : m.cfg.numbers()) f << kv.first << " = " << kv.second << "\n";
        for (auto &kv : m.cfg.strings()) f << kv.first << " = " << kv.second << "\n";
    }
    WeatherService::save(dir + "/weather.csv", m.nodes);
    m.dataDir = dir;
    log << "saved to " << dir << "\n";
    return true;
}
