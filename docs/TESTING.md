# SENTINEL — Testing Guide

## Overview

SENTINEL has **190 test targets** achieving comprehensive coverage across every pipeline layer. Tests use the Qt Test framework and run via CTest.

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

---

## Test Categories

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
- `test_nlp_pipeline` — MOExtractor → CrimeClassifier pipeline
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
- `test_near_repeat_victimisation` — repeat-interval analysis

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
- `test_export_roundtrip` — serialise → parse → compare

### Integration Tests

- `test_full_pipeline` — Poisson + Hawkes + Series + KDE end-to-end
- `test_pipeline_integration_deep` — 500 events through full inference stack
- `test_pipeline_e2e_stress` — 1000 events, all models, all UI widgets
- `test_e2e_integration` — CSV → database → model → leads → export
- `test_network_ensemble` — Hawkes + Series + network inference
- `test_gp_ensemble_integration` — GP + EnsemblePredictor integration
- `test_kde_geo_integration` — KDE hotspots → GeographicProfiler

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
- Statistical tests include both correctness checks (PMF sums to 1, probabilities in [0,1]) and precision checks (within ε of analytical values).
