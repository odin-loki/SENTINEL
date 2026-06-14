# SENTINEL — Analyst Quick Start



This guide walks analysts through installing SENTINEL from the portable release, loading sample crime data, reviewing investigative leads, and exporting results. No build tools or administrator rights are required for the portable ZIP workflow.



---



## 1. Install from the portable ZIP



1. Download `SENTINEL-*-win64-portable.zip` from the [GitHub Releases](https://github.com/odin-loki/SENTINEL/releases) page (or use `SENTINEL-*-win64-setup.exe` for a Start Menu install — same application).

2. Extract the archive to a folder you can write to, for example `C:\SENTINEL`.

3. Confirm the layout:

   - `bin\sentinel.exe` — main application

   - `bin\Qt6Core.dll` — Qt runtime (must be present in portable builds)

   - `share\sentinel\data\` — bundled sample datasets

   - `share\doc\Sentinel\` — documentation



**Linux users:** extract `SENTINEL-*-linux-x86_64.tar.gz` and run `sentinel` from `usr/bin/` (or install the `.deb`). Sample data is under `usr/share/sentinel/data/`.



User settings and the SQLite database are stored automatically:



| Platform | Location |

|----------|----------|

| Windows | `%APPDATA%\SENTINEL\` |

| Linux | `~/.local/share/SENTINEL/` |



---



## 2. Run SENTINEL



**Windows**



```powershell

cd C:\SENTINEL

.\bin\sentinel.exe

```



Double-clicking `sentinel.exe` also works. On first launch, SENTINEL creates an empty local database and opens the main dashboard.



**Linux**



```bash

sentinel

```



Or launch **SENTINEL** from your desktop environment application menu.



### Navigation (9 pages)



Use the left sidebar to switch views:



| Page | Purpose |

|------|---------|

| **Dashboard** | Summary counts, quality metrics, map with risk overlay |

| **Crime Events** | Filterable events table |

| **Analytics** | Charts, **Calibration** tab (live reliability diagram), hotspot PAI, temporal heatmaps |

| **Cases** | Per-case filter, related events, series count, lead history, geo profile summary |

| **Network** | Co-offending graph — zoom/pan, node click, Load CSV (Chicago sample default) |

| **Investigative Leads** | Ranked leads and detected series |

| **Audit Log** | Provenance chain for every model output |

| **Settings** | Pipeline thresholds, quality gate, theme |

| **Debug Console** | Live log output (support / advanced use) |



---



## 3. Import sample data



Bundled sample files live under `share/sentinel/data/crimes/` (Windows portable) or `/usr/share/sentinel/data/crimes/` (Linux).



| File | Rows | Use |

|------|------|-----|

| `uk_metropolitan_street_2024.csv` | ~57k | UK street crime, lat/lon, monthly dates |

| `cincinnati_pdi_crimes_sample.csv` | 15k | US city CSV import |

| `sfpd_incidents_sample.csv` | 15k | US city CSV import |

| `london_crimes_2024.csv` | 100 | Crime-type fixture with coordinates |



**Fastest path (recommended for first run):**



1. Open **File → Import Sample Data** (or press `Ctrl+Shift+I`).

2. SENTINEL loads the bundled `london_crimes_2024.csv`, runs the data-quality scorer and weather enrichment, and shows an **Import Complete** summary (parsed count, average quality, pass/quarantine counts, rows inserted).

3. The app switches to **Analytics** so you can review charts immediately.



**Import your own CSV:**



1. Open **File → Import CSV…** (or press `Ctrl+I`, or click **⬆ Import CSV** on the toolbar).

2. Navigate to `share\sentinel\data\crimes\` (or the Linux equivalent).

3. Select a CSV (use `uk_metropolitan_street_2024.csv` for the richest demo).

4. Review the same **Import Complete** quality summary dialog.

5. Repeat for additional files if you want multi-city coverage.



**Fetch live UK Police data:**



1. Open **File → Fetch UK Police Data…** (`Ctrl+U`).

2. Enter latitude, longitude, and radius in km (default: central London).

3. Events are scored and filtered the same way as CSV imports.



Events below the **Quality Threshold** in **Settings** are quarantined (not stored). Imported events that pass appear in **Crime Events**, **Dashboard**, and **Analytics**.



---



## 4. View leads



After data is loaded, SENTINEL runs the inference pipeline (hotspots, series linkage, MO similarity, anomalies) and ranks investigative leads.



1. Open the **Investigative Leads** tab in the sidebar.

2. Review ranked leads — each entry shows:

   - **Score** and confidence band

   - Linked crime events and geographic context

   - Evidence factors (temporal spike, series match, MO similarity, etc.)

3. Click a lead to highlight related events on the map and in the events table.

4. Use the **Dashboard** for summary counts, quality metrics, and risk overlays.

5. Open **Analytics** for calibration charts (see **Calibration** tab for live reliability diagram), hotspot PAI, and temporal heatmaps.



Leads are regenerated when you import new data or change pipeline settings under **Settings**.



Every lead carries a provenance chain — open the **Audit Log** tab to trace which model and source records produced a given suggestion.



---



## 5. Cases workspace



The **Cases** page (sidebar) lets you focus on a single investigation without leaving the main app.



1. Open **Cases** in the sidebar.

2. Enter a **Case ID** or event ID prefix in the filter box (e.g. `case_HX366462` or a partial event ID), then click **Filter** or press Enter.

3. Review the **Related Events** table — event ID, crime type, date, location, quality score.

4. Check **Series Detection** — shows how many spatiotemporal series `SeriesDetector` found among the filtered events (needs ≥3 events).

5. Review **Lead History** — ranked leads scoped to the filtered case events (rank, category, headline, confidence).

6. Check **Geographic Profile** — when ≥3 geo-tagged events are linked, shows peak anchor coordinates, 50%/80% Rossmo search-area estimates, and a **CGT heatmap** mini-map below the summary.

7. Click **Export Case Report** to save a Markdown report with lead summaries and event provenance (JSON appendix).



**Tips:**

- Leave the filter empty to see the 100 most recent events (useful after a bulk import).

- Import UK or US CSVs with geocoded rows before expecting meaningful series counts.

- Per-crime-type series tuning: open **Settings** → **Model Parameters** to override DBSCAN spatial eps for burglary, theft, and violent crime buckets (0 = use default).

- Case IDs in `meta.caseId` on imported events are matched automatically.



---



## 6. Network view



The **Network** page visualises co-offending relationships from the bundled Chicago sample dataset.



1. Open **Network** in the sidebar.

2. SENTINEL loads `share/sentinel/data/co_offending/chicago_co_offending.csv` automatically.

3. The graph shows:

   - **Nodes** — anonymised person IDs; size reflects PageRank centrality

   - **Edges** — shared-incident links between co-offenders

   - **Status bar** — node and edge counts; hint text for controls

4. **Navigate:** scroll to zoom, drag to pan, click a node for a details tooltip (PageRank, degree, shared incidents).

5. **Load your own graph:** click **Load CSV** and select an agency person–incident file (columns: person ID, incident ID). SENTINEL rebuilds the graph from your file.



**Current limitations:**

- **Load from DB** builds the graph from SQLite events that include `meta.person_id` (or `suspect_id` / `offender_id`). Events without person IDs still require **Load CSV** or the bundled Chicago sample.

- Narrative embeddings (optional ONNX encoder) are not enabled in v1.0.



For the underlying analytics (PageRank, betweenness, community detection), see the **Investigative Leads** panel and exported benchmark reports.



---



## 7. Calibration dashboard



The **Analytics → Calibration** tab runs a live calibration analysis on your imported events.



1. Open **Analytics** in the sidebar.

2. Select the **Calibration** tab.

3. Click **Run Analysis** — SENTINEL holds out the most recent month of events and plots a reliability diagram with ECE, MCE, and Brier score.

4. Review the status badge (Pass / Warn / Fail) against the configured calibration target.



Requires sufficient dated events spanning at least two months. Re-run after importing new data or changing pipeline settings.



---



## 8. Export results



Use **File → Export…** to save outputs for reports or downstream tools:



| Menu item | Format | Contents |

|-----------|--------|----------|

| Events as CSV… | `.csv` | All imported events with quality scores |

| Events as JSON… | `.json` | Structured event records |

| Risk Forecast (JSON)… | `.json` | Zone-level risk forecasts |

| Benchmark Report (Markdown)… | `.md` | PAI, calibration, and evaluation summary |



**Typical analyst workflow:**



1. **File → Import Sample Data** (`Ctrl+Shift+I`) or import UK CSV data.

2. Confirm the quality summary dialog (check pass/quarantine counts).

3. Review charts on **Analytics** (opened automatically after import).

4. Filter a case on **Cases** if investigating a specific incident cluster — use **Export Case Report** for a briefing-ready Markdown file.

5. Review top leads in **Investigative Leads**.

6. Export **Events as CSV** for your case file.

7. Export **Benchmark Report (Markdown)** to attach evaluation metrics to a briefing.



Exports are written to a path you choose — defaults such as `sentinel_events.csv` are offered in the save dialog.



---



## Troubleshooting



| Issue | Fix |

|-------|-----|

| Application won't start | Ensure Qt runtime DLLs are present (`Qt6Core.dll` in `bin\`). Re-extract the portable ZIP or re-run the NSIS installer. |

| Import shows 0 events | Check the CSV has coordinate and date columns; see `share/doc/Sentinel/REAL_DATA_EVALUATION.md`. |

| No leads generated | Import at least a few hundred geocoded events; adjust minimum thresholds in **Settings**. |

| Map is blank | Confirm imported events have valid latitude/longitude. |

| Cases shows 0 series | Need ≥3 related events with coordinates for series detection. |

| Network page is empty | Confirm `share/sentinel/data/co_offending/chicago_co_offending.csv` exists, or use **Load CSV** on the Network page. |

| Calibration tab shows insufficient data | Import more dated events spanning at least two months; click **Run Analysis** again. |



---



## Further reading



- [REAL_DATA_EVALUATION.md](REAL_DATA_EVALUATION.md) — measured quality and PAI metrics on bundled datasets

- [RELEASE.md](RELEASE.md) — portable ZIP vs setup.exe, packaging, verification checklist

- [README.md](../README.md) — architecture and developer build instructions

- [REMAINING.md](../REMAINING.md) — Phase 3–5 backlog (Phase 2 shipped in v1.0)

