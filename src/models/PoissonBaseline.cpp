#include "models/PoissonBaseline.h"
#include <QDate>
#include <QString>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

// ---------------------------------------------------------------------------
// Bucket key construction
// hourBin = hour / 2  → 12 bins per day
// ---------------------------------------------------------------------------

QString PoissonBaseline::bucketKey(const QString& zone, int hourBin,
                                    int dow, int month,
                                    const QString& ctype) const
{
    return zone + QChar('|')
         + QString::number(hourBin) + QChar('|')
         + QString::number(dow)     + QChar('|')
         + QString::number(month)   + QChar('|')
         + ctype.toLower();
}

// ---------------------------------------------------------------------------
// fit()
// Accumulates counts per (zone, hourBin, dow, month, crimeType) bucket,
// then checks for overdispersion and fits negative binomial if needed.
// ---------------------------------------------------------------------------

void PoissonBaseline::fit(const QVector<EventRecord>& events)
{
    m_rates.clear();
    m_nbParams.clear();
    m_totalEvents = events.size();

    // Count raw events per bucket (each occurrence increments one day-slot)
    // We accumulate daily counts: for each event we add 1 to its bucket slot,
    // then store those raw per-event values; to get count distributions we
    // aggregate by (zone, day) first, then slot each daily count.

    // Intermediate map: bucket → daily counts keyed by ISO date string
    QMap<QString, QMap<QString, int>> dailyCounts;

    for (const auto& ev : events) {
        QDate d = ev.occurredAt.date();
        int hour    = ev.occurredAt.time().hour();
        int hourBin = hour / 2;
        int dow     = d.dayOfWeek() - 1;   // 0=Mon..6=Sun
        int month   = d.month();

        QString bk = bucketKey(ev.zoneId, hourBin, dow, month, ev.crimeType);
        QString dayKey = d.toString(Qt::ISODate);
        dailyCounts[bk][dayKey]++;
    }

    // Flatten daily counts into count vectors
    for (auto it = dailyCounts.constBegin(); it != dailyCounts.constEnd(); ++it) {
        QVector<int> counts;
        counts.reserve(it.value().size());
        for (auto cnt : it.value()) {
            counts.append(cnt);
        }
        m_rates[it.key()] = counts;
    }

    // Fit negative binomial for overdispersed buckets
    for (auto it = m_rates.constBegin(); it != m_rates.constEnd(); ++it) {
        const QVector<int>& counts = it.value();
        int n = counts.size();
        if (n <= 5) continue;

        double sum = 0.0, sumSq = 0.0;
        for (int c : counts) {
            sum   += c;
            sumSq += static_cast<double>(c) * c;
        }
        double mean = sum / n;
        double var  = sumSq / n - mean * mean;

        // Overdispersion: variance substantially exceeds mean
        if (var > mean && mean > 0.0) {
            // NB(r, p) with PMF ∝ p^k (1-p)^r: E[X]=r*p/(1-p), Var[X]=r*p/(1-p)²
            // Method of moments: r = μ²/(σ²-μ), p = μ/(μ+r) = 1 - μ/σ²
            double r = mean * mean / (var - mean);
            double p = mean / (mean + r);
            m_nbParams[it.key()] = {r, p};
        }
    }
}

// ---------------------------------------------------------------------------
// Static distribution helpers
// ---------------------------------------------------------------------------

double PoissonBaseline::poissonPMF(double lambda, int k)
{
    if (lambda <= 0.0) return (k == 0) ? 1.0 : 0.0;
    // log-space: k*log(lambda) - lambda - lgamma(k+1)
    double logP = k * std::log(lambda) - lambda - std::lgamma(static_cast<double>(k) + 1.0);
    return std::exp(logP);
}

double PoissonBaseline::poissonPPF(double lambda, double p)
{
    if (lambda <= 0.0) return 0.0;
    double cdf = 0.0;
    for (int k = 0; k < 10000; ++k) {
        cdf += poissonPMF(lambda, k);
        if (cdf >= p) return static_cast<double>(k);
    }
    return lambda + 10.0 * std::sqrt(lambda);   // fallback
}

double PoissonBaseline::negBinPMF(double r, double p, int k)
{
    if (r <= 0.0 || p <= 0.0 || p >= 1.0) return 0.0;
    // PMF = Γ(r+k)/(Γ(r)*k!) * p^k * (1-p)^r
    double logP = std::lgamma(r + k) - std::lgamma(r) - std::lgamma(k + 1.0)
                + k * std::log(p) + r * std::log(1.0 - p);
    return std::exp(logP);
}

double PoissonBaseline::negBinPPF(double r, double p_param, double quantile)
{
    if (r <= 0.0 || p_param <= 0.0) return 0.0;
    double cdf = 0.0;
    for (int k = 0; k < 10000; ++k) {
        cdf += negBinPMF(r, p_param, k);
        if (cdf >= quantile) return static_cast<double>(k);
    }
    // fallback: mean of NegBin = r*p/(1-p)
    return r * p_param / (1.0 - p_param);
}

// ---------------------------------------------------------------------------
// predict()
// ---------------------------------------------------------------------------

PoissonPrediction PoissonBaseline::predict(const QString& zoneId,
                                            const QDateTime& dt,
                                            const QString& crimeType) const
{
    PoissonPrediction pred;
    pred.model = QStringLiteral("NonHomogeneousPoisson");

    QDate d = dt.date();
    int hourBin = dt.time().hour() / 2;
    int dow     = d.dayOfWeek() - 1;
    int month   = d.month();

    QString bk = bucketKey(zoneId, hourBin, dow, month, crimeType);

    auto rateIt = m_rates.constFind(bk);
    if (rateIt == m_rates.constEnd() || rateIt.value().isEmpty()) {
        // No history for this bucket — return near-zero prior
        pred.lambda        = 0.01;
        pred.expectedCount = 0.01;
        pred.probAtLeastOne = 1.0 - poissonPMF(0.01, 0);
        pred.ci90          = {0.0, 2.0};
        pred.nObservations  = 0;
        return pred;
    }

    const QVector<int>& counts = rateIt.value();
    int n = counts.size();
    double sum = 0.0;
    for (int c : counts) sum += c;
    // λ = total count / exposure days (one observation per calendar day in bucket)
    const double lambda = sum / static_cast<double>(n);

    pred.nObservations = n;
    pred.lambda        = lambda;
    pred.expectedCount = lambda;

    auto nbIt = m_nbParams.constFind(bk);
    if (nbIt != m_nbParams.constEnd()) {
        // Use negative binomial model
        double r     = nbIt.value().first;
        double p_nb  = nbIt.value().second;
        pred.model   = QStringLiteral("NegativeBinomial");
        pred.probAtLeastOne = 1.0 - negBinPMF(r, p_nb, 0);
        pred.ci90 = { negBinPPF(r, p_nb, 0.05),
                      negBinPPF(r, p_nb, 0.95) };
    } else {
        pred.probAtLeastOne = 1.0 - poissonPMF(lambda, 0);
        pred.ci90 = { poissonPPF(lambda, 0.05),
                      poissonPPF(lambda, 0.95) };
    }

    return pred;
}
