#!/usr/bin/env python3
"""Download and prepare local test datasets for SENTINEL integration tests."""

from __future__ import annotations

import csv
import json
import sys
import tarfile
import urllib.request
from collections import defaultdict
from io import BytesIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CRIMES = ROOT / "crimes"
COOFF = ROOT / "co_offending"
WEATHER = ROOT / "weather"


def download(url: str, dest: Path) -> None:
    print(f"GET {url}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "SENTINEL-data-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=300) as resp:
        data = resp.read()
    dest.write_bytes(data)
    print(f"  -> {dest} ({len(data):,} bytes)")


def trim_csv(src: Path, dest: Path, max_rows: int) -> int:
    """Keep header + first max_rows data rows."""
    with src.open("r", encoding="utf-8", errors="replace", newline="") as fin:
        reader = csv.reader(fin)
        header = next(reader, None)
        if header is None:
            dest.write_text("", encoding="utf-8")
            return 0
        rows = []
        for i, row in enumerate(reader):
            if i >= max_rows:
                break
            rows.append(row)
    with dest.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.writer(fout)
        writer.writerow(header)
        writer.writerows(rows)
    print(f"  trimmed {src.name} -> {dest.name} ({len(rows)} rows)")
    return len(rows)


def fetch_uk_police_poly(months: list[str], dest: Path) -> int:
    """Fetch UK street crimes for Greater London bounding polygon."""
    poly = "51.28,-0.51:51.69,-0.51:51.69,0.33:51.28,0.33"
    header = [
        "Crime ID",
        "Month",
        "Reported by",
        "Falls within",
        "Longitude",
        "Latitude",
        "Location",
        "LSOA code",
        "LSOA name",
        "Crime type",
        "Last outcome category",
        "Context",
    ]
    count = 0
    with dest.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.writer(fout)
        writer.writerow(header)
        for month in months:
            url = (
                "https://data.police.uk/api/crimes-street/all-crime"
                f"?date={month}&poly={poly}"
            )
            print(f"UK police {month} ...")
            req = urllib.request.Request(url, headers={"User-Agent": "SENTINEL-data-fetch/1.0"})
            with urllib.request.urlopen(req, timeout=120) as resp:
                crimes = json.loads(resp.read().decode("utf-8"))
            for c in crimes:
                loc = c.get("location", {}) or {}
                street = loc.get("street", {}) or {}
                writer.writerow([
                    c.get("id", ""),
                    c.get("month", month),
                    c.get("reported_by", ""),
                    c.get("falls_within", ""),
                    c.get("longitude", ""),
                    c.get("latitude", ""),
                    street.get("name", ""),
                    c.get("lsoa_code", ""),
                    c.get("lsoa_name", ""),
                    c.get("category", ""),
                    c.get("outcome_status", {}).get("category", "")
                    if isinstance(c.get("outcome_status"), dict)
                    else "",
                    "",
                ])
                count += 1
    print(f"  -> {dest} ({count} rows)")
    return count


def convert_chicago_arrests_cooffend(src: Path, dest: Path) -> int:
    """Build person-incident records from shared case_number (co-arrest)."""
    by_case: dict[str, list[str]] = defaultdict(list)
    with src.open("r", encoding="utf-8", errors="replace", newline="") as fin:
        reader = csv.DictReader(fin)
        for row in reader:
            case = (row.get("case_number") or row.get("CASE_NUMBER") or "").strip()
            arrest_id = (row.get("id") or row.get("ID") or "").strip()
            if not case or not arrest_id:
                continue
            person_id = f"arrest_{arrest_id}"
            by_case[case].append(person_id)

    count = 0
    with dest.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.writer(fout)
        writer.writerow(["person_id", "incident_id", "role"])
        for case, persons in by_case.items():
            if len(persons) < 2:
                continue
            incident_id = f"case_{case}"
            for person in persons:
                writer.writerow([person, incident_id, "suspect"])
                count += 1
    print(f"  co-offend pairs -> {dest} ({count} records, {len(by_case)} cases)")
    return count


def fetch_moreno_cooffend(dest: Path) -> int:
    """Download Moreno Crime bipartite network and emit person-incident CSV."""
    url = "http://konect.cc/files/download.tsv.moreno_crime.tar.bz2"
    print(f"Moreno crime {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "SENTINEL-data-fetch/1.0"})
    with urllib.request.urlopen(req, timeout=120) as resp:
        raw = resp.read()
    with tarfile.open(fileobj=BytesIO(raw), mode="r:bz2") as tar:
        edges_name = None
        for name in tar.getnames():
            if name.endswith("moreno_crime_crime.txt") or name.endswith("crime.txt"):
                edges_name = name
                break
        if edges_name is None:
            raise RuntimeError("moreno crime edge file not found in archive")
        edges_file = tar.extractfile(edges_name)
        assert edges_file is not None
        lines = edges_file.read().decode("utf-8", errors="replace").splitlines()

    count = 0
    with dest.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.writer(fout)
        writer.writerow(["person_id", "incident_id", "role"])
        for line in lines:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 2:
                continue
            person_id, incident_id = parts[0], parts[1]
            role = parts[2] if len(parts) > 2 else "participant"
            writer.writerow([f"person_{person_id}", f"crime_{incident_id}", role])
            count += 1
    print(f"  -> {dest} ({count} records)")
    return count


def write_manifest(entries: dict) -> None:
    manifest = ROOT / "manifest.json"
    manifest.write_text(json.dumps(entries, indent=2), encoding="utf-8")
    print(f"Wrote {manifest}")


def main() -> int:
    entries: dict[str, object] = {"datasets": []}

    # UK police street crimes (6 months, London polygon)
    uk_path = CRIMES / "uk_metropolitan_street_2024.csv"
    uk_rows = fetch_uk_police_poly(
        ["2024-01", "2024-02", "2024-03", "2024-04", "2024-05", "2024-06"],
        uk_path,
    )
    entries["datasets"].append(
        {
            "id": "uk_police",
            "path": str(uk_path.relative_to(ROOT)),
            "rows": uk_rows,
            "source": "https://data.police.uk/docs/method/crime-street/",
            "license": "Open Government Licence v3.0",
        }
    )

    # US samples via Socrata with explicit $limit
    samples = [
        (
            "cincinnati_pdi_crimes_sample.csv",
            "https://data.cincinnati-oh.gov/api/views/k59e-2pvf/rows.csv?$limit=15000",
            15000,
            "US Cincinnati PDI",
        ),
        (
            "sfpd_incidents_sample.csv",
            "https://data.sfgov.org/api/views/wg3w-h783/rows.csv?$limit=15000",
            15000,
            "US SFPD incidents",
        ),
        (
            "chicago_arrests_sample.csv",
            "https://data.cityofchicago.org/api/views/dpt3-jri9/rows.csv?$limit=15000",
            15000,
            "US Chicago arrests",
        ),
    ]
    for fname, url, limit, label in samples:
        tmp = CRIMES / f"_tmp_{fname}"
        if "chicago" in fname:
            tmp = COOFF / f"_tmp_{fname}"
        download(url, tmp)
        out = tmp.parent / fname
        rows = trim_csv(tmp, out, limit)
        tmp.unlink(missing_ok=True)
        entries["datasets"].append(
            {
                "id": fname.replace(".csv", ""),
                "path": str(out.relative_to(ROOT)),
                "rows": rows,
                "source": url.split("?")[0],
                "license": "public open data",
                "label": label,
            }
        )

    # Co-offending derived datasets
    chicago_src = COOFF / "chicago_arrests_sample.csv"
    chicago_co = COOFF / "chicago_co_offending.csv"
    co_rows = convert_chicago_arrests_cooffend(chicago_src, chicago_co)
    entries["datasets"].append(
        {
            "id": "chicago_co_offending",
            "path": str(chicago_co.relative_to(ROOT)),
            "rows": co_rows,
            "source": "derived from Chicago arrests (shared case_number)",
        }
    )

    moreno_co = COOFF / "moreno_person_crime.csv"
    moreno_rows = fetch_moreno_cooffend(moreno_co)
    entries["datasets"].append(
        {
            "id": "moreno_person_crime",
            "path": str(moreno_co.relative_to(ROOT)),
            "rows": moreno_rows,
            "source": "http://konect.cc/networks/moreno_crime/",
            "license": "KONECT / academic use",
        }
    )

    # Weather snapshot (London H1 2024)
    weather_path = WEATHER / "london_2024_h1.json"
    weather_url = (
        "https://historical-forecast-api.open-meteo.com/v1/forecast?"
        "latitude=51.5074&longitude=-0.1278&start_date=2024-01-01&end_date=2024-06-30"
        "&hourly=temperature_2m,precipitation,windspeed_10m,visibility,is_day,weathercode"
        "&timezone=Europe%2FLondon&wind_speed_unit=kmh"
    )
    download(weather_url, weather_path)
    weather_doc = json.loads(weather_path.read_text(encoding="utf-8"))
    hourly_count = len(weather_doc.get("hourly", {}).get("time", []))
    entries["datasets"].append(
        {
            "id": "london_weather_h1",
            "path": str(weather_path.relative_to(ROOT)),
            "hourly_records": hourly_count,
            "source": "https://open-meteo.com/",
            "license": "CC BY 4.0 (Open-Meteo)",
        }
    )

    write_manifest(entries)
    return 0


if __name__ == "__main__":
    sys.exit(main())
