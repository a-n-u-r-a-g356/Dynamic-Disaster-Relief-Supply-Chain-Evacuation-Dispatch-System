// Dynamic Disaster Relief Supply Chain & Evacuation Dispatch System
// C data structures (src/c) + C++ object-oriented decision engine (src).
#include "cli.h"
#include "storage.h"
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

int main(int argc, char **argv) {
    std::string dir = "data", script;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dir = argv[++i];
        else if (a == "--script" && i + 1 < argc) script = argv[++i];
        else if (a == "--help" || a == "-h") {
            std::cout << "usage: relief [--data <folder>] [--script <commandfile>]\n"
                         "  --data    folder with nodes.csv, roads.csv, ... (default: data)\n"
                         "  --script  run commands from a file instead of the keyboard\n";
            return 0;
        } else { std::cerr << "unknown option " << a << "\n"; return 1; }
    }

    Model m;
    m.dataDir = dir;
    if (!storage::load(m, dir, std::cout))
        std::cout << "Starting empty. Add data with 'add zone/depot/shelter/road/vehicle', then 'save'.\n";

    if (!script.empty()) {
        std::ifstream f(script);
        if (!f) { std::cerr << "cannot open script " << script << "\n"; return 1; }
        return runShell(m, f, false);
    }return runShell(m, std::cin, true);
}
