#pragma once
#include "engine.h"
#include <ostream>
#include <string>

// All data lives in plain CSV files inside one folder:
//   config.ini   tunable numbers (optional)
//   nodes.csv    id,type,name,lat,lon,population,floodDepthM,capacity,stock
//   roads.csv    from,to,km,blocked
//   vehicles.csv id,type,home,capacity,speedKmh,available
//   reports.csv  zone,source,supporters
//   weather.csv  id,rain_now_mm,rain_24h_mm (written by the weather fetcher)
namespace storage {
bool load(Model &m, const std::string &dir, std::ostream &log);
bool save(Model &m, const std::string &dir, std::ostream &log);
}
