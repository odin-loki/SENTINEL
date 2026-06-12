# SENTINEL â€” Testing Guide

## Overview

SENTINEL has **381 test targets** achieving comprehensive coverage across every pipeline layer. Tests use the Qt Test framework and run via CTest.

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
- `test_app_config` â€” QSettings round-trip, field defaults, boundary values
- `test_appconfig_edge_cases` â€” missing fields, extreme parameter values
- `test_crime_event_serialization` â€” JSON serialise/deserialise round-trip
- `test_database_deep` â€” CRUD, WAL mode, concurrent read
- `test_database_migration` â€” schema version detection, upgrade path
- `test_database_migration_deep` â€” integrity after migration
- `test_database_stress` â€” 10 000 event insertions under concurrent write

**Ingest**
- `test_csv_importer` â€” column auto-detection, row parsing
- `test_csv_importer_deep` â€” real Chicago PD data parsing
- `test_csv_importer_edge` â€” malformed rows, missing fields, empty file
- `test_csv_real_data` â€” known-good CSV fixtures
- `test_data_quality_deep` â€” completeness scoring, source reliability
- `test_data_quality_boundary` â€” empty input, near-threshold pass rate
- `test_data_source_interface` â€” abstract base signals, health check contract
- `test_uk_police_source_deep` â€” JSON parsing, outcome mapping, source ID
- `test_weather_source_advanced` â€” discomfort index, caching, default values
- `test_weather_source_logic` â€” API URL construction, WeatherData fields

**NLP**
- `test_core_nlp` â€” MOExtractor + CrimeClassifier end-to-end
- `test_mo_extractor_deep` â€” entry methods, weapons, items, time of day
- `test_mo_extractor_complex` â€” ambiguous MO strings, canonicalisation
- `test_crime_classifier_deep` â€” 11 crime types, severity ranking
- `test_crime_classifier_multilabel` â€” multi-type, ambiguous, sentiment, threat signal
- `test_nlp_pipeline` â€” MOExtractor â†’ CrimeClassifier pipeline
- `test_nlp_adversarial` â€” adversarial NLP inputs

**Models â€” Poisson / Hawkes / Series**
- `test_poisson_baseline_deep` â€” PMF, PPF, CIs, zero-count zones
- `test_poisson_statistical` â€” statistical correctness: PMF, NegBin, overdispersion
- `test_poisson_overdispersion` â€” variance > mean switching
- `test_hawkes_advanced` â€” convergence, near-repeat decay
- `test_hawkes_convergence` â€” MLE parameter convergence
- `test_hawkes_temporal_decay` â€” intensity decay over time
- `test_series_detector` â€” basic DBSCAN detection
- `test_series_detector_deep` â€” linkage probability calibration
- `test_series_detector_linkage` â€” Haversine, Jaccard, near-repeat params
- `test_series_linkage_calibration` â€” confusion matrix on known series
- `test_near_repeat_victimisation` â€” Knox ratio, decay kernels, alert scoring
- `series_near_repeat` â€” SeriesDetector + NearRepeatVictimisation integration

**Models â€” KDE / GP / Bayesian / Ensemble**
- `test_kde_hotspot` â€” grid computation, normalization
- `test_kde_hotspot_deep` â€” PAI, Silverman bandwidth, topK
- `test_kde_hotspot_advanced` â€” surface normalization, bandwidth effects, edge cases
- `test_gp_regression` â€” fit, predict, uncertainty
- `test_gp_regression_full` â€” log likelihood, kernel parameters
- `test_gp_regression_grid` â€” grid predictions, posterior covariance
- `test_bayesian_hierarchical` â€” posterior, shrinkage, CI
- `test_bayesian_posterior` â€” multi-zone update, partial pooling
- `test_ensemble_predictor_deep` â€” weight combinations, calibration
- `test_ensemble_calibration` â€” post-calibration probability range, ECE
- `test_ensemble_weights` â€” setWeights effects, dominantModel
- `test_risk_forecaster_deep` â€” multi-zone, rolling window
- `test_risk_forecaster_zones` â€” alert levels, forecast horizon
- `test_temporal_features_deep` â€” sin/cos encoding, lunar, payday
- `test_temporal_features_edge` â€” edge cases, seasonal patterns

**Inference Engine**
- `test_geographic_profiler_deep` â€” Rossmo surface, search area estimation
- `test_geographic_profiler_multisite` â€” multi-site anchors, peak location
- `test_mo_analyser_deep` â€” TF-IDF, cosine similarity, topK
- `test_evidence_scorer_deep` â€” Bayesian LR, posterior probability
- `test_anomaly_detector_deep` â€” z-score, isolation, combined score
- `test_anomaly_edge_cases` â€” single event, extreme values, empty input
- `test_cooffending_deep` â€” graph construction, PageRank
- `test_cooffending_pagerank` â€” betweenness, community detection, large network
- `test_hint_engine_deep` â€” lead generation, quality scoring
- `test_hint_engine_advanced` â€” series/MO/geo/anomaly leads, contradiction
- `test_lead_report_generator` â€” Markdown/HTML, provenance, saveToFile

**Benchmarking**
- `test_benchmark_deep` â€” PAI, PEI, SER, AUC
- `test_benchmark_metrics_edge` â€” edge cases: empty, all-positive, identical
- `test_calibration_ece` â€” ECE calculation
- `test_calibration_analyser_deep` â€” ECE, MCE, ACE, isotonic, reliability diagram
- `test_bias_auditor_deep` â€” disparate impact, equal opportunity difference

**Audit / Logger / Export**
- `test_provenance_log_deep` â€” chain recording, multi-event
- `test_sentinel_logger_deep` â€” ring buffer, category filter
- `test_sentinel_logger_filter` â€” level filter, clear, newEntry signal
- `test_data_exporter_formats` â€” Markdown, JSON, CSV, HTML output
- `test_export_roundtrip` â€” serialise â†’ parse â†’ compare

### Integration Tests

- `test_full_pipeline` â€” Poisson + Hawkes + Series + KDE end-to-end
- `test_pipeline_integration_deep` â€” 500 events through full inference stack
- `test_pipeline_e2e_stress` â€” 1000 events, all models, all UI widgets
- `test_e2e_integration` â€” CSV â†’ database â†’ model â†’ leads â†’ export
- `test_network_ensemble` â€” Hawkes + Series + network inference
- `test_gp_ensemble_integration` â€” GP + EnsemblePredictor integration
- `test_kde_geo_integration` â€” KDE hotspots â†’ GeographicProfiler

### UI Tests (headless, offscreen Qt platform)

- `test_dashboard_widget` â€” stats cards, signal binding, model data
- `test_map_widget` â€” construction, resize, risk overlay
- `test_events_table_widget` â€” model rows, sorting, filtering
- `test_debug_console_widget` â€” append, clear, level filter
- `test_audit_log_widget` â€” provenance chain display
- `test_settings_widget_headless` â€” save/load, child widget existence
- `test_analytics_widget_data` â€” Qt Charts data binding
- `test_temporal_heatmap_data` â€” heatmap cell values
- `test_ui_automation` â€” programmatic button clicks, signal verification

### Performance / Stress Tests

- `test_performance` â€” throughput benchmark for all models
- `test_database_stress` â€” 10K concurrent inserts
- `test_network_stress` â€” 100 simultaneous async fetches
- `test_pipeline_stress` â€” 5000-event pipeline throughput
- `test_all_models_perf` â€” comparative model latency report

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

- **In-memory SQLite** (`:memory:`) is used in all database tests â€” no file system side effects.
- **Qt offscreen platform** (`QT_QPA_PLATFORM=offscreen`) allows UI widget tests without a display server.
- **`QSignalSpy`** is used throughout to verify async signal emissions.
- Statistical tests include both correctness checks (PMF sums to 1, probabilities in [0,1]) and precision checks (within Îµ of analytical values).

