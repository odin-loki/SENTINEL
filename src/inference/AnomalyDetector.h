#pragma once
#include <QVector>
#include <QString>
#include <QMap>
#include "core/CrimeEvent.h"

struct AnomalyFeatureVector {
    QString eventId;
    double lat;
    double lon;
    double tDays;
    double hourNorm;
    int crimeTypeCode;
};

class AnomalyDetector {
public:
    explicit AnomalyDetector(double contamination = 0.05);

    void fit(const QVector<AnomalyFeatureVector>& data);

    // Non-const: may auto-fit on the provided batch if not previously fitted.
    QVector<AnomalySignal> detectAnomalies(
        const QVector<AnomalyFeatureVector>& events);

    bool isFitted() const { return m_fitted; }

private:
    double m_contamination;
    bool m_fitted = false;

    struct DimStats { double mean, stddev; };
    QVector<DimStats> m_dimStats;

    double m_trainLatMean  = 0.0, m_trainLatStd  = 1.0;
    double m_trainLonMean  = 0.0, m_trainLonStd  = 1.0;
    double m_trainTMean    = 0.0, m_trainTStd    = 1.0;
    double m_anomalyThreshold = 0.65;

    double isolationScore(const AnomalyFeatureVector& ev,
                           const QVector<AnomalyFeatureVector>& context) const;

    double zScoreTemporal(double tDays) const;
    double zScoreSpatial(double lat, double lon) const;

    double localDensityRatio(const AnomalyFeatureVector& ev,
                              const QVector<AnomalyFeatureVector>& data,
                              int k = 10) const;

    double featureDistance(const AnomalyFeatureVector& a,
                           const AnomalyFeatureVector& b) const;
};
