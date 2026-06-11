#pragma once
#include <QMap>
#include <QVector>
#include "core/CrimeEvent.h"

// QualityReport is defined in core/CrimeEvent.h

class DataQualityScorer {
public:
    // sourceReliabilityMap: maps source name (e.g. "uk_police_v1") -> reliability [0,1]
    explicit DataQualityScorer(
        const QMap<QString, double>& sourceReliabilityMap = {});

    // Convenience: create scorer with pre-populated reliability values for known sources
    static DataQualityScorer withDefaults();

    // Default reliability values for known SENTINEL data sources
    static QMap<QString, double> defaultReliabilityMap();

    QualityReport score(const CrimeEvent& event) const;

    // Batch: returns vector of reports, same order as input
    QVector<QualityReport> scoreBatch(const QVector<CrimeEvent>& events) const;

    // Fraction of events not quarantined
    static double passRate(const QVector<QualityReport>& reports);

private:
    double completenessScore(const CrimeEvent& e) const;
    double temporalPrecisionScore(const CrimeEvent& e) const;
    double spatialPrecisionScore(const CrimeEvent& e) const;
    QString temporalPrecisionLabel(const CrimeEvent& e) const;
    QString spatialPrecisionLabel(const CrimeEvent& e) const;

    QMap<QString, double> m_sourceReliability;

    static constexpr double QUARANTINE_THRESHOLD = 0.3;
};
