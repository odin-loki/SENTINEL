#pragma once
#include <QVector>
#include <QString>
#include "models/SeriesDetector.h"

struct NearRepeatAlert {
    QString eventId;
    QString priorEventId;
    double alertScore            = 0.0;
    double spatialDistanceM      = 0.0;
    double temporalDistanceDays  = 0.0;
};

// Space-time near-repeat victimisation analysis (Ratcliffe 2009; Knox 1964).
class NearRepeatVictimisation {
public:
    explicit NearRepeatVictimisation(double bandwidthM  = 200.0,
                                     double windowDays  = 14.0);

    // Pairwise near-repeat alerts for events after the first.
    // Returns empty when fewer than 2 events are supplied.
    QVector<NearRepeatAlert> analyse(const QVector<SeriesEvent>& events) const;

    // Knox ratio: observed near pairs / expected near pairs under independence.
    // Values > 1.0 indicate significant space-time clustering.
    double knoxStatistic(const QVector<SeriesEvent>& events) const;

    // Proximity score in [0,1] combining spatial and temporal decay kernels.
    double alertScore(double spatialDistanceM,
                      double temporalDistanceDays,
                      const QString& crimeType = QString()) const;

    double bandwidthM() const { return m_bandwidthM; }
    double windowDays() const { return m_windowDays; }

    // Crime-type calibrated bandwidth from published near-repeat literature.
    static double bandwidthFor(const QString& crimeType);

private:
    double m_bandwidthM;
    double m_windowDays;

    static double spatialDecay(double distM, double bandwidthM);
    static double temporalDecay(double dtDays, double windowDays);
};
