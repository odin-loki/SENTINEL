# Real-Data Evaluation Report

SENTINEL is tested against curated public datasets stored in `data/`. This document summarises what we have, how to run the tests, and measured results from the evaluation suite.

## Do we have real data?

**Yes.** Seven datasets covering every major pipeline stage are stored locally:

| Dataset | File | Records | Source |
|---------|------|---------|--------|
| UK street crimes | `crimes/uk_metropolitan_street_2024.csv` | 57,099 | [data.police.uk](https://data.police.uk/) |
| Cincinnati PDI | `crimes/cincinnati_pdi_crimes_sample.csv` | 15,000 | [Cincinnati open data](https://data.cincinnati-oh.gov/) |
| SFPD incidents | `crimes/sfpd_incidents_sample.csv` | 15,000 | [SF open data](https://data.sfgov.org/) |
| London fixture | `crimes/london_crimes_2024.csv` | 100 | Project fixture |
| Chicago co-offending | `co_offending/chicago_co_offending.csv` | 1,960 | Derived from Chicago arrests |
| Moreno network | `co_offending/moreno_person_crime.csv` | 1,476 | [KONECT Moreno Crime](http://konect.cc/networks/moreno_crime/) |
| London weather | `weather/london_2024_h1.json` | 4,368 hourly | [Open-Meteo](https://open-meteo.com/) |

Full-city bulk downloads (~200 MB each) live in `data/bulk/` and are used only to regenerate samples.

## How to run

### One command (recommended)

```powershell
cd sentinel
& scripts/run_real_data_tests.ps1
```

This checks datasets, builds `test_local_datasets` and `test_real_data_evaluation`, and runs both.

### Manual

```powershell
& data/scripts/fetch_datasets.ps1          # if data missing
cd build
cmake --build . --target test_local_datasets test_real_data_evaluation -j4
ctest -R "test_local_datasets|test_real_data_evaluation" -V
```

Evaluation tests print `EVAL:` metric lines to stderr for quick inspection.

---

## Results (measured on this machine)

### 1. Data ingestion and quality

| Source | Sample size | Coord coverage | Date coverage | Quality pass rate | Avg composite score | Crime types |
|--------|-------------|----------------|---------------|-------------------|---------------------|-------------|
| UK Police | 8,000 | **100%** | **100%** | **100%** | **0.87** | 14 |
| Cincinnati | 5,000 | 85% (4,268) | ~100% | **100%** | high | 51 |
| SFPD | 5,000 | 95% (4,737) | ~100% | **100%** | high | 4* |

\*SFPD exports many subcategories in separate columns; `CsvImporter` maps `Incident Category` and the sample's dominant categories collapse to four top-level labels in the parsed field. Import still succeeds with full coords and timestamps after adding `yyyy/MM/dd` date parsing.

**Verdict:** UK and US city CSVs import reliably. UK data is the strongest (official API format, month-level dates, street locations). US data is usable with ~85–95% coordinate coverage on these samples.

### 2. Spatial hotspot prediction (UK temporal holdout)

Train KDE on **Jan–Apr 2024** crimes (38,541 points), evaluate on **May–Jun 2024** (18,558 points):

| Metric | Value | Interpretation |
|--------|-------|----------------|
| **PAI @ 5%** | **6.93** | Flagging top 5% of area captures ~7× more crimes than random |
| **PAI @ 10%** | **6.58** | Strong hotspot concentration |
| **AUC-ROC** | **0.993** | Excellent ranking of crime cells vs non-crime cells |

**Verdict:** KDE hotspot model performs well on real London crime geography. PAI well above 1.0 means the model adds real predictive value over uniform search.

### 3. Poisson baseline and series detection

On a 1,500-event UK subsample:

- Poisson baseline fits with **1,500** events
- Series detector finds **4** linked series (all with 3+ members)

**Verdict:** Statistical models run on real UK volume without synthetic fixtures.

### 4. Co-offending networks

| Network | Nodes | Behaviour |
|---------|-------|-----------|
| Moreno Crime | 200+ persons | PageRank/betweenness graph builds; edges present |
| Chicago arrests | 1,960 nodes | **835 incidents** produce co-offender leads |

**Verdict:** `CoOffendingAnalyser` works on both academic bipartite data and real derived co-arrest pairs.

### 5. Weather enrichment

- **4,368** hourly records cached from Open-Meteo H1 2024
- Lookup hit rate against UK crime timestamps: **100%** on 500-event sample (500/500 hits)

**Verdict:** Weather JSON parses correctly; enrichment is viable for hour-precision data. Month-only UK dates get partial matches.

### 6. End-to-end mini pipeline (`test_local_datasets`)

On 1,500 UK events: quality scoring → Poisson fit → KDE hotspots (≥3 regions) → HintEngine leads.

**Verdict:** Full ingest-to-leads path works on real data.

---

## What works well

1. **UK Police CSV** — best real-data source: coords, types, dates, quality scores all strong
2. **Hotspot prediction** — PAI ~7× at 5% area on temporal holdout
3. **Multi-city CSV import** — Cincinnati and SFPD formats auto-detected
4. **Co-offending** — both academic and Chicago-derived graphs produce leads
5. **Reproducibility** — fixed local files, scripted fetch, automated tests

## Known limitations on real data

| Limitation | Impact |
|------------|--------|
| SFPD rows without lat/lon (~5%) | Quarantine/filter via quality scorer |
| UK dates are month-only (`yyyy-MM`) | Weather hour-match is partial |
| Cincinnati duplicate incident rows | Inflates row count; coords still valid |
| No narrative text in UK/US exports | NLP/MO tested on london fixture only |
| Series detection memory on 4k+ events | Evaluation uses 1,500-event subsample |

## Test files

| Test | Purpose | Typical runtime |
|------|---------|-----------------|
| `test_local_datasets` | Import + graphs + weather + mini pipeline | ~6 min |
| `test_real_data_evaluation` | Quantitative metrics and holdout PAI | ~8 min |

Both require `data/` present and `SENTINEL_DATA_DIR` set at compile time to `sentinel/data`.

---

## Refreshing data

```powershell
& data/scripts/fetch_datasets.ps1
```

Skips existing files. Bulk archives in `data/bulk/` speed up sample regeneration without re-downloading.

---

*Last evaluated: June 2026 — metrics from `test_real_data_evaluation` EVAL output.*
