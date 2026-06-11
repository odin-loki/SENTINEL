#include "ingest/DataQualityScorer.h"
#include <cmath>
#include <algorithm>

DataQualityScorer::DataQualityScorer(const QMap<QString, double>& sourceReliabilityMap)
    : m_sourceReliability(sourceReliabilityMap)
{}

// static
QMap<QString, double> DataQualityScorer::defaultReliabilityMap()
{
    return {
        { QStringLiteral("uk_police_v1"),  0.90 },   // Official UK Police Open Data API
        { QStringLiteral("uk_police"),     0.90 },   // Alias for uk_police_v1
        { QStringLiteral("open_meteo"),    0.85 },   // Open-Meteo free weather service
        { QStringLiteral("weather"),       0.85 },   // Alias for open_meteo
        { QStringLiteral("csv_import"),    0.60 },   // Local CSV — quality depends on source
        { QStringLiteral("csv"),           0.60 },   // Alias for csv_import
        { QStringLiteral("chicago_pd"),    0.85 },   // Chicago PD open data portal
        { QStringLiteral("nypd"),          0.85 },   // NYPD open data portal
        { QStringLiteral("lapd"),          0.80 },   // LAPD open data portal
        { QStringLiteral("abs_australia"), 0.80 },   // Australian Bureau of Statistics
        { QStringLiteral("test"),          0.50 },   // Test / synthetic data
        { QStringLiteral("manual"),        0.40 },   // Manual entry
    };
}

// static
DataQualityScorer DataQualityScorer::withDefaults()
{
    return DataQualityScorer(defaultReliabilityMap());
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::optional<double> effectiveLat(const CrimeEvent& e)
{
    if (e.lat.has_value()) return e.lat;
    if (std::abs(e.latitude) > 1e-9) return e.latitude;
    return std::nullopt;
}

static std::optional<double> effectiveLon(const CrimeEvent& e)
{
    if (e.lon.has_value()) return e.lon;
    if (std::abs(e.longitude) > 1e-9) return e.longitude;
    return std::nullopt;
}

static bool hasLocationText(const CrimeEvent& e)
{
    if (e.locationRaw.has_value() && !e.locationRaw->isEmpty()) return true;
    if (e.addressNormalised.has_value() && !e.addressNormalised->isEmpty()) return true;
    return !e.suburb.isEmpty();
}

// Count significant decimal places of a double value (up to 6).
// e.g. 51.5074 -> 4,  0.0 -> 0,  -0.1278 -> 4,  51.5 -> 1
static int decimalPlacesOf(double val)
{
    if (val == 0.0) return 0;
    val = std::abs(val);
    val -= std::floor(val);           // keep fractional part
    if (val < 1e-10) return 0;

    // Format to 6 decimal places and count trailing non-zero digits
    QString s = QString::number(val, 'f', 6);
    const int dot = s.indexOf('.');
    if (dot < 0) return 0;

    const QString frac = s.mid(dot + 1);
    int lastNonZero = -1;
    for (int i = 0; i < frac.size(); ++i)
        if (frac[i] != QLatin1Char('0')) lastNonZero = i;
    return lastNonZero + 1;
}

// ---------------------------------------------------------------------------
// Completeness
// ---------------------------------------------------------------------------

double DataQualityScorer::completenessScore(const CrimeEvent& e) const
{
    int passed = 0;

    // 1. occurred_at valid
    if (e.occurredAt.has_value() && e.occurredAt->isValid())
        ++passed;

    // 2. lat and lon both present, non-zero, and within valid geographic range
    const auto lat = effectiveLat(e);
    const auto lon = effectiveLon(e);
    if (lat.has_value() && lon.has_value() &&
        std::abs(*lat) > 1e-9 && std::abs(*lon) > 1e-9 &&
        *lat >= -90.0 && *lat <= 90.0 &&
        *lon >= -180.0 && *lon <= 180.0)
        ++passed;

    // 3. crimeType non-empty
    if (!e.crimeType.isEmpty())
        ++passed;

    // 4. location text present (raw, normalised, or suburb)
    if (hasLocationText(e))
        ++passed;

    return passed / 4.0;
}

// ---------------------------------------------------------------------------
// Temporal precision
// ---------------------------------------------------------------------------

QString DataQualityScorer::temporalPrecisionLabel(const CrimeEvent& e) const
{
    if (!e.occurredAt.has_value() || !e.occurredAt->isValid())
        return QStringLiteral("unknown");

    const QTime t = e.occurredAt->time();
    if (t.hour() != 0 || t.minute() != 0 || t.second() != 0)
        return QStringLiteral("hour");

    // Has a valid date but time is midnight — check if year and month are set
    const QDate d = e.occurredAt->date();
    if (d.isValid()) {
        // If day = 1 and time = 00:00:00, treat as month-level precision
        if (d.day() == 1)
            return QStringLiteral("month");
        return QStringLiteral("day");
    }

    return QStringLiteral("unknown");
}

double DataQualityScorer::temporalPrecisionScore(const CrimeEvent& e) const
{
    const QString lbl = temporalPrecisionLabel(e);
    if (lbl == QStringLiteral("hour"))    return 1.0;
    if (lbl == QStringLiteral("day"))     return 0.75;
    if (lbl == QStringLiteral("month"))   return 0.5;
    return 0.0; // "unknown"
}

// ---------------------------------------------------------------------------
// Spatial precision
// ---------------------------------------------------------------------------

QString DataQualityScorer::spatialPrecisionLabel(const CrimeEvent& e) const
{
    const auto latOpt = effectiveLat(e);
    const auto lonOpt = effectiveLon(e);
    if (!latOpt.has_value() || !lonOpt.has_value())
        return QStringLiteral("unknown");

    const double lat = *latOpt;
    const double lon = *lonOpt;

    if (std::abs(lat) < 1e-9 && std::abs(lon) < 1e-9)
        return QStringLiteral("unknown");

    // Penalize out-of-range coordinates
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0)
        return QStringLiteral("unknown");

    // Use minimum decimal places across lat and lon
    const int dp = std::min(decimalPlacesOf(lat), decimalPlacesOf(lon));

    if (dp >= 4) return QStringLiteral("exact");
    if (dp >= 2) return QStringLiteral("block");
    if (dp >= 1) return QStringLiteral("suburb");
    return QStringLiteral("unknown");
}

double DataQualityScorer::spatialPrecisionScore(const CrimeEvent& e) const
{
    const QString lbl = spatialPrecisionLabel(e);
    if (lbl == QStringLiteral("exact"))  return 1.0;
    if (lbl == QStringLiteral("block"))  return 0.66;
    if (lbl == QStringLiteral("suburb")) return 0.33;
    return 0.0; // "unknown"
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

QualityReport DataQualityScorer::score(const CrimeEvent& event) const
{
    QualityReport report;
    report.eventId = event.eventId;

    report.completeness      = completenessScore(event);
    report.temporalPrecision = temporalPrecisionLabel(event);
    report.spatialPrecision  = spatialPrecisionLabel(event);

    // Source reliability: look up in map, default 0.5
    report.sourceReliability =
        m_sourceReliability.value(event.source, 0.5);

    report.compositeScore = std::clamp(
        0.30 * report.completeness +
        0.20 * temporalPrecisionScore(event) +
        0.20 * spatialPrecisionScore(event) +
        0.30 * report.sourceReliability,
        0.0, 1.0);

    report.quarantined = (report.compositeScore < QUARANTINE_THRESHOLD);

    return report;
}

QVector<QualityReport> DataQualityScorer::scoreBatch(
    const QVector<CrimeEvent>& events) const
{
    QVector<QualityReport> results;
    results.reserve(events.size());
    for (const CrimeEvent& ev : events)
        results.append(score(ev));
    return results;
}

double DataQualityScorer::passRate(const QVector<QualityReport>& reports)
{
    if (reports.isEmpty()) return 0.0;
    int passed = 0;
    for (const QualityReport& r : reports)
        if (!r.quarantined) ++passed;
    return passed / static_cast<double>(reports.size());
}
