#pragma once
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QMap>
#include <QPair>
#include "core/CrimeEvent.h"

class PoissonBaseline {
public:
    struct EventRecord {
        QString zoneId;
        QDateTime occurredAt;
        QString crimeType;
    };

    void fit(const QVector<EventRecord>& events);

    PoissonPrediction predict(const QString& zoneId,
                              const QDateTime& dt,
                              const QString& crimeType) const;

    bool isFitted() const { return !m_rates.isEmpty(); }
    int totalEvents() const { return m_totalEvents; }

    // ── Public distribution helpers (used by tests and external callers) ────
    // Poisson PMF: exp(k*log(lambda) - lambda - lgamma(k+1))
    static double poissonPMF(double lambda, int k);
    // Poisson quantile (PPF) via sequential CDF accumulation
    static double poissonPPF(double lambda, double p);
    // Negative binomial PMF
    static double negBinPMF(double r, double p, int k);
    // Negative binomial quantile (PPF)
    static double negBinPPF(double r, double p_param, double quantile);

private:
    // key = "zoneId|hourBin|dow|month|crimeType" → list of per-bucket counts
    QMap<QString, QVector<int>> m_rates;
    // zones with overdispersion → (r, p) negative binomial params
    QMap<QString, QPair<double, double>> m_nbParams;
    int m_totalEvents = 0;

    QString bucketKey(const QString& zone, int hourBin,
                      int dow, int month,
                      const QString& ctype) const;
};
