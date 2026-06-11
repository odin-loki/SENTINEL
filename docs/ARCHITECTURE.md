# SENTINEL — Architecture Reference

## Overview

SENTINEL is structured as a layered pipeline. Each layer has strictly defined responsibilities and communicates via well-typed C++ structs and Qt signals/slots. No layer knows the internals of another.

```
┌──────────────────────────────────────────────────────────────┐
│  INGEST        UKPoliceSource | WeatherSource | CsvImporter  │
│                DataQualityScorer                             │
└──────────────────────────┬───────────────────────────────────┘
                           │ CrimeEvent, WeatherData
┌──────────────────────────▼───────────────────────────────────┐
│  NLP           MOExtractor | CrimeClassifier                 │
│                MOFeatures, NLPResult, CrimeType, Severity    │
└──────────────────────────┬───────────────────────────────────┘
                           │ enriched CrimeEvent
┌──────────────────────────▼───────────────────────────────────┐
│  FEATURE ENG.  TemporalFeatures (sin/cos, lunar, payday)     │
└──────────────────────────┬───────────────────────────────────┘
                           │ TemporalFeatureVector
┌──────────────────────────▼───────────────────────────────────┐
│  MODEL STACK (9 statistical models)                           │
│  PoissonBaseline  HawkesProcess  SeriesDetector              │
│  KDEHotspot       GPRegression   BayesianHierarchical        │
│  RiskForecaster   EnsemblePredictor  NearRepeatVictimisation │
└──────────────────────────┬───────────────────────────────────┘
                           │ predictions + uncertainty
┌──────────────────────────▼───────────────────────────────────┐
│  INFERENCE ENGINE                                             │
│  GeographicProfiler  MOAnalyser      EvidenceScorer          │
│  AnomalyDetector     CoOffending     HintEngine              │
│                      LeadReportGenerator                     │
└──────────────────────────┬───────────────────────────────────┘
                           │ InvestigativeLeads, AnomalySignals
┌──────────────────────────▼───────────────────────────────────┐
│  BENCHMARKING  BenchmarkMetrics | BiasAuditor | CalibrationA.│
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│  PERSISTENCE   Database (SQLite), ProvenanceLog              │
└──────────────────────────┬───────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────┐
│  UI LAYER      MainWindow → 7 child widgets                  │
└──────────────────────────────────────────────────────────────┘
```

---

## Core Data Structures (`src/core/CrimeEvent.h`)

All inter-layer communication uses these shared structs. They are copy-constructible, movable, and serialisable to JSON.

### `CrimeEvent`
Primary data entity — one record from any source.

```
eventId                   QString (primary key)
source, sourceVersion     QString
occurredAt                std::optional<QDateTime>
crimeType, crimeSubtype   QString
lat, lon                  std::optional<double> (WGS-84)
suburb, street, postcode  QString
narrative                 QString
qualityScore              double [0,1]
moFeatures                MOFeatures
nlpResult                 NLPResult
```

### `InvestigativeLead`
Output of HintEngine.

```
rank              int (1 = highest priority)
category          QString (series_linkage, mo_similarity, geographic, anomaly, network_association)
headline          QString
detail            QString
confidence        double [0,1]
confidenceMethod  QString
supportingData    QJsonObject
provenance        std::vector<QString>
contradictions    std::vector<QString>
generatedAt       QDateTime
```

### `GeographicProfile`
Rossmo CGT formula probability surface output.

The CGT formula correctly places the buffer-zone term `B^(g-f)/(2B-d)^g` for `d ≤ B` 
(suppressing crimes near the offender's anchor) and the distance-decay term `1/d^f` 
for `B < d < 4B`.

```
peakLat, peakLon      double — highest-probability anchor point
peakProbability       double
searchArea50pct       double km²
searchArea80pct       double km²
gridLats, gridLons    std::vector<double>
probabilitySurface    std::vector<std::vector<double>> — normalised to sum ≈ 1.0
method                QString — "rossmo_cgt"
```

### `AnomalySignal`
Output of AnomalyDetector.

```
eventId           QString
isolationScore    double
lofScore          double
zScoreTemporal    double
zScoreSpatial     double
combinedScore     double — aggregated anomaly magnitude [0,1]
isAnomaly         bool
signalReasons     std::vector<QString>
```

### `SeriesMatch`
Output of SeriesDetector.

```
seriesId              QString
memberCount           int
linkProbability       double
spatialDistanceM      double
temporalDistanceDays  double
moSimilarity          double
compositeScore        double
method                QString
```

### `NetworkLead`
Output of CoOffendingAnalyser.

```
personId              QString
connectionType        QString — "direct_cooffender" | "second_degree" | "venue_linked"
sharedIncidents       int
centralityScore       double — PageRank / betweenness
communityId           int
riskScore             double
reasoning             QString
```

---

## Module Responsibilities

### `sentinel_core` (static library)
All non-UI code. Linked by both the application executable and all test executables. This allows every model and inference component to be tested without a display.

### `sentinel` (executable)
Links `sentinel_core` + UI sources. `main.cpp` creates `QApplication`, `MainWindow`, and starts the event loop.

### Tests (`tests/`)
Each test binary links `sentinel_core` + `Qt6::Test`. UI tests additionally instantiate `QApplication` with the offscreen platform (`QT_QPA_PLATFORM=offscreen`).

---

## Qt Signal/Slot Contract

Data sources emit:
- `eventFetched(CrimeEvent)` — one event ready
- `fetchComplete(int count)` — batch done
- `fetchError(QString message)` — async failure

`HintEngine` is a pure value-returning service (not a `QObject`):
- `generate(HintEngineInput) → QVector<InvestigativeLead>` — returns ranked leads synchronously

`SentinelLogger` emits:
- `newEntry(LogEntry)` — for live console display

---

## Database Schema (v3)

```sql
CREATE TABLE schema_version (version INTEGER NOT NULL);

CREATE TABLE events (
    event_id          TEXT PRIMARY KEY,
    source            TEXT,
    source_version    TEXT,
    ingested_at       TEXT,
    occurred_at       TEXT,
    crime_type        TEXT,
    crime_subtype     TEXT,
    location_raw      TEXT,
    lat               REAL,
    lon               REAL,
    suburb            TEXT,
    street            TEXT,
    postcode          TEXT,
    narrative         TEXT,
    outcome           TEXT,
    quality_score     REAL
);

CREATE TABLE leads (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id            TEXT,
    rank                INTEGER,
    category            TEXT,
    headline            TEXT,
    detail              TEXT,
    confidence          REAL,
    confidence_method   TEXT,
    supporting_data     TEXT,
    contradictions      TEXT,
    provenance          TEXT,
    generated_at        TEXT
);

CREATE TABLE audit_log (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    ts        TEXT,
    event_id  TEXT,
    category  TEXT,
    message   TEXT
);

-- v3 migration adds:
CREATE TABLE zone_risk_scores (
    zone_id     TEXT PRIMARY KEY,
    risk_score  REAL,
    updated_at  TEXT
);
```

WAL mode enabled (`PRAGMA journal_mode=WAL`) for concurrent read/write.  
In-memory mode (`:memory:`) used in all tests.

---

## AppConfig (`src/core/AppConfig.h`)

Runtime configuration struct with `QSettings` persistence. All model parameters are editable at runtime via `SettingsWidget`.

Key parameter groups:
- **Pipeline**: `hawkesHistoryDays` ∈ [7, 3650], DBSCAN epsilon (km, days, MO weight)
- **Alerts**: Elevated/High/Critical probability thresholds (must be strictly ordered)
- **GP**: σ² ∈ (0, 100], lengthscale ∈ (0, 10], noise σ² ∈ (0, 10]
- **Ensemble**: Poisson/Hawkes weight ratio ∈ [0, 1]
- **UI**: map zoom, max lead count, theme
- **Database**: `databasePath` defaults to `":memory:"` (in-process testing); production uses `AppDataLocation/sentinel.db`

---

## Uncertainty Decomposition

`EnsemblePredictor` separates total prediction uncertainty:

```
total_variance = aleatoric_variance + epistemic_variance

aleatoric  = mean over models of each model's internal variance
epistemic  = variance over models of their mean predictions
```

Combined standard deviation: `σ_total = sqrt(aleatoric² + epistemic²)`.  
Both components are propagated to `EnsemblePrediction.ciLow95` / `ciHigh95` (computed as `probCrime ± 1.96 * σ_total`) and are visible in the Analytics → Calibration tab.
