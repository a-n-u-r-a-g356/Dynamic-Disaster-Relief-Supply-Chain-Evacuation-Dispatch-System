#!/usr/bin/env python3
"""Fetch live rainfall for every ZONE in <data>/nodes.csv and write <data>/weather.csv.

Source: Open-Meteo forecast API (free, no key). One request covers all zones.
Usage:  python3 tools/fetch_weather.py [data_folder]
Exit code 0 on success, 1 on any failure (the C++ program then keeps the last values).
"""
import csv
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

API = "https://api.open-meteo.com/v1/forecast"


def read_zones(folder):
    zones = []
    with open(os.path.join(folder, "nodes.csv"), newline="") as f:
        for row in csv.DictReader(f):
            if (row.get("type") or "").strip().upper() == "ZONE":
                zones.append({"id": row["id"].strip(), "lat": row["lat"].strip(), "lon": row["lon"].strip()})
    return zones


def fetch(zones):
    query = urllib.parse.urlencode({
        "latitude": ",".join(z["lat"] for z in zones),
        "longitude": ",".join(z["lon"] for z in zones),
        "current": "precipitation",      # mm in the current hour
        "hourly": "precipitation",       # hourly mm, summed for the last 24 h
        "past_hours": 24,
        "forecast_hours": 1,
        "timezone": "auto",
    })
    with urllib.request.urlopen(f"{API}?{query}", timeout=20) as resp:
        data = json.load(resp)
    return data if isinstance(data, list) else [data]   # one location returns a dict


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "data"
    try:
        zones = read_zones(folder)
    except OSError as e:
        print(f"cannot read nodes.csv: {e}", file=sys.stderr)
        return 1
    if not zones:
        print("no ZONE rows in nodes.csv", file=sys.stderr)
        return 1
    try:
        results = fetch(zones)
    except (urllib.error.URLError, OSError, ValueError) as e:
        print(f"weather request failed: {e}", file=sys.stderr)
        return 1
    if len(results) != len(zones):
        print("unexpected API response size", file=sys.stderr)
        return 1

    rows = []
    for zone, res in zip(zones, results):
        now = (res.get("current") or {}).get("precipitation") or 0.0
        hourly = (res.get("hourly") or {}).get("precipitation") or []
        last24 = sum(x or 0.0 for x in hourly[:24])
        rows.append((zone["id"], round(now, 2), round(last24, 1)))

    with open(os.path.join(folder, "weather.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["id", "rain_now_mm", "rain_24h_mm"])
        w.writerows(rows)
    print(f"weather.csv written for {len(rows)} zone(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
