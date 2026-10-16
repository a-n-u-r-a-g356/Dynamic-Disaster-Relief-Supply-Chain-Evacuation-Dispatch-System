# Dynamic Disaster Relief Supply Chain & Evacuation Dispatch System

Explainable decision-support prototype: it verifies disaster reports, scores each affected zone, ranks the
zones, assigns relief vehicles and supplies, plans evacuation to shelters, and re-plans when roads or
weather change. **Data Structures in C** + **OOP in C++**, no external libraries.

## Build and run

Needs `gcc`, `g++` (C++17), `make`, and `python3` (only for live weather).

```
make            # builds ./relief
make test       # unit tests for the C data structures
./relief                                   # interactive shell, loads ./data
./relief --data my_scenario                # use another folder (starts empty if it does not exist)
./relief --script tests/session.txt        # run a command file (works offline)
```

Inside the shell type `help`. A typical flow:

```
weather fetch          # live rainfall from the internet (or: weather set Z1 6 80)
assess                 # confidence, impact score, level, ranking
dispatch               # which vehicle carries how many supplies to which zone, and the route
evacuate               # how many people go to which shelter, and the route
block D1 Z1            # a road is lost -> run dispatch again, routes change
report citizen Z5 4    # new report enters the intake queue
watch 15               # refetch weather every 15 minutes and re-dispatch (Ctrl-C to stop)
save                   # write everything back to the CSV files
```

## Project layout

```
include/ + src/c/    C data-structure library
  hashmap   string -> int hash map, chaining, auto-resize   (node id -> index)
  heap      indexed max/min binary heap, O(log n) key update (zone priority, Dijkstra)
  graph     adjacency list of linked edges + Dijkstra       (road network)
  queue     linked FIFO queue                                (report intake)
src/                 C++ object-oriented engine
  node.h        Node (abstract) -> ZoneNode, DepotNode, ShelterNode;  NodeRegistry
  report.h      Report (abstract) -> SensorReport, CitizenReport, WeatherReport;  ReportManager
  vehicle.h     Vehicle (abstract) -> Truck, Boat, Helicopter
  road_network.h  RoadNetwork wraps the C graph (RAII, rebuilt when roads/weather change)
  config.h      reads config.ini
  weather.h     loads/saves weather.csv, runs the fetcher
  engine.*      verification, impact scoring, PriorityManager, supply dispatch, evacuation
  storage.*     CSV load/save
  cli.*, main.cpp  interactive shell
tools/fetch_weather.py   live rainfall from Open-Meteo (free, no API key)
data/                    scenario files you edit
tests/                   C unit tests + scripted end-to-end session
```

## Where the course concepts are used

| Concept | Where |
|---|---|
| Hash map | `NodeRegistry` maps ids to indices |
| Max-heap / priority queue | `PriorityManager` ranks zones by score |
| Graph + Dijkstra (min-heap, decrease-key) | `RoadNetwork`: vehicle routes, evacuation routes |
| Linked list / FIFO queue | graph edges; report intake queue |
| Abstraction, inheritance, polymorphism | `Node`, `Report`, `Vehicle` hierarchies (virtual `confidence()`, `canServe()`, `usesRoads()`) |
| Encapsulation, RAII | C structures are owned by C++ classes and freed in destructors; copying is disabled |
| STL | vectors, maps, `unique_ptr` |

## How decisions are made

1. **Verification:** each report has a trust value by source type. Independent evidence combines as
   `confidence = 1 - product(1 - trust_i)`. Live rain also counts as weak evidence. A zone with no
   evidence at all gets score 0 and is not dispatched ("NO EVIDENCE").
2. **Impact score** (0-100) = `confidence x (w_pop x population + w_depth x flood depth + w_rain x rainfall) x 100`,
   each factor normalised to 0-1. Levels: LOW / MODERATE / HIGH / CRITICAL.
3. **Supply dispatch:** zones are served highest score first. Demand = affected people x units per person x days.
   Each zone gets the available, capable, reachable vehicle with the shortest ETA whose home depot still has stock.
   Trucks need shallow water, boats need deep water, helicopters ignore roads.
   One vehicle per zone per run, so the output shows what percentage of demand a trip covers.
4. **Evacuation:** zones at or above `evac.minScore` are sent to the nearest shelters with capacity, splitting across
   shelters when needed. Zones with nowhere to go are reported as `NO SHELTER CAPACITY` or `UNREACHABLE`.
5. **Weather effects on roads:** rain and flood depth multiply a road's cost; rain at or above `road.closeRainMmH` closes it.

## Weather: how live data gets in

`weather fetch` runs the command in `weather.command` (default `python3 tools/fetch_weather.py`). The script reads
the zone coordinates from `nodes.csv`, asks Open-Meteo for current and last-24-hour precipitation (one request for all
zones) and writes `weather.csv`. The C++ side loads that file. To use another provider (IMD, a sensor gateway, a
database export), point `weather.command` at any program that writes the same three columns:
`id,rain_now_mm,rain_24h_mm`. If the fetch fails, the last known values are kept and `weather set` lets you type them in.

## Data files (all in the data folder, all editable)

| File | Columns |
|---|---|
| `config.ini` | every weight, threshold, trust value and limit (see comments inside) |
| `nodes.csv` | `id,type,name,lat,lon,population,floodDepthM,capacity,stock` - type is ZONE, DEPOT or SHELTER |
| `roads.csv` | `from,to,km,blocked` |
| `vehicles.csv` | `id,type,home,capacity,speedKmh,available` - type is TRUCK, BOAT or HELI; home is a depot id |
| `reports.csv` | `zone,source,supporters` - source is SENSOR or CITIZEN |
| `weather.csv` | `id,rain_now_mm,rain_24h_mm` (written by the fetcher) |

The files shipped in `data/` are a small sample dataset so the program runs out of the box. Replace them with
your own area, or start empty with `./relief --data new_folder` and use the `add ...` commands, then `save`.

## Known limits

- Roads are undirected; vehicles do one trip per zone per run.
- Weights, thresholds and trust values are tunable assumptions, not calibrated against real flood data.
- Nodes cannot be deleted from the shell (edit the CSV instead).
