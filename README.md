# SENTINEL

**Crime analytics and investigative decision support — C++23 / Qt 6 desktop application**

SENTINEL is a standalone desktop system for ingesting public crime data, running transparent statistical models, and producing ranked investigative leads with full provenance. Every output can be traced back to its source record, model, and parameters. The stack is implemented entirely in C++ with Qt 6 — no external ML runtimes, no opaque model APIs.

---

## Table of Contents

1. [What SENTINEL Does](#what-sentinel-does)
2. [What We Have Built](#what-we-have-built)
3. [Current State](#current-state)
4. [Strategic Plan](#strategic-plan)
5. [Architecture](#architecture)
6. [Local Data & Testing](#local-data--testing)
7. [Building & Running](#building--running)
8. [Releases & Installation](#releases--installation)
9. [Design Principles](#design-principles)

**Analysts:** see **[docs/ANALYST_GUIDE.md](docs/ANALYST_GUIDE.md)** for portable ZIP install, sample data import, leads review, and export.

**Backlog:** see **[REMAINING.md](REMAINING.md)** for Phase 3–5 work not yet shipped.

---

## What SENTINEL Does

SENTINEL supports analysts and researchers who need to:

- **Ingest** heterogeneous crime feeds (UK Police API, open-data CSV exports, weather time series)
- **Score** data quality and quarantine unreliable records before modelling
- **Extract** modus operandi features and classify crime types from narratives
- **Model** spatiotemporal risk (Poisson, Hawkes, KDE hotspots, series linkage, ensembles)
- **Infer** geographic profiles, MO similarity, co-offending networks, and anomalies
- **Generate** ranked investigative leads with contradiction detection and evidence weighting
- **Audit** every step through a provenance log and exportable reports
- **Evaluate** predictions with calibration, fairness, and hotspot-accuracy metrics

The application ships as a native desktop UI (dashboard, map, events table, leads panel, analytics, audit log) backed by SQLite persistence.

---

## What We Have Built

### Core platform

| Area | Delivered |
|------|-----------|
| **Runtime** | C++23 desktop app with Qt 6 widgets, charts, networking, and SQL |
| **Persistence** | SQLite database with schema versioning, event CRUD, provenance storage |
| **Configuration** | `QSettings`-backed pipeline parameters, alert thresholds, UI preferences |
| **Logging** | Ring-buffer log handler with category filtering and debug console widget |
| **Export** | Markdown, JSON, CSV, and HTML output for events and lead reports |

### Data ingestion

| Component | Capability |
|-----------|------------|
| `UKPoliceSource` | Live and historical UK street crime via `data.police.uk` |
| `WeatherSource` | Open-Meteo hourly weather cache for crime–weather correlation |
| `CsvImporter` | Auto-detects columns across UK, US city, and generic crime CSV layouts |
| `DataQualityScorer` | Completeness, temporal/spatial precision, source reliability, quarantine threshold |

### NLP (classical, inspectable)

| Component | Capability |
|-----------|------------|
| `MOExtractor` | Entry method, weapons, target type, solo/group, time-of-day from text |
| `CrimeClassifier` | Keyword TF-IDF classification across 13 crime categories with severity scoring |

### Statistical models

| Model | Role |
|-------|------|
| `PoissonBaseline` | Zone × time × crime-type rate baselines with Negative Binomial overdispersion |
| `HawkesProcess` | Self-exciting near-repeat victimisation |
| `SeriesDetector` | Spatiotemporal DBSCAN + MO Jaccard linkage |
| `KDEHotspot` | 2D Gaussian kernel density hotspots with PAI support |
| `GPRegression` | Squared-exponential GP with Cholesky posterior |
| `BayesianHierarchical` | Gamma–Poisson conjugate zone hierarchy |
| `RiskForecaster` | Multi-day zone risk from Poisson + temporal features |
| `EnsemblePredictor` | Weighted ensemble with epistemic/aleatoric uncertainty split |
| `NearRepeatVictimisation` | Knox ratio clustering and decay-scored alerts |
| `TemporalFeatures` | Cyclical time encoding, lunar phase, payday proximity, night/weekend flags |

### Inference engine

| Component | Role |
|-----------|------|
| `GeographicProfiler` | Rossmo CGT probability surface with search-area thresholds |
| `MOAnalyser` | TF-IDF cosine similarity against resolved cases |
| `EvidenceScorer` | Bayesian likelihood-ratio updating |
| `AnomalyDetector` | Isolation scoring + temporal/spatial z-scores |
| `CoOffendingAnalyser` | PageRank, betweenness, community detection on person–incident graphs |
| `HintEngine` | Ranked leads with contradiction detection and quality weighting |
| `LeadReportGenerator` | Markdown/HTML investigative reports with provenance chains |

### Benchmarking & fairness

| Component | Role |
|-----------|------|
| `BenchmarkMetrics` | PAI, PEI, SER, AUC, MAE, RMSE, Brier, MRR, Precision@K |
| `CalibrationAnalyser` | ECE, MCE, reliability diagrams, isotonic recalibration |
| `BiasAuditor` | Disparate impact, equal opportunity, feedback-loop risk |

### User interface (9 pages)

Sidebar navigation — all wired to the live pipeline:

| # | Page | Widget |
|---|------|--------|
| 0 | Dashboard | Map with risk overlay, summary metrics |
| 1 | Crime Events | Filterable events table |
| 2 | Analytics | Charts, **Calibration** dashboard (live reliability diagram), hotspot PAI, heatmaps |
| 3 | Cases | Case filter, events table, series merge/split overrides, lead history, Rossmo CGT heatmap, Export Case Report |
| 4 | Network | Co-offending graph with zoom/pan, node click, Load CSV, Load from DB (SQLite) |
| 5 | Investigative Leads | Ranked leads + detected series |
| 6 | Audit Log | Provenance chain viewer |
| 7 | Settings | Pipeline thresholds, quality gate, theme |
| 8 | Debug Console | Live log output |

### Test engineering (30 deep-audit iterations)

- **495 automated tests** across unit, integration, stress, benchmark, and headless UI layers
- Deep-audit iterations added focused test files per component (ingest, models, inference, UI widgets, export, provenance)
- Full suite runs in under ~40 seconds parallelised; local real-data integration test runs in ~6 minutes

### Real-world local datasets

Curated data lives under `data/` and is exercised by `test_local_datasets` and `test_real_data_evaluation`:

**Run real-data tests in one step:**

```powershell
& scripts/run_real_data_tests.ps1
```

**Evaluation report:** [docs/REAL_DATA_EVALUATION.md](docs/REAL_DATA_EVALUATION.md)

| Dataset | Rows | Pipeline coverage |
|---------|------|-------------------|
| UK Metropolitan street crimes (H1 2024) | 57,099 | UK API format, lat/lon, crime types, monthly dates |
| Cincinnati PDI sample | 15,000 | US CSV import, coords, offense types |
| SFPD incidents sample | 15,000 | US CSV import |
| London narrative fixture | 100 | MO extraction, classification |
| Chicago co-offending (derived) | 1,960 | Shared-case arrest network |
| Moreno crime network | 1,476 | Person–crime bipartite graph |
| London weather H1 2024 | 4,368 hourly | Open-Meteo JSON parse and cache lookup |

Fetch script: `data/scripts/fetch_datasets.ps1` — see `data/README.md` for licenses and refresh instructions.

---

## Current State

### Strengths

1. **End-to-end pipeline is real and tested** — ingest → NLP → models → inference → UI → audit, not a prototype skeleton.
2. **Transparency** — every algorithm is open C++; weights and thresholds are configurable and inspectable.
3. **Test depth** — 495 tests with iterative hardening across edge cases, stress loads, and widget behaviour.
4. **Real data validation** — local datasets prove importers and models against actual UK/US exports, co-offending graphs, and weather JSON.
5. **Investigative workflow** — leads, evidence scoring, geographic profiling, and co-offending analysis are integrated, not isolated demos.

### Gaps and limitations (honest assessment)

| Gap | Impact |
|-----|--------|
| NLP is rule/keyword-based | Misses nuanced narrative structure; no embedding or transformer layer |
| Single-machine desktop only | No multi-user deployment or federated ingestion; read-only local REST API shipped (`LocalApiServer`) |
| Limited live connectors | UK Police + weather + CSV; no streaming social, court, or news feeds |
| Map is custom QPainter | Functional but not a full GIS (no tile server, routing, or shapefile import) |
| Co-offending needs person IDs | Real agencies rarely publish identifiable arrest graphs at scale |
| Narrative NLP depth | Rule/keyword MO extraction only; no optional ONNX sentence encoder yet |
| No production deployment story | **Portable ZIP + NSIS setup (Windows) and TGZ/DEB (Linux) ship in v1.0.0**; code signing remains; push tag `v1.0.0` to trigger GitHub Release |

### Readiness snapshot

| Capability | Status |
|------------|--------|
| Research / demo / analyst workstation | **Ready** |
| Native install (Windows ZIP/NSIS, Linux TGZ/DEB) | **Ready** — see [docs/RELEASE.md](docs/RELEASE.md) |
| Batch CSV + API ingest for UK/US cities | **Ready** |
| Lead generation with provenance | **Ready** |
| Model benchmarking on held-out data | **Ready** (metrics implemented; benchmark harness needs curated evaluation sets) |
| Operational multi-agency deployment | **Not ready** |
| Real-time streaming ingestion | **Not ready** |

---

## Strategic Plan

**Full roadmap with phase status:** [docs/ROADMAP.md](docs/ROADMAP.md)

This plan assumes SENTINEL continues as an **auditable desktop analytics platform** first, with optional service/API layers added only where they do not compromise traceability.

### Vision (18–24 months)

SENTINEL becomes the reference implementation for **transparent crime analytics**: agencies and researchers can import their own data, reproduce every score, export court-ready provenance reports, and benchmark model fairness before operational use.

### Phase 1 — Consolidate & operationalise ✅ Complete

**Goal:** Turn the current build into a reliable analyst tool someone can use daily.

| Initiative | Status |
|------------|--------|
| Data workspace UI | **Done** — CSV / UK API / sample import → quality summary → Analytics |
| Weather enrichment | **Done** — `IngestEnricher` attaches bundled weather cache on ingest |
| Evaluation harness | **Done** — [REAL_DATA_EVALUATION.md](docs/REAL_DATA_EVALUATION.md) |
| Packaging | **Done** — primary build: `& packaging/windows/deploy_portable.ps1 -BuildDir build` |
| Analyst guide | **Done** — [ANALYST_GUIDE.md](docs/ANALYST_GUIDE.md) |

### Phase 2 — Depth on investigation ✅ Complete

**Goal:** Make SENTINEL indispensable for case linkage and network analysis.

| Initiative | Status | Shipped |
|------------|--------|---------|
| **Case workspace** | **Done** | `CaseWorkspaceWidget`: filter, events, series merge/split overrides, lead history, Rossmo CGT heatmap (`GeoProfileHeatmapWidget`), Export Case Report |
| **Series refinement** | **Done** | Per-crime-type DBSCAN eps in `AppConfig` / `SeriesDetector` + analyst merge/split overrides on Cases page |
| **Network visualisation** | **Done** | `CoOffendingGraphWidget`: zoom/pan, node click, Load CSV, **Load from DB** (`loadFromDatabase()` via SQLite `meta.person_id`) |
| **Calibration dashboard** | **Done** | `CalibrationDashboardWidget` on Analytics **Calibration** tab |
| **Local read-only API** | **Done** | `LocalApiServer` REST endpoints + `test_local_api` |
| **Narrative upgrade** | Not started | Optional ONNX sentence encoder (feature flag) |

See [REMAINING.md](REMAINING.md) for the full Phase 3–5 backlog.

### Phase 3 — Scale & collaboration (6–12 months)

**Goal:** Support larger jurisdictions and small teams without losing auditability.

| Initiative | Actions | Success criteria |
|------------|---------|------------------|
| **Performance** | Parallel ingest, indexed SQLite queries, incremental model refit | 500k events ingest + baseline fit in <5 minutes on workstation |
| **Multi-jurisdiction** | Zone registry, CRS normalisation, per-force config profiles | UK + US cities in one project without column hacks |
| **Optional read-only API** | Local REST layer shipped (`LocalApiServer`); gRPC + auth remain future work | External dashboard can query without duplicating logic |
| **Audit compliance** | Immutable provenance export (signed JSON), retention policies | Export passes internal audit checklist |
| **GIS integration** | Optional MapLibre or Qt Location tile layer; GeoJSON boundary import | Hotspots overlaid on real street map |

### Phase 4 — Research & policy impact (12–18 months)

**Goal:** Position SENTINEL as an evidence-based alternative to black-box predictive policing tools.

| Initiative | Actions | Success criteria |
|------------|---------|------------------|
| **Fairness-by-default** | Pre-deployment bias report required before alerts go live | No zone goes on “high alert” without disparity check |
| **Published benchmarks** | Open benchmark suite on public data with leaderboards | Third parties can reproduce PAI/ECE numbers |
| **Academic partnerships** | Publish methods paper; share evaluation scripts | Peer-reviewed validation of Hawkes + ensemble stack |
| **Policy toolkit** | Exportable “model card” per deployment: data, limits, known failure modes | Usable in governance / ethics review |

### Phase 5 — Optional productisation (18–24 months)

Only pursue if Phases 1–3 validate real user demand.

| Track | Description | Guardrails |
|-------|-------------|------------|
| **Agency desktop licence** | Signed builds, support channel, custom ingest adapters | No cloud dependency; data stays on device |
| **Training mode** | Synthetic scenario generator for academy / university use | Clearly labelled simulated data |
| **Managed analytics service** | Hosted version for researchers | Strict tenant isolation; full provenance export; no proprietary model lock-in |

### Strategic priorities (ordered)

1. **Trust** — provenance, calibration, and fairness before more models
2. **Usability** — UI workflows for import, case review, and export
3. **Evidence** — benchmark harness on real local data splits
4. **Scale** — performance and multi-city normalisation
5. **Innovation** — optional ML only where classical methods plateau

### Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Biased alerts harm communities | Mandatory `BiasAuditor` gate; configurable thresholds; human-in-the-loop leads |
| Data quality varies by city | `DataQualityScorer` quarantine; source reliability map; visible quality badges in UI |
| Co-offending data scarcity | Support agency CSV; derive from case numbers where legal; never assume public PII |
| Model overconfidence | Ensemble uncertainty + calibration dashboard + explicit “low data” warnings |
| Scope creep into opaque AI | Keep classical path default; embeddings opt-in with full provenance |

### Metrics that define success

| Metric | Target (12 months) |
|--------|-------------------|
| Automated test count | Maintain ≥495; add regression tests per new ingest format |
| Real-data integration | ≥3 cities + UK + weather on every release |
| Benchmark reproducibility | Fixed PAI/ECE report from `data/` with published command |
| Analyst time-to-first-lead | <15 minutes from install to ranked lead on sample data |
| Provenance coverage | 100% of leads traceable to source row + model + parameters |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     SENTINEL DESKTOP PIPELINE                    │
├─────────────────────────────────────────────────────────────────┤
│  INGEST          NLP              FEATURES                       │
│  UKPolice        MOExtractor      TemporalFeatures               │
│  Weather         CrimeClassifier  DataQualityScorer              │
│  CsvImporter                                                     │
│       │                │                  │                    │
│       └────────────────┴──────────────────┘                    │
│                          ▼                                       │
│  MODELS: Poisson · Hawkes · Series · KDE · GP · Bayesian ·       │
│          Ensemble · NearRepeat · RiskForecaster                  │
│                          ▼                                       │
│  INFERENCE: GeoProfiler · MOAnalyser · Evidence · Anomaly ·      │
│             CoOffending · HintEngine · LeadReports               │
│                          ▼                                       │
│  SQLite · ProvenanceLog · BenchmarkMetrics · Qt UI               │
└─────────────────────────────────────────────────────────────────┘
```

**Technology:** C++23, Qt 6 (Core, Widgets, Network, Charts, SQL, Test), CMake 3.27+, SQLite, pure C++ statistics (no external ML libraries in the classical path).

---

## Local Data & Testing

```powershell
# One command: fetch (if needed) + build + run real-data tests
& scripts/run_real_data_tests.ps1
```

```bash
# Or manually
& data/scripts/fetch_datasets.ps1
cmake --build build --target test_local_datasets test_real_data_evaluation
ctest -R "test_local_datasets|test_real_data_evaluation" -V
```

See [docs/REAL_DATA_EVALUATION.md](docs/REAL_DATA_EVALUATION.md) for measured PAI, quality scores, and per-dataset results.

```bash
# Full suite (495 tests, ~40s parallel)
cd build && ctest --output-on-failure -j4
```

Dataset details: [`data/README.md`](data/README.md)

---

## Building & Running

### Prerequisites

| Requirement | Version |
|-------------|---------|
| Qt | 6.4+ (Core, Widgets, Network, Charts, Sql, Test) |
| CMake | 3.27+ |
| Compiler | C++23 (GCC 13+, MSVC 2022, Clang 16+) |

### Windows — primary packaging path

**Primary build-and-package command:** `deploy_portable.ps1` (MinGW or MSVC; no Windows SDK required for MinGW).

```powershell
cd sentinel
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build build --target sentinel
& packaging/windows/deploy_portable.ps1 -BuildDir build
packaging/pkg/bin/sentinel.exe   # smoke-test staged tree
```

Artifacts land in `dist/` (`SENTINEL-1.0.0-win64-portable.zip`, `SENTINEL-1.0.0-win64-setup.exe` when NSIS is installed).

### Windows (MSVC — requires Windows 11 SDK)

MSVC builds need the **Windows 11 SDK** (`stddef.h`, UCRT). Install via `& packaging/windows/install_windows_sdk.ps1` or Visual Studio Installer → *Desktop development with C++* → Windows 11 SDK.

Requires Qt 6 with the **MSVC 2022 64-bit** kit (e.g. `C:/Qt/6.11.0/msvc2022_64`). Open a **Developer Command Prompt for VS 2022** so `cl.exe` is on `PATH`.

```powershell
cd sentinel
& packaging/windows/build_release.ps1
# Or: & packaging/windows/build_msvc.bat
```

If MSVC is unavailable, use MinGW + `deploy_portable.ps1` instead (see Windows row in Releases table below).

CI on GitHub Actions uses **MinGW + Ninja** (Qt 6.6.0 via aqtinstall) on `windows-latest` and **clang** on `ubuntu-latest` — see `.github/workflows/ci.yml`.

### Linux

```bash
cd sentinel
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/usr
cmake --build build
./build/sentinel
```

### Headless UI tests

```bash
set QT_QPA_PLATFORM=offscreen
ctest -R test_ui -j4
```

---

## Releases & Installation

SENTINEL **v1.0.0** ships as native packages with sample datasets and documentation bundled.

| Platform | Primary build command | Artifacts (`dist/`) |
|----------|----------------------|---------------------|
| **Windows** | `& packaging/windows/deploy_portable.ps1 -BuildDir build` | `SENTINEL-1.0.0-win64-portable.zip`, `SENTINEL-1.0.0-win64-setup.exe` |
| **Windows (MSVC + SDK)** | `& packaging/windows/build_release.ps1` | `SENTINEL-1.0.0-win64-portable.zip`; `deploy_portable.ps1` also produces setup.exe |
| **Linux** | `QT_PREFIX=/usr ./packaging/linux/build_release.sh` | `SENTINEL-1.0.0-linux-x86_64.tar.gz`, optional `sentinel_1.0.0_amd64.deb` |

**Staged smoke-test tree:** `packaging/pkg/bin/sentinel.exe` — must include `Qt6Core.dll` and `platforms/qwindows.dll`. Do **not** distribute CPack `SENTINEL-*-win64.exe` (stub without Qt DLLs).

**Windows SDK missing?** Use MinGW + `deploy_portable.ps1`, or run `& packaging/windows/install_windows_sdk.ps1` before MSVC release builds.

**Analyst quick start:** **[docs/ANALYST_GUIDE.md](docs/ANALYST_GUIDE.md)** — portable ZIP or setup.exe, import sample CSVs, review leads, export reports.

End-user guide (portable vs setup.exe): **[docs/RELEASE.md](docs/RELEASE.md)**

Remaining work (Phases 3–5): **[REMAINING.md](REMAINING.md)** · Full roadmap: **[docs/ROADMAP.md](docs/ROADMAP.md)**

---

## Design Principles

1. **Auditability first** — every prediction and lead carries a provenance chain.
2. **Uncertainty is output** — credible intervals, calibration, and ensemble decomposition, not point estimates alone.
3. **No black boxes** — classical algorithms in readable C++; optional ML only behind explicit flags.
4. **Fairness by design** — bias metrics and configurable thresholds before alerts propagate.
5. **Reproducibility** — deterministic pipelines given fixed data and configuration.

---

*SENTINEL — transparent crime analytics for investigators, researchers, and accountable deployment.*
