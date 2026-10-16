#include "cli.h"
#include "storage.h"
#include "util.h"
#include "weather.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

using std::cout;
using std::string;
using Args = std::vector<string>;

namespace {

const char *HELP = R"(Commands (ids have no spaces; use _ inside names):
  load [dir] | save [dir]                      read / write all CSV files
  list nodes|roads|vehicles|reports|config
  add zone <id> <name> <lat> <lon> <population> <floodDepthM>
  add depot <id> <name> <lat> <lon> <stock>
  add shelter <id> <name> <lat> <lon> <capacity>
  add road <idA> <idB> <km>        remove road <idA> <idB>
  add vehicle <id> <truck|boat|heli> <depotId> <capacity> <speedKmh>
  block <idA> <idB> | unblock <idA> <idB>      close / reopen a road
  depth <zoneId> <metres>                       update flood depth
  population <zoneId> <people>
  stock <depotId> <units>
  vehicle <id> free|busy                        change availability
  report sensor <zoneId> | report citizen <zoneId> <supporters> | report clear <zoneId>
  weather fetch                                 pull live rainfall from the internet
  weather show | weather set <zoneId> <rainNow_mm_h> <rain24h_mm> | weather clear
  config set <key> <number>
  assess                                        confidence, impact score, level per zone
  dispatch                                      supply + vehicle plan
  evacuate                                      evacuation plan to shelters
  route <idA> <idB>                             shortest open road route
  watch <minutes> [cycles]                      refetch weather and re-dispatch repeatedly
  help | quit)";

bool num(const string &s, double &d) { return util::toDouble(s, d); }

string pathStr(Model &m, const std::vector<int> &p) {
    string s;
    for (size_t i = 0; i < p.size(); i++) s += (i ? " -> " : "") + m.nodes.at(p[i]).name();
    return s;
}

void printAssess(Model &m) {
    std::vector<int> order = Engine::priorityOrder(m);
    cout << std::left << std::setw(5) << "Rank" << std::setw(8) << "ID" << std::setw(20) << "Zone" << std::setw(10)
         << "Level" << std::right << std::setw(7) << "Score" << std::setw(7) << "Conf" << std::setw(9) << "People"
         << std::setw(8) << "Depth" << std::setw(8) << "Rain/h" << std::setw(8) << "Rain24" << "\n";
    int rank = 1;
    for (int i : order) {
        ZoneNode *z = m.nodes.as<ZoneNode>(i);
        cout << std::left << std::setw(5) << rank++ << std::setw(8) << z->id() << std::setw(20) << z->name()
             << std::setw(10) << z->level << std::right << std::fixed << std::setprecision(1) << std::setw(7) << z->score
             << std::setprecision(2) << std::setw(7) << z->confidence << std::setw(9) << z->population
             << std::setprecision(1) << std::setw(8) << z->floodDepth << std::setw(8) << z->rainNow << std::setw(8)
             << z->rain24h << "\n";
    }
    if (order.empty()) cout << "(no zones)\n";
}

void printDispatch(Model &m) {
    auto plan = Engine::dispatchSupplies(m);
    cout << std::fixed << std::setprecision(1);
    for (auto &a : plan) {
        ZoneNode *z = m.nodes.as<ZoneNode>(a.zone);
        cout << z->name() << " [" << z->level << " " << z->score << ", conf " << std::setprecision(2) << z->confidence
             << std::setprecision(1) << "] demand " << a.demand << " -> " << a.status;
        if (a.status == "ASSIGNED")
            cout << ": " << a.vehicleType << " " << a.vehicleId << " from " << a.depotId << ", load " << a.load << " ("
                 << (a.demand > 0 ? 100.0 * a.load / a.demand : 0.0) << "% of demand), " << a.km << " km-eq, ETA "
                 << a.etaMin << " min\n    route: " << pathStr(m, a.path);
        cout << "\n";
    }
    if (plan.empty()) cout << "(no zones)\n";
}

void printEvac(Model &m) {
    auto plan = Engine::evacuate(m);
    cout << std::fixed << std::setprecision(1);
    for (auto &e : plan) {
        ZoneNode *z = m.nodes.as<ZoneNode>(e.zone);
        cout << z->name() << " [" << z->level << " " << z->score << "] " << std::setprecision(0) << e.people
             << std::setprecision(1) << " people -> ";
        if (e.shelter >= 0)
            cout << m.nodes.at(e.shelter).name() << " (" << e.km << " km-eq)\n    route: " << pathStr(m, e.path) << "\n";
        else
            cout << e.status << (e.status == "UNREACHABLE" ? " by road (airlift or open a road)" : "") << "\n";
    }
    if (plan.empty()) cout << "No zone reaches the evacuation threshold (evac.minScore).\n";
}

bool refreshWeather(Model &m) {
    cout << "Fetching live weather...\n";
    if (!WeatherService::fetch(m.cfg, m.dataDir)) {
        cout << "Weather fetch failed (no internet or fetcher error). Keeping the last known values; "
                "use 'weather set' to enter them by hand.\n";
        return false;
    }
    int n = WeatherService::load(m.dataDir + "/weather.csv", m.nodes);
    cout << "Weather updated for " << n << " zone(s).\n";
    return n >= 0;
}

Vehicle *findVehicle(Model &m, const string &id) {
    for (auto &v : m.fleet) if (v->id() == id) return v.get();
    return nullptr;
}

// Returns false when the shell should exit.
bool exec(Model &m, const Args &a) {
    if (a.empty()) return true;
    const string &c = a[0];

    if (c == "quit" || c == "exit") return false;
    if (c == "help") { cout << HELP << "\n"; return true; }

    if (c == "load") { storage::load(m, a.size() > 1 ? a[1] : m.dataDir, cout); return true; }
    if (c == "save") { storage::save(m, a.size() > 1 ? a[1] : m.dataDir, cout); return true; }

    if (c == "list" && a.size() > 1) {
        const string &w = a[1];
        if (w == "nodes") {
            for (size_t i = 0; i < m.nodes.size(); i++) {
                Node &n = m.nodes.at((int)i);
                cout << std::left << std::setw(8) << n.id() << std::setw(9) << n.typeName() << std::setw(22) << n.name();
                if (auto *z = dynamic_cast<ZoneNode *>(&n)) cout << "people " << z->population << ", depth " << z->floodDepth << " m, rain " << z->rainNow << " mm/h, 24h " << z->rain24h << " mm";
                else if (auto *d = dynamic_cast<DepotNode *>(&n)) cout << "stock " << d->stock;
                else if (auto *s = dynamic_cast<ShelterNode *>(&n)) cout << "capacity " << s->capacity;
                cout << "\n";
            }
        } else if (w == "roads") {
            Engine::refreshRoads(m);        // show current weather effects
            for (const Road &r : m.roads.all())
                cout << m.nodes.at(r.u).id() << " - " << m.nodes.at(r.v).id() << "  " << r.km << " km  x" << r.factor
                     << (r.blocked ? "  BLOCKED" : "") << (r.weatherClosed ? "  CLOSED BY RAIN" : "") << "\n";
        } else if (w == "vehicles") {
            for (auto &v : m.fleet)
                cout << std::left << std::setw(8) << v->id() << std::setw(7) << v->type() << "base " << std::setw(6) << v->home()
                     << "cap " << std::setw(6) << v->capacity() << "speed " << std::setw(6) << v->speedKmh()
                     << (v->available() ? "free" : "BUSY") << "\n";
        } else if (w == "reports") {
            m.reports.drain();
            for (auto &r : m.reports.accepted())
                cout << r->zoneId() << "  " << r->source() << "  supporters " << r->supporters() << "  trust " << r->confidence(m.cfg) << "\n";
        } else if (w == "config") {
            for (auto &kv : m.cfg.numbers()) cout << kv.first << " = " << kv.second << "\n";
            for (auto &kv : m.cfg.strings()) cout << kv.first << " = " << kv.second << "\n";
        } else cout << "list what? nodes|roads|vehicles|reports|config\n";
        return true;
    }

    if (c == "add" && a.size() > 1) {
        const string &w = a[1];
        double lat, lon, x, y;
        if (w == "zone" && a.size() == 8 && num(a[4], lat) && num(a[5], lon) && num(a[6], x) && num(a[7], y)) {
            cout << (m.nodes.add(std::make_unique<ZoneNode>(a[2], a[3], lat, lon, (int)x, y)) ? "zone added\n" : "id already exists\n");
        } else if (w == "depot" && a.size() == 7 && num(a[4], lat) && num(a[5], lon) && num(a[6], x)) {
            cout << (m.nodes.add(std::make_unique<DepotNode>(a[2], a[3], lat, lon, x)) ? "depot added\n" : "id already exists\n");
        } else if (w == "shelter" && a.size() == 7 && num(a[4], lat) && num(a[5], lon) && num(a[6], x)) {
            cout << (m.nodes.add(std::make_unique<ShelterNode>(a[2], a[3], lat, lon, x)) ? "shelter added\n" : "id already exists\n");
        } else if (w == "road" && a.size() == 5 && num(a[4], x)) {
            int u = m.nodes.indexOf(a[2]), v = m.nodes.indexOf(a[3]);
            m.roads.setNodeCount((int)m.nodes.size());
            cout << ((u >= 0 && v >= 0 && m.roads.addRoad(u, v, x)) ? "road added\n" : "unknown node id or bad length\n");
        } else if (w == "vehicle" && a.size() == 7 && num(a[5], x) && num(a[6], y)) {
            string t = util::upper(a[3]);
            if (!m.nodes.as<DepotNode>(m.nodes.indexOf(a[4]))) { cout << "home must be an existing depot id\n"; return true; }
            if (findVehicle(m, a[2])) { cout << "vehicle id already exists\n"; return true; }
            if (t == "TRUCK") m.fleet.push_back(std::make_unique<Truck>(a[2], a[4], x, y));
            else if (t == "BOAT") m.fleet.push_back(std::make_unique<Boat>(a[2], a[4], x, y));
            else if (t == "HELI") m.fleet.push_back(std::make_unique<Helicopter>(a[2], a[4], x, y));
            else { cout << "type must be truck, boat or heli\n"; return true; }
            cout << "vehicle added\n";
        } else cout << "bad 'add' command - see help\n";
        return true;
    }

    if (c == "remove" && a.size() == 4 && a[1] == "road") {
        cout << (m.roads.removeRoad(m.nodes.indexOf(a[2]), m.nodes.indexOf(a[3])) ? "road removed\n" : "no such road\n");
        return true;
    }
    if ((c == "block" || c == "unblock") && a.size() == 3) {
        cout << (m.roads.setBlocked(m.nodes.indexOf(a[1]), m.nodes.indexOf(a[2]), c == "block") ? "ok\n" : "no such road\n");
        return true;
    }
    if (c == "depth" && a.size() == 3) {
        double d;
        ZoneNode *z = m.nodes.as<ZoneNode>(m.nodes.indexOf(a[1]));
        if (z && num(a[2], d) && d >= 0) { z->floodDepth = d; cout << "ok\n"; } else cout << "usage: depth <zoneId> <metres>\n";
        return true;
    }
    if (c == "population" && a.size() == 3) {
        double d;
        ZoneNode *z = m.nodes.as<ZoneNode>(m.nodes.indexOf(a[1]));
        if (z && num(a[2], d) && d >= 0) { z->population = (int)d; cout << "ok\n"; } else cout << "usage: population <zoneId> <people>\n";
        return true;
    }
    if (c == "stock" && a.size() == 3) {
        double d;
        DepotNode *p = m.nodes.as<DepotNode>(m.nodes.indexOf(a[1]));
        if (p && num(a[2], d) && d >= 0) { p->stock = d; cout << "ok\n"; } else cout << "usage: stock <depotId> <units>\n";
        return true;
    }
    if (c == "vehicle" && a.size() == 3) {
        Vehicle *v = findVehicle(m, a[1]);
        if (v && (a[2] == "free" || a[2] == "busy")) { v->setAvailable(a[2] == "free"); cout << "ok\n"; } else cout << "usage: vehicle <id> free|busy\n";
        return true;
    }

    if (c == "report" && a.size() >= 3) {
        if (a[1] == "clear") { cout << m.reports.clearZone(a[2]) << " report(s) removed\n"; return true; }
        if (!m.nodes.as<ZoneNode>(m.nodes.indexOf(a[2]))) { cout << "unknown zone id\n"; return true; }
        if (a[1] == "sensor") m.reports.submit(std::make_unique<SensorReport>(a[2]));
        else if (a[1] == "citizen") {
            double s = 1;
            if (a.size() > 3 && !num(a[3], s)) { cout << "supporters must be a number\n"; return true; }
            m.reports.submit(std::make_unique<CitizenReport>(a[2], (int)s));
        } else { cout << "usage: report sensor|citizen|clear ...\n"; return true; }
        cout << "report queued (" << m.reports.pending() << " waiting in intake queue)\n";
        return true;
    }

    if (c == "weather" && a.size() > 1) {
        if (a[1] == "fetch") refreshWeather(m);
        else if (a[1] == "show") {
            for (int i : m.nodes.indicesOf<ZoneNode>()) {
                ZoneNode *z = m.nodes.as<ZoneNode>(i);
                cout << std::left << std::setw(8) << z->id() << std::setw(22) << z->name() << "now " << z->rainNow << " mm/h, 24h " << z->rain24h << " mm\n";
            }
        } else if (a[1] == "set" && a.size() == 5) {
            double n, h;
            ZoneNode *z = m.nodes.as<ZoneNode>(m.nodes.indexOf(a[2]));
            if (z && num(a[3], n) && num(a[4], h) && n >= 0 && h >= 0) { z->rainNow = n; z->rain24h = h; cout << "ok\n"; }
            else cout << "usage: weather set <zoneId> <rainNow_mm_h> <rain24h_mm>\n";
        } else if (a[1] == "clear") {
            for (int i : m.nodes.indicesOf<ZoneNode>()) { m.nodes.as<ZoneNode>(i)->rainNow = 0; m.nodes.as<ZoneNode>(i)->rain24h = 0; }
            cout << "weather cleared\n";
        } else cout << "usage: weather fetch|show|set|clear\n";
        return true;
    }
    if (c == "config" && a.size() == 4 && a[1] == "set") {
        double d;
        if (m.cfg.has(a[2]) && num(a[3], d)) { m.cfg.set(a[2], d); cout << "ok (not written to config.ini)\n"; }
        else cout << "unknown key or bad number (see: list config)\n";
        return true;
    }

    if (c == "assess") { printAssess(m); return true; }
    if (c == "dispatch") { printDispatch(m); return true; }
    if (c == "evacuate") { printEvac(m); return true; }
    if (c == "route" && a.size() == 3) {
        int u = m.nodes.indexOf(a[1]), v = m.nodes.indexOf(a[2]);
        if (u < 0 || v < 0) { cout << "unknown node id\n"; return true; }
        Engine::assess(m);
        Engine::refreshRoads(m);
        Paths p = m.roads.from(u);
        Route r = m.roads.route(p, v);
        if (r.ok) cout << std::fixed << std::setprecision(1) << r.cost << " km-eq: " << pathStr(m, r.nodes) << "\n";
        else cout << "no open road route\n";
        return true;
    }
    if (c == "watch" && a.size() >= 2) {
        double minutes, cycles = 0;
        if (!num(a[1], minutes) || minutes <= 0 || (a.size() > 2 && !num(a[2], cycles))) { cout << "usage: watch <minutes> [cycles]\n"; return true; }
        for (int i = 1; cycles == 0 || i <= (int)cycles; i++) {
            cout << "\n----- watch cycle " << i << " -----\n";
            refreshWeather(m);
            printDispatch(m);
            if (cycles != 0 && i >= (int)cycles) break;
            std::this_thread::sleep_for(std::chrono::duration<double>(minutes * 60.0));
        }
        return true;
    }

    cout << "unknown command '" << c << "' (type help)\n";
    return true;
}

} // namespace

namespace {

bool executeLine(Model &m, const string &line) {
    string t = util::trim(line);
    if (t.empty() || t[0] == '#') return true;
    std::istringstream ss(t);
    Args a;
    string w;
    while (ss >> w) a.push_back(w);
    try {
        return exec(m, a);
    } catch (const std::exception &e) {
        cout << "error: " << e.what() << "\n";
        return true;
    }
}

void printBanner() {
    cout << "\n"
         << "============================================================\n"
         << "                     RESQNET CONSOLE                        \n"
         << "        Disaster Relief & Evacuation Dispatch System        \n"
         << "============================================================\n";
}

void printMainMenu() {
    cout << "\n---------------------- MAIN MENU ----------------------------\n"
         << "  1. Risk assessment dashboard\n"
         << "  2. Dispatch relief supplies\n"
         << "  3. Evacuation planning\n"
         << "  4. Weather and incident reports\n"
         << "  5. Manage zones, depots and shelters\n"
         << "  6. Manage vehicles and supplies\n"
         << "  7. Roads and route planning\n"
         << "  8. View system data\n"
         << "  9. Save / load scenario\n"
         << " 10. Automated monitoring (watch)\n"
         << " 11. Advanced command console\n"
         << "  0. Exit\n"
         << "-------------------------------------------------------------\n";
}

void commandPrompt(Model &m, std::istream &in, const string &label) {
    cout << "\n" << label << "\n"
         << "Enter the command shown (or type BACK to return).\n"
         << "Command: " << std::flush;
    string line;
    if (!std::getline(in, line)) return;
    if (util::upper(util::trim(line)) == "BACK") return;
    executeLine(m, line);
}

void submenu(Model &m, std::istream &in, const string &title, const string &commands) {
    while (in.good()) {
        cout << "\n---------------- " << title << " ----------------\n"
             << commands
             << "  0. Back to main menu\n"
             << "Select an option: " << std::flush;
        string choice;
        if (!std::getline(in, choice)) return;
        choice = util::trim(choice);
        if (choice == "0" || util::upper(choice) == "BACK") return;

        string cmd;
        if (title == "WEATHER & REPORTS") {
            if (choice == "1") cmd = "weather fetch";
            else if (choice == "2") cmd = "weather show";
            else if (choice == "3") commandPrompt(m, in, "Set rainfall for a zone: weather set <zoneId> <rainNow_mm_h> <rain24h_mm>");
            else if (choice == "4") commandPrompt(m, in, "Add a sensor report: report sensor <zoneId>");
            else if (choice == "5") commandPrompt(m, in, "Add a citizen report: report citizen <zoneId> <supporters>");
            else if (choice == "6") commandPrompt(m, in, "Clear reports for a zone: report clear <zoneId>");
            else if (choice == "7") cmd = "weather clear";
            else { cout << "Invalid option. Choose 0-7.\n"; continue; }
        } else if (title == "ZONES, DEPOTS & SHELTERS") {
            if (choice == "1") commandPrompt(m, in, "add zone <id> <name> <lat> <lon> <population> <floodDepthM>");
            else if (choice == "2") commandPrompt(m, in, "add depot <id> <name> <lat> <lon> <stock>");
            else if (choice == "3") commandPrompt(m, in, "add shelter <id> <name> <lat> <lon> <capacity>");
            else if (choice == "4") commandPrompt(m, in, "depth <zoneId> <metres>");
            else if (choice == "5") commandPrompt(m, in, "population <zoneId> <people>");
            else if (choice == "6") cmd = "list nodes";
            else { cout << "Invalid option. Choose 0-6.\n"; continue; }
        } else if (title == "VEHICLES & SUPPLIES") {
            if (choice == "1") commandPrompt(m, in, "add vehicle <id> <truck|boat|heli> <depotId> <capacity> <speedKmh>");
            else if (choice == "2") commandPrompt(m, in, "vehicle <id> free|busy");
            else if (choice == "3") commandPrompt(m, in, "stock <depotId> <units>");
            else if (choice == "4") cmd = "list vehicles";
            else { cout << "Invalid option. Choose 0-4.\n"; continue; }
        } else if (title == "ROADS & ROUTES") {
            if (choice == "1") commandPrompt(m, in, "add road <idA> <idB> <km>");
            else if (choice == "2") commandPrompt(m, in, "remove road <idA> <idB>");
            else if (choice == "3") commandPrompt(m, in, "block <idA> <idB>");
            else if (choice == "4") commandPrompt(m, in, "unblock <idA> <idB>");
            else if (choice == "5") commandPrompt(m, in, "route <idA> <idB>");
            else if (choice == "6") cmd = "list roads";
            else { cout << "Invalid option. Choose 0-6.\n"; continue; }
        } else if (title == "VIEW SYSTEM DATA") {
            if (choice == "1") cmd = "list nodes";
            else if (choice == "2") cmd = "list roads";
            else if (choice == "3") cmd = "list vehicles";
            else if (choice == "4") cmd = "list reports";
            else if (choice == "5") cmd = "list config";
            else { cout << "Invalid option. Choose 0-5.\n"; continue; }
        } else if (title == "SAVE / LOAD SCENARIO") {
            if (choice == "1") cmd = "save";
            else if (choice == "2") cmd = "load";
            else if (choice == "3") commandPrompt(m, in, "Save to another folder: save <folder>");
            else if (choice == "4") commandPrompt(m, in, "Load from another folder: load <folder>");
            else { cout << "Invalid option. Choose 0-4.\n"; continue; }
        }
        if (!cmd.empty()) executeLine(m, cmd);
    }
}

int runMenu(Model &m, std::istream &in) {
    printBanner();
    cout << "Loaded scenario folder: " << m.dataDir << "\n"
         << "Tip: IDs and names must not contain spaces; use underscores.\n";
    while (in.good()) {
        printMainMenu();
        cout << "Select an option: " << std::flush;
        string choice;
        if (!std::getline(in, choice)) break;
        choice = util::trim(choice);
        if (choice == "0") {
            cout << "Exiting ResQNet. Remember to save changes before exit.\n";
            break;
        }
        if (choice == "1") executeLine(m, "assess");
        else if (choice == "2") executeLine(m, "dispatch");
        else if (choice == "3") executeLine(m, "evacuate");
        else if (choice == "4") submenu(m, in, "WEATHER & REPORTS",
            "  1. Fetch live weather\n  2. Show current rainfall\n  3. Set rainfall manually\n"
            "  4. Submit sensor report\n  5. Submit citizen report\n  6. Clear zone reports\n  7. Clear weather values\n");
        else if (choice == "5") submenu(m, in, "ZONES, DEPOTS & SHELTERS",
            "  1. Add zone\n  2. Add depot\n  3. Add shelter\n  4. Update flood depth\n  5. Update population\n  6. List nodes\n");
        else if (choice == "6") submenu(m, in, "VEHICLES & SUPPLIES",
            "  1. Add vehicle\n  2. Change vehicle availability\n  3. Update depot stock\n  4. List vehicles\n");
        else if (choice == "7") submenu(m, in, "ROADS & ROUTES",
            "  1. Add road\n  2. Remove road\n  3. Block road\n  4. Reopen road\n  5. Find shortest route\n  6. List roads\n");
        else if (choice == "8") submenu(m, in, "VIEW SYSTEM DATA",
            "  1. Nodes and zones\n  2. Roads\n  3. Vehicles\n  4. Reports\n  5. Configuration\n");
        else if (choice == "9") submenu(m, in, "SAVE / LOAD SCENARIO",
            "  1. Save current scenario\n  2. Reload current scenario\n  3. Save to another folder\n  4. Load from another folder\n");
        else if (choice == "10") commandPrompt(m, in, "Run monitoring: watch <interval_minutes> [number_of_cycles]");
        else if (choice == "11") {
            cout << "\nAdvanced command console. Type HELP for commands, BACK to return.\n";
            while (in.good()) {
                cout << "resqnet> " << std::flush;
                string line;
                if (!std::getline(in, line)) return 0;
                if (util::upper(util::trim(line)) == "BACK") break;
                if (!executeLine(m, line)) return 0;
            }
        } else {
            cout << "Invalid option. Please select a number from 0 to 11.\n";
        }
    }
    return 0;
}

} // namespace

int runShell(Model &m, std::istream &in, bool showPrompt) {
    // Interactive keyboard sessions use the menu; --script retains the
    // original line-oriented command interface for reproducible tests.
    if (showPrompt) return runMenu(m, in);

    string line;
    while (std::getline(in, line)) {
        string t = util::trim(line);
        if (t.empty() || t[0] == '#') continue;
        cout << "> " << t << "\n";
        if (!executeLine(m, t)) break;
    }
    return 0;
}
