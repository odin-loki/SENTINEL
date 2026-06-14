# SENTINEL - Testing Guide

## Overview

SENTINEL has **495 test targets** achieving comprehensive coverage across every pipeline layer. Tests use the Qt Test framework and run via CTest.

### Real-world data tests

```powershell
& scripts/run_real_data_tests.ps1
```

See [REAL_DATA_EVALUATION.md](REAL_DATA_EVALUATION.md) for measured results on UK, US, co-offending, and weather datasets.

---

## Running Tests

```bash
# Build first
cd sentinel
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build build

# Run all tests
cd build
ctest --output-on-failure -j4

# Run with verbose output
ctest -V

# Run only tests matching a pattern
ctest -R "poisson|hawkes" --output-on-failure

# List all test names
ctest -N
```

## Release builds

```powershell
& packaging/windows/build_release.ps1
```

See [RELEASE.md](RELEASE.md) for Linux packaging and end-user install steps.

---

### Unit Tests

**Core**
- `test_app_config` — QSettings round-trip, field defaults, boundary values
- `test_appconfig_edge_cases` — missing fields, extreme parameter values
- `test_crime_event_serialization` — JSON serialise/deserialise round-trip
- `test_database_deep` — CRUD, WAL mode, concurrent read
- `test_database_migration` — schema version detection, upgrade path
- `test_database_migration_deep` — integrity after migration
- `test_database_stress` — 10 000 event insertions under concurrent write

**Ingest**
- `test_csv_importer` — column auto-detection, row parsing
- `test_csv_importer_deep` — real Chicago PD data parsing
- `test_csv_importer_edge` — malformed rows, missing fields, empty file
- `test_csv_real_data` — known-good CSV fixtures
- `test_data_quality_deep` — completeness scoring, source reliability
- `test_data_quality_boundary` — empty input, near-threshold pass rate
- `test_data_source_interface` — abstract base signals, health check contract
- `test_uk_police_source_deep` — JSON parsing, outcome mapping, source ID
- `test_weather_source_advanced` — discomfort index, caching, default values
- `test_weather_source_logic` — API URL construction, WeatherData fields

**NLP**
- `test_core_nlp` — MOExtractor + CrimeClassifier end-to-end
- `test_mo_extractor_deep` — entry methods, weapons, items, time of day
- `test_mo_extractor_complex` — ambiguous MO strings, canonicalisation
- `test_crime_classifier_deep` — 11 crime types, severity ranking
- `test_crime_classifier_multilabel` — multi-type, ambiguous, sentiment, threat signal
- `test_nlp_pipeline` — MOExtractor — CrimeClassifier pipeline
- `test_nlp_adversarial` — adversarial NLP inputs

**Models — Poisson / Hawkes / Series**
- `test_poisson_baseline_deep` — PMF, PPF, CIs, zero-count zones
- `test_poisson_statistical` — statistical correctness: PMF, NegBin, overdispersion
- `test_poisson_overdispersion` — variance > mean switching
- `test_hawkes_advanced` — convergence, near-repeat decay
- `test_hawkes_convergence` — MLE parameter convergence
- `test_hawkes_temporal_decay` — intensity decay over time
- `test_series_detector` — basic DBSCAN detection
- `test_series_detector_deep` — linkage probability calibration
- `test_series_detector_linkage` — Haversine, Jaccard, near-repeat params
- `test_series_linkage_calibration` — confusion matrix on known series
- `test_near_repeat_victimisation` — Knox ratio, decay kernels, alert scoring
- `series_near_repeat` — SeriesDetector + NearRepeatVictimisation integration

**Models — KDE / GP / Bayesian / Ensemble**
- `test_kde_hotspot` — grid computation, normalization
- `test_kde_hotspot_deep` — PAI, Silverman bandwidth, topK
- `test_kde_hotspot_advanced` — surface normalization, bandwidth effects, edge cases
- `test_gp_regression` — fit, predict, uncertainty
- `test_gp_regression_full` — log likelihood, kernel parameters
- `test_gp_regression_grid` — grid predictions, posterior covariance
- `test_bayesian_hierarchical` — posterior, shrinkage, CI
- `test_bayesian_posterior` — multi-zone update, partial pooling
- `test_ensemble_predictor_deep` — weight combinations, calibration
- `test_ensemble_calibration` — post-calibration probability range, ECE
- `test_ensemble_weights` — setWeights effects, dominantModel
- `test_risk_forecaster_deep` — multi-zone, rolling window
- `test_risk_forecaster_zones` — alert levels, forecast horizon
- `test_temporal_features_deep` — sin/cos encoding, lunar, payday
- `test_temporal_features_edge` — edge cases, seasonal patterns

**Inference Engine**
- `test_geographic_profiler_deep` — Rossmo surface, search area estimation
- `test_geographic_profiler_multisite` — multi-site anchors, peak location
- `test_mo_analyser_deep` — TF-IDF, cosine similarity, topK
- `test_evidence_scorer_deep` — Bayesian LR, posterior probability
- `test_anomaly_detector_deep` — z-score, isolation, combined score
- `test_anomaly_edge_cases` — single event, extreme values, empty input
- `test_cooffending_deep` — graph construction, PageRank
- `test_cooffending_pagerank` — betweenness, community detection, large network
- `test_hint_engine_deep` — lead generation, quality scoring
- `test_hint_engine_advanced` — series/MO/geo/anomaly leads, contradiction
- `test_lead_report_generator` — Markdown/HTML, provenance, saveToFile

**Benchmarking**
- `test_benchmark_deep` — PAI, PEI, SER, AUC
- `test_benchmark_metrics_edge` — edge cases: empty, all-positive, identical
- `test_calibration_ece` — ECE calculation
- `test_calibration_analyser_deep` — ECE, MCE, ACE, isotonic, reliability diagram
- `test_bias_auditor_deep` — disparate impact, equal opportunity difference

**Audit / Logger / Export**
- `test_provenance_log_deep` — chain recording, multi-event
- `test_sentinel_logger_deep` — ring buffer, category filter
- `test_sentinel_logger_filter` — level filter, clear, newEntry signal
- `test_data_exporter_formats` — Markdown, JSON, CSV, HTML output
- `test_export_roundtrip` — serialise — parse — compare

### Integration Tests

- `test_full_pipeline` — Poisson + Hawkes + Series + KDE end-to-end
- `test_pipeline_integration_deep` — 500 events through full inference stack
- `test_pipeline_e2e_stress` — 1000 events, all models, all UI widgets
- `test_e2e_integration` — CSV — database — model — leads — export
- `test_local_datasets` — import all `data/` files, co-offending, weather, mini pipeline (~6 min)
- `test_real_data_evaluation` — quantitative metrics: quality pass rates, KDE PAI holdout, series (~8 min)
- `test_network_ensemble` — Hawkes + Series + network inference
- `test_gp_ensemble_integration` — GP + EnsemblePredictor integration
- `test_kde_geo_integration` — KDE hotspots — GeographicProfiler

### UI Tests (headless, offscreen Qt platform)

- `test_dashboard_widget` — stats cards, signal binding, model data
- `test_map_widget` — construction, resize, risk overlay
- `test_events_table_widget` — model rows, sorting, filtering
- `test_debug_console_widget` — append, clear, level filter
- `test_audit_log_widget` — provenance chain display
- `test_settings_widget_headless` — save/load, child widget existence
- `test_analytics_widget_data` — Qt Charts data binding
- `test_temporal_heatmap_data` — heatmap cell values
- `test_ui_automation` — programmatic button clicks, signal verification

### Performance / Stress Tests

- `test_performance` — throughput benchmark for all models
- `test_database_stress` — 10K concurrent inserts
- `test_network_stress` — 100 simultaneous async fetches
- `test_pipeline_stress` — 5000-event pipeline throughput
- `test_all_models_perf` — comparative model latency report

---

## Deep Audit Iteration 30 (12 new targets)

| Target | Focus |
|---|---|
| `mo_analyser_deep8` | Resolved outcome, score ordering, shared features, unrelated query |
| `bayesian_hierarchical_deep8` | allPosteriors sort, shrinkage, predictive prob, global hyperparams |
| `calibration_analyser_deep7` | logLoss, brierScore perfect, reliability diagram, nBins |
| `benchmark_metrics_deep8` | PEI bounds, SER, brier comparison, fullReport, hint NDCG |
| `data_exporter_deep8` | leads JSON, events CSV, saveText/saveJson roundtrip, forecasts JSON |
| `appconfig_deep7` | seriesMinEvents, alert ordering, gpSigma2 clamp, maxLeadCount |
| `sentinel_logger_deep7` | recent tail order, category filter, critical level, ring cap |
| `database_deep7` | location_raw search, crimeTypeCounts, insertLead, hourly slots |
| `bias_auditor_deep5` | feedbackLoopCheck, equalizedOddsDiff, formatReports, FPR |
| `test_main_window_deep7` | Window title, File menu, status bar, central widget, close |
| `test_dashboard_widget_deep7` | Multi crime types, risk table, double refresh, event count |
| `test_events_table_deep7` | Crime combo, Filter button, rows after insert, type filter |

---

## Deep Audit Iteration 29 (12 new targets)

| Target | Focus |
|---|---|
| `hint_engine_deep10` | Series linkage, anomaly/geo leads, data-quality dampening |
| `geographic_profiler_deep10` | Three-site centroid, gridN surface, Rossmo f/g params |
| `anomaly_detector_deep10` | Signal reasons, combined score, contamination sensitivity |
| `evidence_scorer_deep11` | Bayes factor, QVector API, posterior bounds, exculpatory LR |
| `series_detector_deep9` | Haversine zero distance, linkProbability, nearRepeat params |
| `cooffending_deep9` | PageRank, betweenness, connectionType, community IDs |
| `csv_importer_deep9` | primary_type alias, location column, skip empty rows, UTF-8 |
| `data_quality_deep7` | Quarantine sparse events, default reliability, precision labels |
| `lead_report_deep9` | formatLead rank, plain/markdown body, HTML headline |
| `crime_classifier_deep7` | corpusSize, vehicle theft, sentiment, severity ordering |
| `test_settings_widget_deep6` | DB path field, hawkes range, radius spin (decimals=1) |
| `test_audit_log_deep6` | Action/stage columns, refresh reload, filter narrowing |

---

## Deep Audit Iteration 28 (12 new targets)

| Target | Focus |
|---|---|
| `poisson_baseline_deep10` | totalEvents, probAtLeastOne bounds, lambda non-negative, crime buckets |
| `hawkes_process_deep10` | empty fit rejection, intensity non-negative, parameter accessors |
| `near_repeat_deep9` | bandwidthFor crime types, analyse distances, Knox cluster statistic |
| `gp_regression_deep9` | kernel params reset fit, predict variance, empty input |
| `kde_hotspot_deep8` | grid dimensions, centroid peak, paiAreaFraction, Silverman fallback |
| `risk_forecaster_deep9` | multi-zone forecast, custom thresholds, horizon day count |
| `ensemble_predictor_deep9` | riskGrid shape, brierScore/ece static metrics, fit flag |
| `mo_extractor_deep6` | forced_entry regex, vehicle target, canonical MO string |
| `temporal_features_deep7` | weekOfMonth range, isDark/isNight, day-of-year encoding |
| `provenance_deep7` | recent() ordering, time-range filter, filterByModel, CSV header |
| `test_map_widget_deep6` | construct, setEvents empty, refresh idempotent, zoom controls |
| `test_leads_widget_deep6` | construct, empty leads, column headers, filter combo |

---

## Deep Audit Iteration 27 (12 new targets)

| Target | Focus |
|---|---|
| `hint_engine_deep9` | MO lead cap (3), headline dedup, kMaxLeads (50), resolved MO outcome |
| `geographic_profiler_deep9` | Single-site profile, gridN resolution, bufferKm, peak bounds |
| `anomaly_detector_deep9` | Outlier flagging, LOF/isolation bounds, auto-fit on detect |
| `evidence_scorer_deep10` | Contributions list, available types, LR lookup, exculpatory evidence |
| `bias_auditor_deep4` | groupStats counts, equalOpportunity, maxDisparateImpact, formatReports |
| `cooffending_deep8` | topK limit, nodes export, degree counts, direct participant leads |
| `csv_importer_deep8` | Quoted commas, case_number alias, header-only, lat/lon parse |
| `data_quality_deep6` | passRate, batch order, custom reliability, composite bounds |
| `series_detector_deep8` | moJaccard empty/identical, dominant type, temporal span |
| `test_events_table_deep6` | Header labels, refresh idempotent, date filter, search box |
| `test_dashboard_widget_deep6` | Empty DB cards, refresh after insert, risk table columns |
| `test_analytics_widget_deep6` | Tab count, period combo, refresh stability, summary label |

---

## Deep Audit Iteration 26 (12 new targets)

| Target | Focus |
|---|---|
| `risk_forecaster_deep8` | peakDayIndex, alertLabel tiers, zoneCount, zero-risk unfitted forecast |
| `ensemble_predictor_deep8` | CI90/CI95 bounds, weight fractions, calibrate flag, expectedCount |
| `mo_analyser_deep7` | topK cap, minSimilarity filter, identical MO score, caseCount |
| `lead_report_deep8` | high-confidence count, HTML export, provenance arrow, save roundtrip |
| `data_exporter_deep7` | provenance JSON, forecast CSV header, benchmark markdown, HTML title |
| `appconfig_deep6` | hawkes clamp, ensemble normalisation, map zoom, forecast horizon |
| `benchmark_metrics_deep7` | PAI ordering, AUC perfect classifier, hintQuality MRR, reportText |
| `bayesian_hierarchical_deep7` | posterior mean ordering, credible intervals, predictMean, type filter |
| `pipeline_integration_deep4` | LeadReport chain, provenance export, anomaly bounds, Poisson fit |
| `test_main_window_deep6` | central stack, toolbar, minimum size, close lifecycle |
| `test_debug_console_deep6` | append/clear, category filter, level combo |
| `test_audit_log_deep5` | table rows, count label, event-id filter, clear button |

---

## Deep Audit Iteration 25 (12 new targets)

| Target | Focus |
|---|---|
| `geographic_profiler_deep8` | Multi-site centroid peak, search-area 50/80%, surface normalisation, far-outlier stability |
| `hint_engine_deep8` | Network/anomaly leads, geo/series contradictions, provenance chain, rank bounds |
| `evidence_scorer_deep9` | DNA/fingerprint keys, corroboration weighting, empty-evidence guard |
| `anomaly_detector_deep8` | LOF/isolation fusion, z-score thresholds, empty-input guard |
| `calibration_analyser_deep6` | ECE bins, isotonic PAVA, reliability diagram, log-loss |
| `temporal_features_deep6` | Hour/day-of-week encoding, seasonality flags, cyclic features |
| `crime_classifier_deep6` | Keyword rules, top-N ranking, unknown-type fallback |
| `sentinel_logger_deep6` | Category filtering, install hook, lcDatabase routing |
| `provenance_deep6` | Chain append, stage filter, JSON/HTML export |
| `database_deep6` | Batch insert, lat/lon round-trip, audit log, keyword search |
| `test_leads_widget_deep5` | Lead list population, detail panel, confidence spin |
| `test_settings_widget_deep5` | Hawkes/radius/theme controls, save button, settingsSaved signal |

---

## Deep Audit Iteration 24 (12 new targets)

| Target | Focus |
|---|---|
| `risk_forecaster_deep7` | Alert tier boundaries, weeklyRisk mean, horizon clamp, crimeType filter |
| `ensemble_predictor_deep7` | Isotonic calibration, weight normalisation, dominantModel, ECE/Brier |
| `poisson_baseline_deep9` | NB overdispersion, hour-23 bucket, CI ordering, PMF sum |
| `series_detector_deep7` | DBSCAN minPts, linkProbability bounds, moJaccard, temporal decay |
| `kde_hotspot_deep7` | Silverman bandwidth, findHotspots ranking, PAI fraction |
| `hawkes_process_deep9` | Branching ratio, intensity decay, risk surface, sorted history |
| `near_repeat_deep8` | Knox statistic, temporal decay window, colocated alerts |
| `gp_regression_deep8` | Posterior variance, log-ML guard, lengthscale clamp |
| `mo_extractor_deep5` | Entry method, firearm, solo/group, time of day, items taken |
| `test_map_widget_deep5` | setEvents, zoom clamp, clearOverlays, risk surface |
| `test_analytics_widget_deep5` | Chart tabs, refresh, period combo, benchmark/calibration |
| `test_temporal_heatmap_deep5` | 7x24 grid, peak cell, monthly projection, clear |

---

## Deep Audit Iteration 23 (12 new targets)

| Target | Focus |
|---|---|
| `bias_auditor_deep3` | Feedback loop detection, formatReports, trendSlope, custom DI threshold |
| `bayesian_hierarchical_deep6` | Credible intervals, shrinkage vs MLE, unknown-zone predictMean |
| `mo_analyser_deep6` | TF-IDF cosine similarity, topK, resolved-case fit/match |
| `lead_report_deep7` | Markdown/HTML report, escaping, saveToFile, rank ordering |
| `uk_police_source_deep5` | JSON outcome mapping, date range, lat/lon extraction |
| `weather_source_deep5` | Discomfort index bounds, cache hit, URL construction |
| `data_quality_deep5` | Completeness decay, quarantine boundary, spatial/temporal precision |
| `csv_importer_deep7` | Chicago PD detection, malformed rows, UTF-8 BOM, date formats |
| `cooffending_deep7` | Star/chain PageRank, betweenness bridge, community detection |
| `pipeline_integration_deep3` | 100-event Poisson+Hawkes+Series+HintEngine+anomaly+JSON |
| `test_dashboard_widget_deep5` | Stat cards, risk alerts table, refresh with seeded events |
| `test_events_table_deep5` | Row count, crime filter, date sort, quality badge column |

---

## Deep Audit Iteration 22 (12 new targets)

| Target | Focus |
|---|---|
| `gp_regression_deep7` | Log-ML unfitted guard, kernel clamp, variance non-negative |
| `anomaly_detector_deep7` | Contamination threshold, isolation/LOF, auto-fit |
| `evidence_scorer_deep8` | Prior edges, LR overflow guard, bayesFactor |
| `geographic_profiler_deep7` | Rossmo buffer, search areas, grid normalisation |
| `hint_engine_deep7` | Geo cap, series filter, rankScore rerank |
| `near_repeat_deep7` | Knox ratio, decay windows, bandwidth guards |
| `data_exporter_deep6` | HTML escape, JSON/CSV roundtrip, forecast labels |
| `calibration_analyser_deep5` | ECE bins, isotonic PAVA, log-loss extremes |
| `temporal_features_deep5` | Cyclical encoding, lunar phase, sun altitude |
| `crime_classifier_deep5` | Severity ranking, threat/sentiment, multilabel |
| `test_main_window_deep5` | Headless shell, menus, status bar |
| `test_debug_console_deep3` | Level filter, trim, search, timestamps |

---

## Deep Audit Iteration 21 (12 new targets)

| Target | Focus |
|---|---|
| `risk_forecaster_deep6` | Stale-zone forecast enumeration, horizon clamp, alert tiers |
| `ensemble_predictor_deep6` | ECE bin clamping, calibration no-op, weight fallback |
| `poisson_baseline_deep8` | Case-normalised buckets, NB gate, hour-23 bin |
| `series_detector_deep6` | DBSCAN linkage, near-repeat params, cluster isolation |
| `kde_hotspot_deep6` | Bandwidth guard, surface peaks, grid bounds |
| `hawkes_process_deep8` | Sorted history, intensity cutoff, branching ratio |
| `sentinel_logger_deep5` | Level filter, ring buffer, JSON fields |
| `provenance_deep5` | Chain integrity, hash stability, export fields |
| `benchmark_metrics_deep4` | PEI/PAI guards, SER, hint NDCG |
| `test_settings_widget_deep4` | Auto-save wiring, theme toggle, validation |
| `test_audit_log_deep4` | Filter narrowness, export, row selection |
| `test_leads_widget_deep4` | Evidence key mapping, series tab refresh |

---

## Writing New Tests

All test executables are registered with `add_test` in `tests/CMakeLists.txt`:

```cmake
qt_add_executable(test_my_feature tests/test_my_feature.cpp)
target_link_libraries(test_my_feature PRIVATE sentinel_core Qt6::Test)
add_test(NAME test_my_feature COMMAND test_my_feature)
```

For UI tests, add the offscreen platform:
```cmake
set_tests_properties(test_my_feature PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

### Example test structure

```cpp
#include <QtTest/QtTest>
#include "models/PoissonBaseline.h"

class TestPoissonBaseline : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { /* one-time setup */ }
    void testProbAtLeastOneRange() {
        PoissonBaseline m;
        m.update({"Zone1"}, {/* events */});
        auto pred = m.predict("Zone1", 1.0);
        QVERIFY(pred.probAtLeastOne >= 0.0);
        QVERIFY(pred.probAtLeastOne <= 1.0);
    }
};

QTEST_MAIN(TestPoissonBaseline)
#include "test_poisson_baseline.moc"
```

---

## Test Coverage Notes

- **In-memory SQLite** (`:memory:`) is used in all database tests — no file system side effects.
- **Qt offscreen platform** (`QT_QPA_PLATFORM=offscreen`) allows UI widget tests without a display server.
- **`QSignalSpy`** is used throughout to verify async signal emissions.
- Statistical tests include both correctness checks (PMF sums to 1, probabilities in [0,1]) and precision checks (within — of analytical values).
