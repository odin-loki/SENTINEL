// CrimeEvent.h — Core data structures for SENTINEL
// All pipeline stages share these types. Keep additions here additive-only.

#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QJsonObject>
#include <optional>
#include <vector>
#include <map>
#include <utility>

// ─────────────────────────────────────────────
// Raw normalised event from any data source
// ─────────────────────────────────────────────

struct CrimeEvent {
    // ── Canonical fields ───────────────────────────────────────────────────
    QString eventId;
    QString source;
    QString sourceVersion;
    QDateTime ingestedAt;
    std::optional<QDateTime> occurredAt;
    std::optional<QDateTime> reportedAt;
    QString crimeType;                   // empty string = unknown
    std::optional<QString> crimeSubtype;
    std::optional<QString> locationRaw;
    std::optional<double> lat;
    std::optional<double> lon;
    std::optional<QString> addressNormalised;
    std::optional<QString> lga;
    QString suburb;                      // empty string = unknown
    std::optional<QString> narrative;
    QString outcome;                     // empty string = unknown; resolved|unresolved|unknown
    std::optional<bool> conviction;
    std::optional<int> suspectCount;
    std::optional<int> victimCount;
    std::optional<QString> weapon;
    QJsonObject meta;                    // source-specific raw fields, preserved verbatim
    double qualityScore = 0.5;           // composite quality score [0,1]

    // ── Flat UI convenience fields (populated by Database::rowToEvent) ─────
    QString   id;                        // = eventId
    QDateTime timestamp;                 // = occurredAt.value_or({})
    QString   locationDescription;       // = locationRaw.value_or("")
    double    latitude  = 0.0;           // = lat.value_or(0.0)
    double    longitude = 0.0;           // = lon.value_or(0.0)
};

// ─────────────────────────────────────────────
// Data quality assessment
// ─────────────────────────────────────────────

struct QualityReport {
    QString eventId;
    double completeness       = 0.0;     // fraction of required fields non-null
    QString temporalPrecision;           // "hour" | "day" | "month" | "unknown"
    QString spatialPrecision;            // "exact" | "block" | "suburb" | "unknown"
    double sourceReliability  = 0.5;     // per-source historical reliability
    double compositeScore     = 0.0;     // weighted composite [0,1]
    bool quarantined          = false;   // true if compositeScore < 0.3
};

// ─────────────────────────────────────────────
// NLP outputs
// ─────────────────────────────────────────────

struct MOFeatures {
    std::optional<QString> entryMethod;  // forced_entry | unlocked | deception | tailgating
    std::optional<QString> timeOfDay;    // early_morning | morning | afternoon | evening | night
    std::optional<QString> targetType;   // residential | commercial | vehicle | person
    std::optional<QString> weaponType;
    std::vector<QString> itemsTaken;
    std::optional<QString> victimProfile;
    std::optional<QString> soloOrGroup;  // solo | group | unknown
    std::optional<QString> precaution;   // gloves | mask | balaclava
};

struct NLPResult {
    QString eventId;
    std::optional<QString> crimeType;
    double crimeTypeConfidence  = 0.0;
    double severityScore        = 0.0;   // 0.0 – 1.0
    double sentimentCompound    = 0.0;   // -1.0 to 1.0
    bool threatSignal           = false;
    std::optional<QDateTime> extractedDatetime;
    std::vector<QString> extractedLocations;
    MOFeatures moFeatures;
    QString rawText;
};

// ─────────────────────────────────────────────
// Statistical model outputs
// ─────────────────────────────────────────────

struct PoissonPrediction {
    double lambda               = 0.0;
    double probAtLeastOne       = 0.0;
    double expectedCount        = 0.0;
    std::pair<double, double> ci90{0.0, 0.0};
    int nObservations           = 0;
    QString model;              // "poisson" | "negative_binomial"
};

struct HawkesParams {
    double mu     = 0.1;        // background rate
    double alpha  = 0.3;        // excitation magnitude
    double beta   = 0.5;        // temporal decay rate (1/days)
    double sigma  = 0.01;       // spatial bandwidth (degrees)
    double logLik = 0.0;        // log-likelihood at fitted params
};

// ─────────────────────────────────────────────
// Inference engine outputs
// ─────────────────────────────────────────────

struct SeriesMatch {
    QString seriesId;
    int memberCount             = 0;
    double linkProbability      = 0.0;
    double spatialDistanceM     = 0.0;
    double temporalDistanceDays = 0.0;
    double moSimilarity         = 0.0;
    double compositeScore       = 0.0;
    QString method;
};

struct GeographicProfile {
    std::vector<std::vector<double>> probabilitySurface;  // [nLat][nLon] normalised
    double peakLat              = 0.0;
    double peakLon              = 0.0;
    double peakProbability      = 0.0;
    double searchArea50pct      = 0.0;  // km²
    double searchArea80pct      = 0.0;  // km²
    std::vector<double> gridLats;
    std::vector<double> gridLons;
    QString method;
};

struct EvidenceWeight {
    QString evidenceType;
    double likelihoodRatio       = 1.0;
    double posteriorOdds         = 1.0;
    double posteriorProbability  = 0.5;
    double reliability           = 0.5;
    QString notes;
};

struct AnomalySignal {
    QString eventId;
    double isolationScore       = 0.0;
    double lofScore             = 0.0;
    double zScoreTemporal       = 0.0;
    double zScoreSpatial        = 0.0;
    double combinedScore        = 0.0;
    bool isAnomaly              = false;
    std::vector<QString> signalReasons;  // renamed: "signals" is a Qt reserved keyword
};

struct InvestigativeLead {
    int rank                    = 0;
    QString category;
    QString headline;
    QString detail;
    double confidence           = 0.0;
    QString confidenceMethod;
    QJsonObject supportingData;
    std::vector<QString> contradictions;
    std::vector<QString> provenance;    // chain: data source → rule → output
    QDateTime generatedAt;

    // ── UI aliases ──────────────────────────────────────────────────────────
    // description maps to detail; provenanceChain maps to provenance
    QStringList relatedEventIds;        // optional related event IDs for UI display
};

// ─────────────────────────────────────────────
// MO analysis types (shared between MOAnalyser and HintEngine)
// ─────────────────────────────────────────────

struct MOCaseRecord {
    QString caseId;
    QString moText;           // canonical MO string for TF-IDF
    bool resolved     = false;
    QString outcome;
    QString suspectProfile;
    int convictionYear = 0;   // 0 if none
};

struct MOMatch {
    QString caseId;
    double similarityScore    = 0.0;
    QStringList sharedFeatures;
    bool resolved             = false;
    QString outcome;
    QString suspectProfile;
    int convictionYear        = 0;
};

// ─────────────────────────────────────────────
// Co-offending network lead
// ─────────────────────────────────────────────

struct NetworkLead {
    QString personId;
    QString connectionType;   // direct_cooffender | second_degree | venue_linked
    int sharedIncidents       = 0;
    double centralityScore    = 0.0;
    int communityId           = -1;
    double riskScore          = 0.0;
    QString reasoning;
};
