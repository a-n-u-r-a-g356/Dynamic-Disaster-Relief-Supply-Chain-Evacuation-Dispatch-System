#!/usr/bin/env python3
"""Location-Based Nearby Weather Monitoring Server for ResQNet.

Serves a web dashboard that:
  1. Detects the user's location via browser geolocation.
  2. Discovers 5-6 nearby populated places using OpenStreetMap Overpass API.
  3. Fetches live weather from Open-Meteo (free, no API key).
  4. Computes Haversine distances and weather-based risk indicators.
  5. Displays everything on a responsive disaster-management dashboard.

Usage:
    python tools/nearby_weather.py [--port PORT] [--radius RADIUS_KM]

All APIs used are free and do not require API keys.
Third-party API usage policies and rate limits are respected.
"""

import argparse
import html
import http.server
import io
import json
import math
import os
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from functools import lru_cache

# Fix Windows console encoding for emoji/Unicode output
if sys.platform == "win32":
    try:
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
        sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace")
    except Exception:
        pass

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
DEFAULT_PORT = 8765
DEFAULT_RADIUS_KM = 50
MAX_NEARBY_PLACES = 6
AUTO_REFRESH_MINUTES = 15
CACHE_TTL_SECONDS = 300  # 5 minutes

# API endpoints (free, no API key required)
OPEN_METEO_API = "https://api.open-meteo.com/v1/forecast"
NOMINATIM_API = "https://nominatim.openstreetmap.org/reverse"
NOMINATIM_SEARCH_API = "https://nominatim.openstreetmap.org/search"
OVERPASS_MIRRORS = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://lz4.overpass-api.de/api/interpreter",
    "https://overpass.private.coffee/api/interpreter",
]
OVERPASS_API = OVERPASS_MIRRORS[0]

# User-Agent for OSM Nominatim usage policy compliance
USER_AGENT = "ResQNet-NearbyWeather/1.0 (disaster-management-tool)"

# WMO Weather Interpretation Codes
# https://open-meteo.com/en/docs
WMO_CODES = {
    0: ("Clear sky", "☀️", "clear"),
    1: ("Mainly clear", "🌤️", "clear"),
    2: ("Partly cloudy", "⛅", "cloudy"),
    3: ("Overcast", "☁️", "cloudy"),
    45: ("Fog", "🌫️", "fog"),
    48: ("Depositing rime fog", "🌫️", "fog"),
    51: ("Light drizzle", "🌦️", "drizzle"),
    53: ("Moderate drizzle", "🌦️", "drizzle"),
    55: ("Dense drizzle", "🌧️", "drizzle"),
    56: ("Light freezing drizzle", "🌧️", "drizzle"),
    57: ("Dense freezing drizzle", "🌧️", "drizzle"),
    61: ("Slight rain", "🌧️", "rain"),
    63: ("Moderate rain", "🌧️", "rain"),
    65: ("Heavy rain", "🌧️", "rain"),
    66: ("Light freezing rain", "🌧️", "rain"),
    67: ("Heavy freezing rain", "🌧️", "rain"),
    71: ("Slight snow", "🌨️", "snow"),
    73: ("Moderate snow", "🌨️", "snow"),
    75: ("Heavy snow", "❄️", "snow"),
    77: ("Snow grains", "❄️", "snow"),
    80: ("Slight rain showers", "🌦️", "rain"),
    81: ("Moderate rain showers", "🌧️", "rain"),
    82: ("Violent rain showers", "🌧️", "rain"),
    85: ("Slight snow showers", "🌨️", "snow"),
    86: ("Heavy snow showers", "❄️", "snow"),
    95: ("Thunderstorm", "⛈️", "thunderstorm"),
    96: ("Thunderstorm with slight hail", "⛈️", "thunderstorm"),
    99: ("Thunderstorm with heavy hail", "⛈️", "thunderstorm"),
}

# Configurable risk thresholds
RISK_THRESHOLDS = {
    "heavy_rainfall_mm": {"moderate": 5.0, "high": 15.0, "critical": 30.0},
    "strong_wind_kmh": {"moderate": 40.0, "high": 60.0, "critical": 90.0},
    "thunderstorm_codes": [95, 96, 99],
    "extreme_temp_high_c": {"moderate": 38.0, "high": 42.0, "critical": 45.0},
    "extreme_temp_low_c": {"moderate": 5.0, "high": 0.0, "critical": -5.0},
}

# ---------------------------------------------------------------------------
# Haversine distance (km) — same formula used by util.h in the C++ engine
# ---------------------------------------------------------------------------
def haversine_km(lat1, lon1, lat2, lon2):
    R = 6371.0
    k = math.pi / 180.0
    dLat = (lat2 - lat1) * k
    dLon = (lon2 - lon1) * k
    a = (math.sin(dLat / 2) ** 2 +
         math.cos(lat1 * k) * math.cos(lat2 * k) * math.sin(dLon / 2) ** 2)
    return 2 * R * math.asin(math.sqrt(a))


# ---------------------------------------------------------------------------
# Weather code interpretation
# ---------------------------------------------------------------------------
def interpret_weather_code(code):
    if code in WMO_CODES:
        desc, icon, category = WMO_CODES[code]
        return {"description": desc, "icon": icon, "category": category}
    return {"description": "Unknown", "icon": "❓", "category": "unknown"}


# ---------------------------------------------------------------------------
# Risk assessment
# ---------------------------------------------------------------------------
def assess_risk(weather):
    """Compute weather-based risk indicators. These are NOT confirmed disaster alerts."""
    risks = []

    # Heavy rainfall
    precip = weather.get("precipitation", 0) or 0
    th = RISK_THRESHOLDS["heavy_rainfall_mm"]
    if precip >= th["critical"]:
        risks.append({"type": "Heavy Rainfall", "level": "CRITICAL",
                       "value": f"{precip:.1f} mm", "detail": "Extremely heavy precipitation detected"})
    elif precip >= th["high"]:
        risks.append({"type": "Heavy Rainfall", "level": "HIGH",
                       "value": f"{precip:.1f} mm", "detail": "Heavy precipitation ongoing"})
    elif precip >= th["moderate"]:
        risks.append({"type": "Heavy Rainfall", "level": "MODERATE",
                       "value": f"{precip:.1f} mm", "detail": "Moderate precipitation"})

    # Strong winds
    wind = weather.get("wind_speed", 0) or 0
    th = RISK_THRESHOLDS["strong_wind_kmh"]
    if wind >= th["critical"]:
        risks.append({"type": "Strong Winds", "level": "CRITICAL",
                       "value": f"{wind:.1f} km/h", "detail": "Dangerously high wind speed"})
    elif wind >= th["high"]:
        risks.append({"type": "Strong Winds", "level": "HIGH",
                       "value": f"{wind:.1f} km/h", "detail": "Very strong winds"})
    elif wind >= th["moderate"]:
        risks.append({"type": "Strong Winds", "level": "MODERATE",
                       "value": f"{wind:.1f} km/h", "detail": "Elevated wind speed"})

    # Thunderstorms
    code = weather.get("weather_code", -1)
    if code in RISK_THRESHOLDS["thunderstorm_codes"]:
        level = "CRITICAL" if code == 99 else "HIGH"
        risks.append({"type": "Thunderstorm", "level": level,
                       "value": interpret_weather_code(code)["description"],
                       "detail": "Active thunderstorm detected via weather code"})

    # Extreme high temperature
    temp = weather.get("temperature", None)
    if temp is not None:
        th = RISK_THRESHOLDS["extreme_temp_high_c"]
        if temp >= th["critical"]:
            risks.append({"type": "Extreme Heat", "level": "CRITICAL",
                           "value": f"{temp:.1f}°C", "detail": "Life-threatening heat"})
        elif temp >= th["high"]:
            risks.append({"type": "Extreme Heat", "level": "HIGH",
                           "value": f"{temp:.1f}°C", "detail": "Dangerously high temperature"})
        elif temp >= th["moderate"]:
            risks.append({"type": "Extreme Heat", "level": "MODERATE",
                           "value": f"{temp:.1f}°C", "detail": "Very high temperature"})

        # Extreme low temperature
        th = RISK_THRESHOLDS["extreme_temp_low_c"]
        if temp <= th["critical"]:
            risks.append({"type": "Extreme Cold", "level": "CRITICAL",
                           "value": f"{temp:.1f}°C", "detail": "Dangerously low temperature"})
        elif temp <= th["high"]:
            risks.append({"type": "Extreme Cold", "level": "HIGH",
                           "value": f"{temp:.1f}°C", "detail": "Very cold temperature"})
        elif temp <= th["moderate"]:
            risks.append({"type": "Extreme Cold", "level": "MODERATE",
                           "value": f"{temp:.1f}°C", "detail": "Low temperature"})

    if not risks:
        risks.append({"type": "All Clear", "level": "NORMAL",
                       "value": "—", "detail": "No weather-based risks detected"})

    return risks


# ---------------------------------------------------------------------------
# API calls with caching and error handling
# ---------------------------------------------------------------------------
_cache = {}
_cache_lock = threading.Lock()


def _cached_request(url, cache_key, timeout=20, post_data=None):
    """HTTP GET or POST with in-memory TTL cache."""
    with _cache_lock:
        entry = _cache.get(cache_key)
        if entry and (time.time() - entry["ts"]) < CACHE_TTL_SECONDS:
            return entry["data"]

    req = urllib.request.Request(url, data=post_data, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = json.load(resp)

    with _cache_lock:
        _cache[cache_key] = {"data": data, "ts": time.time()}
    return data


def forward_geocode(query_str):
    """Search for locations by place name using OSM Nominatim."""
    params = urllib.parse.urlencode({
        "q": query_str, "format": "json", "limit": 5, "addressdetails": 1
    })
    url = f"{NOMINATIM_SEARCH_API}?{params}"
    cache_key = f"nomsearch:{query_str.lower().strip()}"
    try:
        data = _cached_request(url, cache_key, timeout=10)
        results = []
        for item in data:
            results.append({
                "name": item.get("name") or query_str,
                "display": item.get("display_name", ""),
                "lat": float(item["lat"]),
                "lon": float(item["lon"]),
                "type": item.get("type", "locality"),
            })
        return {"status": "ok", "results": results}
    except Exception as e:
        return {"status": "error", "message": str(e), "results": []}


def reverse_geocode(lat, lon):
    """Reverse geocode coordinates to a place name using OSM Nominatim."""
    params = urllib.parse.urlencode({
        "lat": lat, "lon": lon, "format": "json",
        "zoom": 14, "addressdetails": 1
    })
    url = f"{NOMINATIM_API}?{params}"
    try:
        data = _cached_request(url, f"nominatim:{lat:.4f},{lon:.4f}")
        addr = data.get("address", {})
        # Try to get the most specific locality name
        name = (addr.get("city") or addr.get("town") or addr.get("village")
                or addr.get("suburb") or addr.get("municipality")
                or addr.get("county") or data.get("display_name", "Unknown"))
        state = addr.get("state", "")
        country = addr.get("country", "")
        return {
            "name": name,
            "display": f"{name}, {state}" if state else name,
            "state": state,
            "country": country,
            "full": data.get("display_name", ""),
        }
    except Exception as e:
        return {"name": "Unknown Location", "display": "Unknown Location",
                "state": "", "country": "", "full": str(e)}


def discover_nearby_places(lat, lon, radius_km):
    """Find nearby populated places using OSM Overpass API, with Nominatim fallback."""
    places = _discover_via_overpass(lat, lon, radius_km)
    if not places:
        places = _discover_via_nominatim_grid(lat, lon, radius_km)
    return places


def _discover_via_overpass(lat, lon, radius_km):
    """Primary discovery: Overpass API across multiple official mirrors with POST."""
    radius_m = int(radius_km * 1000)
    query = f"""
    [out:json][timeout:12];
    (
      node["place"~"city|town|village"](around:{radius_m},{lat},{lon});
    );
    out body;
    """
    post_body = urllib.parse.urlencode({"data": query}).encode("utf-8")
    cache_key = f"overpass:{lat:.3f},{lon:.3f}:{radius_km}"

    for mirror in OVERPASS_MIRRORS[:2]:
        try:
            data = _cached_request(mirror, cache_key, timeout=4, post_data=post_body)
            elements = data.get("elements", [])
            if not elements:
                continue

            places = []
            seen_names = set()
            for el in elements:
                name = el.get("tags", {}).get("name")
                if not name or name in seen_names:
                    continue
                seen_names.add(name)
                plat = el.get("lat")
                plon = el.get("lon")
                if plat is None or plon is None:
                    continue
                dist = haversine_km(lat, lon, plat, plon)
                place_type = el.get("tags", {}).get("place", "unknown")
                population = el.get("tags", {}).get("population")
                try:
                    population = int(population) if population else None
                except (ValueError, TypeError):
                    population = None

                places.append({
                    "name": name,
                    "lat": plat,
                    "lon": plon,
                    "distance_km": round(dist, 1),
                    "type": place_type,
                    "population": population,
                })

            places.sort(key=lambda p: p["distance_km"])
            places = [p for p in places if p["distance_km"] >= 1.0]
            if places:
                return places[:MAX_NEARBY_PLACES]
        except Exception as e:
            # Overpass mirror failed or timed out, try next mirror
            continue

    return []


def _discover_via_nominatim_grid(lat, lon, radius_km):
    """Fallback discovery: probe Nominatim around the user coordinates."""
    offsets_deg = radius_km / 111.0
    places = []
    seen_names = set()
    angles = [0, 60, 120, 180, 240, 300]
    distances = [0.35, 0.75]

    for frac in distances:
        for angle_deg in angles:
            angle = math.radians(angle_deg)
            d = offsets_deg * frac
            plat = lat + d * math.cos(angle)
            plon = lon + d * math.sin(angle) / max(math.cos(math.radians(lat)), 0.01)
            try:
                params = urllib.parse.urlencode({
                    "lat": plat, "lon": plon, "format": "json",
                    "zoom": 10, "addressdetails": 1
                })
                url = f"{NOMINATIM_API}?{params}"
                data = _cached_request(url, "nomgrid:{:.3f},{:.3f}".format(plat, plon), timeout=4)
                addr = data.get("address", {})
                name = (addr.get("city") or addr.get("town") or addr.get("village")
                        or addr.get("suburb") or addr.get("municipality")
                        or addr.get("hamlet") or addr.get("county") or addr.get("state_district")
                        or data.get("name"))
                if not name or name in seen_names:
                    continue
                seen_names.add(name)
                rlat = float(data.get("lat", plat))
                rlon = float(data.get("lon", plon))
                dist = haversine_km(lat, lon, rlat, rlon)
                if dist < 1.0 or dist > radius_km:
                    continue
                place_type = ("city" if addr.get("city") else "town" if addr.get("town")
                              else "village" if addr.get("village") else "locality")
                places.append({
                    "name": name, "lat": rlat, "lon": rlon,
                    "distance_km": round(dist, 1), "type": place_type,
                    "population": None,
                })
                if len(places) >= MAX_NEARBY_PLACES:
                    break
                time.sleep(0.4)
            except Exception:
                continue
        if len(places) >= MAX_NEARBY_PLACES:
            break

    places.sort(key=lambda p: p["distance_km"])
    return places[:MAX_NEARBY_PLACES]


def fetch_weather_batch(locations):
    """Fetch weather for multiple locations in a single Open-Meteo request."""
    if not locations:
        return []

    params = urllib.parse.urlencode({
        "latitude": ",".join(str(loc["lat"]) for loc in locations),
        "longitude": ",".join(str(loc["lon"]) for loc in locations),
        "current": ",".join([
            "temperature_2m", "apparent_temperature", "relative_humidity_2m",
            "precipitation", "wind_speed_10m", "weather_code", "is_day"
        ]),
        "timezone": "auto",
    })
    url = f"{OPEN_METEO_API}?{params}"
    coord_parts = []
    for l in locations:
        coord_parts.append("{:.3f}:{:.3f}".format(l["lat"], l["lon"]))
    cache_key = "meteo:" + ",".join(coord_parts)

    try:
        data = _cached_request(url, cache_key)
        # One location returns a dict, multiple returns a list
        results = data if isinstance(data, list) else [data]
        weather_list = []
        for i, res in enumerate(results):
            current = res.get("current", {})
            code = current.get("weather_code", 0)
            interp = interpret_weather_code(code)
            weather = {
                "temperature": current.get("temperature_2m"),
                "feels_like": current.get("apparent_temperature"),
                "humidity": current.get("relative_humidity_2m"),
                "precipitation": current.get("precipitation"),
                "wind_speed": current.get("wind_speed_10m"),
                "weather_code": code,
                "is_day": current.get("is_day", 1) == 1,
                "condition": interp["description"],
                "icon": interp["icon"],
                "category": interp["category"],
                "time": current.get("time", ""),
            }
            weather["risks"] = assess_risk(weather)
            weather_list.append(weather)
        return weather_list
    except Exception as e:
        print(f"Open-Meteo API error: {e}", file=sys.stderr)
        return [None] * len(locations)


# ---------------------------------------------------------------------------
# Main handler: process a location request
# ---------------------------------------------------------------------------
def process_location(lat, lon, radius_km=DEFAULT_RADIUS_KM):
    """Full pipeline: reverse geocode → discover nearby → fetch weather."""
    result = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S %Z", time.localtime()),
        "user_location": {"lat": lat, "lon": lon},
        "radius_km": radius_km,
        "auto_refresh_minutes": AUTO_REFRESH_MINUTES,
    }

    # Step 1: Reverse geocode user location
    geo = reverse_geocode(lat, lon)
    result["user_location"]["name"] = geo["name"]
    result["user_location"]["display"] = geo["display"]
    result["user_location"]["state"] = geo["state"]
    result["user_location"]["country"] = geo["country"]

    # Step 2: Discover nearby places
    nearby = discover_nearby_places(lat, lon, radius_km)
    result["nearby_count"] = len(nearby)

    # Step 3: Fetch weather for user location + all nearby places
    all_locations = [{"lat": lat, "lon": lon, "name": geo["name"]}] + nearby
    weather_data = fetch_weather_batch(all_locations)

    # Step 4: Attach weather to user location
    if weather_data and weather_data[0]:
        result["user_weather"] = weather_data[0]
        result["user_weather"]["location"] = geo["display"]
    else:
        result["user_weather"] = None

    # Step 5: Attach weather to nearby places
    result["nearby"] = []
    for i, place in enumerate(nearby):
        entry = dict(place)
        w_idx = i + 1
        if w_idx < len(weather_data) and weather_data[w_idx]:
            entry["weather"] = weather_data[w_idx]
        else:
            entry["weather"] = None
        result["nearby"].append(entry)

    # Step 6: Compute overall risk summary
    all_risks = []
    if result["user_weather"]:
        all_risks.extend(result["user_weather"].get("risks", []))
    for n in result["nearby"]:
        if n.get("weather"):
            all_risks.extend(n["weather"].get("risks", []))

    # Determine highest risk level
    level_order = {"NORMAL": 0, "MODERATE": 1, "HIGH": 2, "CRITICAL": 3}
    max_level = "NORMAL"
    for r in all_risks:
        if level_order.get(r["level"], 0) > level_order.get(max_level, 0):
            max_level = r["level"]
    result["overall_risk_level"] = max_level
    result["risk_thresholds"] = RISK_THRESHOLDS
    result["status"] = "ok"
    return result


# ---------------------------------------------------------------------------
# HTTP server
# ---------------------------------------------------------------------------
class WeatherHandler(http.server.BaseHTTPRequestHandler):
    radius_km = DEFAULT_RADIUS_KM

    def log_message(self, fmt, *args):
        # Quieter logging
        sys.stderr.write(f"[{self.log_date_time_string()}] {fmt % args}\n")

    def _send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_html(self, content, status=200):
        body = content.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/" or path == "/index.html":
            # Serve the dashboard HTML
            html_path = os.path.join(os.path.dirname(__file__), "nearby_weather_dashboard.html")
            try:
                with open(html_path, "r", encoding="utf-8") as f:
                    self._send_html(f.read())
            except FileNotFoundError:
                self._send_html("<h1>Dashboard file not found</h1>", 404)
        elif path == "/api/health":
            self._send_json({"status": "ok", "service": "ResQNet Nearby Weather Monitor"})
        elif path == "/api/search":
            q = query.get("q", [""])[0].strip()
            if not q:
                self._send_json({"status": "error", "message": "Missing search query parameter 'q'"}, 400)
            else:
                self._send_json(forward_geocode(q))
        elif path == "/api/weather":
            try:
                lat = float(query["lat"][0])
                lon = float(query["lon"][0])
                radius = float(query.get("radius_km", [self.radius_km])[0])
                result = process_location(lat, lon, radius)
                self._send_json(result)
            except (KeyError, ValueError, IndexError) as e:
                self._send_json({"status": "error", "message": f"Invalid query parameters: {e}"}, 400)
        else:
            self._send_html("<h1>404 Not Found</h1>", 404)

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/api/weather":
            try:
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length)
                params = json.loads(body)
                lat = float(params["lat"])
                lon = float(params["lon"])
                radius = float(params.get("radius_km", self.radius_km))
                result = process_location(lat, lon, radius)
                self._send_json(result)
            except (json.JSONDecodeError, KeyError, ValueError, TypeError) as e:
                self._send_json({"status": "error", "message": f"Invalid request: {e}"}, 400)
            except Exception as e:
                self._send_json({"status": "error", "message": f"Server error: {e}"}, 500)
        elif path == "/api/search":
            try:
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length)
                params = json.loads(body)
                q = params.get("query", "").strip()
                if not q:
                    self._send_json({"status": "error", "message": "Query cannot be empty"}, 400)
                else:
                    self._send_json(forward_geocode(q))
            except Exception as e:
                self._send_json({"status": "error", "message": f"Search error: {e}"}, 500)
        else:
            self._send_json({"status": "error", "message": "Not found"}, 404)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()


def print_cli_summary(lat, lon, radius_km=DEFAULT_RADIUS_KM):
    """Print a text-based weather summary for the CLI."""
    print("Fetching nearby weather data...")
    result = process_location(lat, lon, radius_km)

    if result.get("status") != "ok":
        print("Failed to fetch weather data.")
        return

    loc = result["user_location"]
    print(f"\n{'='*60}")
    print(f"  NEARBY WEATHER MONITOR — {loc['display']}")
    print(f"  Coordinates: {lat:.4f}°N, {lon:.4f}°E")
    print(f"  Radius: {radius_km} km  |  Updated: {result['timestamp']}")
    print(f"{'='*60}")

    uw = result.get("user_weather")
    if uw:
        print(f"\n  📍 YOUR LOCATION: {loc['display']}")
        print(f"     {uw['icon']} {uw['condition']}  |  {uw['temperature']:.1f}°C (feels {uw['feels_like']:.1f}°C)")
        print(f"     💧 Humidity: {uw['humidity']}%  |  🌧 Precip: {uw['precipitation']:.1f} mm  |  💨 Wind: {uw['wind_speed']:.1f} km/h")
        for r in uw.get("risks", []):
            if r["level"] != "NORMAL":
                sym = "🔴" if r["level"] == "CRITICAL" else "🟡" if r["level"] == "HIGH" else "🟠"
                print(f"     {sym} [{r['level']}] {r['type']}: {r['value']}")

    nearby = result.get("nearby", [])
    if nearby:
        print(f"\n  {'—'*56}")
        print(f"  NEARBY LOCATIONS ({len(nearby)} found)")
        print(f"  {'—'*56}")
        for p in nearby:
            w = p.get("weather")
            dist = p["distance_km"]
            if w:
                print(f"\n  📌 {p['name']} ({dist:.1f} km away)")
                print(f"     {w['icon']} {w['condition']}  |  {w['temperature']:.1f}°C (feels {w['feels_like']:.1f}°C)")
                print(f"     💧 {w['humidity']}%  |  🌧 {w['precipitation']:.1f} mm  |  💨 {w['wind_speed']:.1f} km/h")
                for r in w.get("risks", []):
                    if r["level"] != "NORMAL":
                        sym = "🔴" if r["level"] == "CRITICAL" else "🟡" if r["level"] == "HIGH" else "🟠"
                        print(f"     {sym} [{r['level']}] {r['type']}: {r['value']}")
            else:
                print(f"\n  📌 {p['name']} ({dist:.1f} km) — weather unavailable")
    else:
        print("\n  No nearby populated places found in the search radius.")

    print(f"\n{'='*60}")
    risk_level = result.get("overall_risk_level", "NORMAL")
    sym = {"NORMAL": "✅", "MODERATE": "🟠", "HIGH": "🟡", "CRITICAL": "🔴"}.get(risk_level, "❓")
    print(f"  OVERALL RISK LEVEL: {sym} {risk_level}")
    print(f"  ⚠️  Weather observations only — NOT confirmed disaster alerts.")
    print(f"{'='*60}\n")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="ResQNet Nearby Weather Monitor — serves a dashboard on localhost.")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"HTTP server port (default: {DEFAULT_PORT})")
    parser.add_argument("--radius", type=int, default=DEFAULT_RADIUS_KM,
                        help=f"Search radius in km (default: {DEFAULT_RADIUS_KM})")
    parser.add_argument("--cli", nargs=2, metavar=("LAT", "LON"),
                        help="Print CLI summary for given coordinates and exit")
    args = parser.parse_args()

    if args.cli:
        try:
            lat, lon = float(args.cli[0]), float(args.cli[1])
        except ValueError:
            print("Invalid coordinates. Use: --cli LAT LON", file=sys.stderr)
            sys.exit(1)
        print_cli_summary(lat, lon, args.radius)
        sys.exit(0)

    WeatherHandler.radius_km = args.radius
    server = http.server.HTTPServer(("127.0.0.1", args.port), WeatherHandler)
    print(f"\n{'='*60}")
    print(f"  RESQNET NEARBY WEATHER MONITOR")
    print(f"  Dashboard: http://127.0.0.1:{args.port}")
    print(f"  Search radius: {args.radius} km")
    print(f"  Auto-refresh: every {AUTO_REFRESH_MINUTES} minutes")
    print(f"{'='*60}")
    print(f"  Press Ctrl+C to stop.\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.server_close()


if __name__ == "__main__":
    main()
