#include "models/RiskForecaster.h"

#include <algorithm>
#include <cmath>
#include <QTimeZone>

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

RiskForecaster::RiskForecaster(int horizonDays)
    : m_horizonDays(std::max(horizonDays, 1))
{
}

void RiskForecaster::setAlertThresholds(double elevated, double high, double critical)
{
    m_elevated = elevated;
    m_high     = high;
    m_critical = critical;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Fit Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

void RiskForecaster::fit(const QVector<CrimeEvent>& events, const QString& crimeType)
{
    m_crimeType = crimeType;
    m_recentCounts.clear();
    m_fittedZones.clear();

    // Find the latest event date (use occurredAt when present — matches Poisson fit)
    QDate latest;
    for (const auto& e : events) {
        const QDate d = e.occurredAt.value_or(e.timestamp).date();
        if (!latest.isValid() || d > latest) latest = d;
    }

    // Build Poisson records and recent-count index (last 30 days)
    QVector<PoissonBaseline::EventRecord> records;
    for (const auto& e : events) {
        if (!crimeType.isEmpty() && e.crimeType != crimeType)
            continue;
        const QString zone = e.suburb.isEmpty() ? e.lga.value_or("unknown") : e.suburb;
        const QDateTime dt = e.occurredAt.value_or(e.timestamp);
        records.append({zone, dt, e.crimeType});
        m_fittedZones.insert(zone);

        if (latest.isValid()) {
            const QDate evDate = dt.date();
            const int daysAgo = evDate.daysTo(latest);
            if (daysAgo >= 0 && daysAgo <= 30)
                m_recentCounts[zone].append(evDate);
        }
    }

    m_poisson.fit(records);
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Escalation factor Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

double RiskForecaster::escalation(const QString& zoneId, const QDate& date) const
{
    const auto it = m_recentCounts.find(zoneId);
    if (it == m_recentCounts.end()) return 1.0;

    // Count events in last 14 days before forecast date, weighted by recency
    double weight = 0.0;
    for (const QDate& evDate : it.value()) {
        const int lag = evDate.daysTo(date);
        if (lag >= 0 && lag <= 14)
            weight += std::exp(-0.1 * lag);   // exponential decay
    }

    // Escalation saturates at 2.0
    return 1.0 + std::min(weight / 3.0, 1.0);
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Temporal multiplier (DOW and month seasonality) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

double RiskForecaster::temporalMultiplier(const QDate& date) const
{
    // Day-of-week: weekends have historically higher property crime
    const int dow = date.dayOfWeek();   // 1=Mon, 7=Sun
    const double dowFactor = (dow >= 5) ? 1.2 : 1.0;   // +20% Fri/Sat/Sun

    // Month: winter months often higher in UK (dark earlier)
    const int month = date.month();
    const double monthFactor = (month >= 10 || month <= 2) ? 1.1 : 1.0;

    return dowFactor * monthFactor;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Forecast zone Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

ZoneForecast RiskForecaster::forecastZone(const QString& zoneId,
                                            const QDateTime& from) const
{
    ZoneForecast zf;
    zf.zoneId = zoneId;

    if (!m_poisson.isFitted()) {
        for (int d = 0; d < m_horizonDays; ++d) {
            ForecastDay day;
            day.zoneId   = zoneId;
            day.date     = from.addDays(d).date();
            day.rank     = d + 1;
            day.riskScore = 0.0;
            zf.days.append(day);
        }
        zf.weeklyRisk = 0.0;
        zf.alertLevel = 0;
        return zf;
    }

    for (int d = 0; d < m_horizonDays; ++d) {
        const QDateTime dt = from.addDays(d);
        const QDate date   = dt.date();

        // Poisson prediction
        const auto pred = m_poisson.predict(zoneId, dt, m_crimeType);
        const double baseProb = pred.probAtLeastOne;

        // Escalation and temporal modifiers
        const double esc  = escalation(zoneId, date);
        const double temp = temporalMultiplier(date);

        // Composite risk: clamp to [0,1]
        const double rawRisk = std::clamp(baseProb * esc * temp, 0.0, 1.0);

        ForecastDay day;
        day.zoneId           = zoneId;
        day.date             = date;
        day.baselineProb     = baseProb;
        day.escalationFactor = esc;
        day.temporalFactor   = temp;
        day.expectedCount    = pred.expectedCount * esc * temp;
        day.riskScore        = rawRisk;
        day.rank             = d + 1;
        day.explanation      = QStringLiteral("Baseline=%1 \xd7 Escalation=%2 \xd7 Temporal=%3")
            .arg(baseProb, 0, 'f', 3)
            .arg(esc,      0, 'f', 2)
            .arg(temp,     0, 'f', 2);
        zf.days.append(day);
    }

    // Weekly risk = mean daily risk; find peak day
    if (!zf.days.isEmpty()) {
        double sum = 0.0;
        double peakRisk = -1.0;
        for (int i = 0; i < zf.days.size(); ++i) {
            sum += zf.days[i].riskScore;
            if (zf.days[i].riskScore > peakRisk) {
                peakRisk       = zf.days[i].riskScore;
                zf.peakDayIndex = i;
            }
        }
        zf.weeklyRisk = sum / zf.days.size();
    }

    // Alert level
    if (zf.weeklyRisk >= m_critical)      zf.alertLevel = 3;
    else if (zf.weeklyRisk >= m_high)     zf.alertLevel = 2;
    else if (zf.weeklyRisk >= m_elevated) zf.alertLevel = 1;
    else                                  zf.alertLevel = 0;

    return zf;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Forecast all zones Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

QVector<ZoneForecast> RiskForecaster::forecast(const QDateTime& from) const
{
    QVector<ZoneForecast> results;
    for (const QString& zone : m_fittedZones)
        results.append(forecastZone(zone, from));

    // Sort by weeklyRisk descending
    std::sort(results.begin(), results.end(),
              [](const ZoneForecast& a, const ZoneForecast& b) {
                  return a.weeklyRisk > b.weeklyRisk;
              });

    return results;
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬ Alert label Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

QString ZoneForecast::alertLabel() const
{
    switch (alertLevel) {
        case 3: return QStringLiteral("CRITICAL");
        case 2: return QStringLiteral("HIGH");
        case 1: return QStringLiteral("ELEVATED");
        default: return QStringLiteral("NORMAL");
    }
}
