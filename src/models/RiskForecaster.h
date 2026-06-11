#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// RiskForecaster — multi-day crime probability forecasting
//
// Produces a risk forecast for the next N days for each zone, combining:
//   - Poisson baseline (long-term rate)
//   - Temporal features (cyclical patterns: hour, DOW, month)
//   - Recent incident escalation (decay factor over last 14 days)
//   - KDE hotspot proximity (spatial risk amplification)
//
// Output: ForecastDay per zone per day, sorted by risk descending
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QMap>
#include <QString>
#include <QDateTime>
#include "core/CrimeEvent.h"
#include "models/PoissonBaseline.h"
#include "models/TemporalFeatures.h"

struct ForecastDay {
    QString   zoneId;
    QDate     date;
    double    riskScore        = 0.0;   // composite risk [0,1]
    double    baselineProb     = 0.0;   // Poisson P(crime > 0)
    double    escalationFactor = 1.0;   // recency boost [1, ∞)
    double    temporalFactor   = 1.0;   // cyclical pattern multiplier
    double    expectedCount    = 0.0;   // E[crimes]
    int       rank             = 0;
    QString   explanation;
};

struct ZoneForecast {
    QString zoneId;
    QVector<ForecastDay> days;
    double weeklyRisk = 0.0;    // mean of daily risks over forecast window
    int    alertLevel = 0;      // 0=Normal, 1=Elevated, 2=High, 3=Critical
    QString alertLabel() const;
};

class RiskForecaster {
public:
    explicit RiskForecaster(int horizonDays = 7);

    // Fit from historical events. Zones are inferred from suburb field.
    void fit(const QVector<CrimeEvent>& events, const QString& crimeType = {});

    // Forecast for all fitted zones over the horizon
    QVector<ZoneForecast> forecast(const QDateTime& from) const;

    // Forecast a specific zone
    ZoneForecast forecastZone(const QString& zoneId,
                               const QDateTime& from) const;

    bool isFitted() const { return m_poisson.isFitted(); }
    int zoneCount() const { return m_recentCounts.size(); }

    // Alert thresholds (configurable)
    void setAlertThresholds(double elevated, double high, double critical);

private:
    double escalation(const QString& zoneId, const QDate& date) const;
    double temporalMultiplier(const QDate& date) const;

    int         m_horizonDays;
    PoissonBaseline m_poisson;
    QString     m_crimeType;

    // zoneId → list of recent event dates (last 30 days)
    QMap<QString, QVector<QDate>> m_recentCounts;

    // Alert thresholds
    double m_elevated = 0.3;
    double m_high     = 0.5;
    double m_critical = 0.75;
};
