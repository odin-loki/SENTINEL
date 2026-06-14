# SENTINEL — Roadmap



This document tracks **where SENTINEL is today**, **near-term delivery**, and **long-term vision**. It complements the developer README with planning detail for contributors and stakeholders.



**Current version:** 1.0.0  

**Last updated:** June 2026  

**Test suite:** 495 automated tests (unit, integration, stress, headless UI, real-data evaluation)



---



## Current state (v1.0.0)



### Shipped and validated



| Area | Status | Evidence |

|------|--------|----------|

| End-to-end desktop pipeline | Done | Ingest → NLP → models → inference → UI → SQLite provenance |

| UK + US CSV importers | Done | UK Met 57k rows, Cincinnati/SFPD 15k samples |

| Statistical models | Done | Poisson, Hawkes, KDE, series, ensemble, GP, Bayesian, near-repeat |

| Investigative inference | Done | Geo profiling, MO similarity, co-offending, leads, reports |

| Benchmarking & fairness | Done | PAI, AUC, ECE, bias metrics implemented |

| Real-data evaluation | Done | [REAL_DATA_EVALUATION.md](REAL_DATA_EVALUATION.md) — KDE PAI 6.93 @ 5%, AUC 0.993 |

| Native packaging | Done | Windows portable ZIP + NSIS setup; Linux TGZ + DEB — [RELEASE.md](RELEASE.md) |

| Automated testing | Done | `test_local_datasets`, `test_real_data_evaluation`, 495 total |

| Nine-page analyst UI | Done | Dashboard, Events, Analytics, **Cases**, **Network**, Leads, Audit, Settings, Debug |



### Known limitations



- NLP is classical (keywords / TF-IDF), not transformer-based.

- Single-user desktop; no multi-tenant server. Read-only local REST API shipped (`LocalApiServer`); gRPC and auth remain Phase 3.

- Map is custom QPainter overlay, not full GIS.

- Cases page CGT heatmap and Network **Load from DB** require `meta.person_id` on imported events; CSV-only datasets still use Chicago sample fallback.

- Co-offending at scale needs agency-provided anonymised person IDs.

- Packages are unsigned; no auto-update channel yet.

- Weather enrichment only applies to London-bbox imports with bundled cache dates.



### Readiness matrix



| Use case | Ready? |

|----------|--------|

| Research / demo / analyst workstation | **Yes** |

| Batch UK/US CSV + UK Police API ingest | **Yes** |

| Lead generation with full provenance export | **Yes** |

| Reproducible benchmark on local data splits | **Yes** |

| Per-case investigation workspace (full) | **Yes** — merge/split overrides, Rossmo CGT heatmap, lead history, case export |

| Interactive co-offending network from live DB | **Yes** — Load from DB, zoom/pan, node click, CSV fallback |

| Multi-agency operational deployment | **No** |

| Real-time streaming ingestion | **No** |



---



## Vision (18–24 months)



SENTINEL becomes the reference implementation for **transparent crime analytics**: agencies and researchers import their own data, reproduce every score, export court-ready provenance reports, and benchmark model fairness **before** operational use — without proprietary black-box APIs.



---



## Phase 1 — Consolidate & operationalise (0–3 months) ✅ Complete



**Goal:** Reliable daily analyst tool on a fresh install.



| Initiative | Status (Jun 2026) | Notes |

|------------|-------------------|-------|

| Native installers | **Done** | Portable ZIP + NSIS setup (Windows); TGZ + DEB (Linux). Code signing remains; push `v*` tag to trigger `.github/workflows/release.yml`. |

| Real-data test harness | **Done** | `scripts/run_real_data_tests.ps1` |

| Evaluation report | **Done** | [REAL_DATA_EVALUATION.md](REAL_DATA_EVALUATION.md) |

| Data workspace UI | **Done** | CSV / UK API / sample import → quality summary → Analytics navigation |

| Weather enrichment on ingest | **Done** | `IngestEnricher` scores + attaches bundled weather cache |

| Analyst guide (non-developer) | **Done** | [ANALYST_GUIDE.md](ANALYST_GUIDE.md) — install → import → leads → export |



**Exit criteria met:** Analyst reaches first ranked lead in <15 minutes from portable ZIP install.



---



## Phase 2 — Investigation depth (3–6 months) ✅ Complete



**Goal:** Case linkage and network analysis as core workflow.



| Initiative | Status (Jun 2026) | Shipped | Remaining work |

|------------|-------------------|---------|----------------|

| Case workspace | **Done** | `CaseWorkspaceWidget`: case filter; events table; series count + **merge/split override UI**; lead history; geo summary; **Rossmo CGT heatmap** (`GeoProfileHeatmapWidget`); **Export Case Report** | — |

| Series refinement | **Done** | Per-crime-type DBSCAN eps in `AppConfig` / `SeriesDetector`; **analyst merge/split overrides** on Cases page | — |

| Network visualisation | **Done** | `CoOffendingGraphWidget`: zoom/pan; node click; Load CSV; **Load from DB** (SQLite `meta.person_id`); Chicago CSV fallback | — |

| Narrative upgrade (optional) | Not started | — | ONNX sentence encoder behind feature flag |

| Calibration dashboard | **Done** | `CalibrationDashboardWidget` on Analytics **Calibration** tab — live reliability diagram, ECE/MCE/Brier from month holdout on imported events | — |



**Exit criteria:** Single-case export with complete provenance chain (**met**); CGT heatmap on Cases page (**met**); analyst series overrides (**met**); SQLite co-offending graph (**met**).



---



## Phase 3 — Scale & collaboration (6–12 months)



**Goal:** Larger jurisdictions and small teams without losing auditability.



| Initiative | Actions |

|------------|---------|

| Performance | Parallel ingest, indexed queries, incremental model refit (500k events <5 min) |

| Multi-jurisdiction | Zone registry, CRS normalisation, per-force config profiles |

| Read-only local API | REST starter shipped (`LocalApiServer`); remaining: gRPC + auth |

| Audit compliance | Signed immutable provenance export, retention policies |

| GIS integration | Optional MapLibre / Qt Location tiles; GeoJSON boundaries |



**Exit criteria:** UK + US cities in one project without column hacks; optional tile map for hotspots.



---



## Phase 4 — Research & policy (12–18 months)



**Goal:** Evidence-based alternative to opaque predictive policing tools.



| Initiative | Actions |

|------------|---------|

| Fairness-by-default | Bias report required before alerts go live |

| Open benchmarks | Public-data leaderboard with reproducible scripts |

| Academic partnerships | Methods paper; shared evaluation harness |

| Policy toolkit | Per-deployment “model card”: data, limits, failure modes |



**Exit criteria:** Third parties reproduce PAI/ECE numbers; governance review uses exported model cards.



---



## Phase 5 — Optional productisation (18–24 months)



Only if Phases 1–3 validate real user demand.



| Track | Guardrails |

|-------|------------|

| Agency desktop licence | Signed builds, support, custom ingest adapters; data stays on device |

| Training mode | Synthetic scenarios for academy use; clearly labelled simulated data |

| Managed analytics (researchers) | Tenant isolation; full provenance export; no model lock-in |



---



## Strategic priorities (ordered)



1. **Trust** — provenance, calibration, fairness before more models  

2. **Usability** — import, case review, export in the UI  

3. **Evidence** — benchmark harness on real local splits every release  

4. **Scale** — performance and multi-city normalisation  

5. **Innovation** — optional ML only where classical methods plateau  



---



## Risks & mitigations



| Risk | Mitigation |

|------|------------|

| Biased alerts | `BiasAuditor` gate; human-in-the-loop leads |

| Poor source data | `DataQualityScorer` quarantine; visible quality badges |

| Co-offending data scarcity | Agency CSV with anonymised IDs; never assume public PII |

| Model overconfidence | Ensemble uncertainty + calibration + “low data” warnings |

| Opaque AI scope creep | Classical path default; embeddings opt-in with provenance |



---



## Success metrics



| Metric | Target (12 months) |

|--------|-------------------|

| Automated tests | ≥495; regression per new ingest format |

| Real-data coverage | ≥3 cities + UK + weather on every release |

| Benchmark reproducibility | Fixed PAI/ECE report from `data/` |

| Time-to-first-lead | <15 min from install |

| Provenance coverage | 100% of leads → source row + model + parameters |



---



## How to contribute to the roadmap



1. Pick an initiative with **Partial** or **Not started** status in Phase 3–5 (see [REMAINING.md](../REMAINING.md)).

2. Add or extend tests before changing pipeline behaviour.

3. Run `scripts/run_real_data_tests.ps1` (or `ctest -R test_real_data_evaluation`) before opening a PR.

4. Update this file and `REMAINING.md` when an initiative moves to **Done**.



---



*For build and install instructions see [RELEASE.md](RELEASE.md). For architecture see [ARCHITECTURE.md](ARCHITECTURE.md).*

