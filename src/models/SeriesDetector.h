#pragma once
#include <QVector>
#include <QMap>
#include <QSet>
#include <QString>
#include <array>
#include "core/CrimeEvent.h"

struct SeriesEvent {
    QString eventId;
    double  lat, lon;
    double  tDays;          // days since reference epoch
    QString crimeType;
    QString moText;         // canonical MO string for similarity
};

struct CrimeSeries {
    QString seriesId;
    QVector<SeriesEvent> members;
    QString dominantCrimeType;
    double centroidLat  = 0.0;
    double centroidLon  = 0.0;
    double firstDays    = 0.0;
    double lastDays     = 0.0;
};

class SeriesDetector {
public:
    // Near-repeat parameters calibrated from published research
    struct NearRepeatParams {
        double distM      = 300.0;  // spatial epsilon in metres
        double days       = 14.0;   // temporal epsilon in days
        double multiplier = 3.0;    // crime risk multiplier
    };

    SeriesDetector(double epsKm     = 0.3,
                   double epsDays   = 14.0,
                   int    minSamples = 3);

    // Detect series in a set of events using spatiotemporal DBSCAN
    QVector<CrimeSeries> detectSeries(const QVector<SeriesEvent>& events);

    // Convenience overload: converts CrimeEvent → SeriesEvent automatically
    QVector<CrimeSeries> detect(const QVector<CrimeEvent>& events);

    // Compute P(new event ∈ series) given candidate series and MO similarity
    SeriesMatch linkProbability(const SeriesEvent&  newEvent,
                                 const CrimeSeries&  series,
                                 double              moSimilarity) const;

    // MO text similarity — Jaccard coefficient on word-token sets
    static double moJaccard(const QString& a, const QString& b);

    // Haversine distance between two lat/lon coordinates (kilometres)
    static double haversineKm(double lat1, double lon1,
                               double lat2, double lon2);

    // Look up near-repeat params by crime type (lower-cased, normalised).
    // Public so external callers and tests can inspect the calibration table.
    static NearRepeatParams nearRepeatFor(const QString& crimeType);

private:
    double m_epsKm;
    double m_epsDays;
    int    m_minSamples;

    // Core DBSCAN: returns per-point cluster label (-1 = noise, >=0 = cluster)
    QVector<int> dbscan(const QVector<std::array<double, 3>>& points,
                         double eps, int minPts) const;

    // Euclidean distance in normalised 3D feature space
    static double dist3D(const std::array<double, 3>& a,
                          const std::array<double, 3>& b);

    // Published near-repeat table (crime-type → params)
    static const QMap<QString, NearRepeatParams>& nearRepeatTable();
};
