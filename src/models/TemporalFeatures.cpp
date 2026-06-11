#include "models/TemporalFeatures.h"
#include <QDate>
#include <QTime>
#include <cmath>
#include <algorithm>

static constexpr double M_PI_VAL = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Cyclical encoding helpers
// ---------------------------------------------------------------------------

double TemporalFeatures::cyclicalSin(double val, double period)
{
    return std::sin(2.0 * M_PI_VAL * val / period);
}

double TemporalFeatures::cyclicalCos(double val, double period)
{
    return std::cos(2.0 * M_PI_VAL * val / period);
}

// ---------------------------------------------------------------------------
// Lunar phase approximation
// Known new-moon epoch: 2000-01-06 (JD 2451549.5)
// Synodic period = 29.53059 days
// ---------------------------------------------------------------------------

double TemporalFeatures::lunarPhaseApprox(const QDate& date)
{
    static const QDate epoch(2000, 1, 6);
    double daysSince = static_cast<double>(epoch.daysTo(date));
    constexpr double synodicPeriod = 29.53059;
    double phase = std::fmod(daysSince, synodicPeriod);
    if (phase < 0.0) phase += synodicPeriod;
    return phase / synodicPeriod;   // 0 = new moon, 0.5 = full moon
}

// ---------------------------------------------------------------------------
// Sun altitude approximation
// Uses a simplified solar position model:
//   - Solar declination: δ ≈ -23.45° * cos(2π/365 * (doy + 10))
//   - Hour angle: H = (hour_UTC - 12) * 15°
//   - sin(alt) = sin(lat)*sin(δ) + cos(lat)*cos(δ)*cos(H)
// lat is in degrees (defaults to 51.5° ≈ London).
// ---------------------------------------------------------------------------

double TemporalFeatures::sunAltitudeApprox(const QDateTime& dt, double lat)
{
    int doy = dt.date().dayOfYear();
    double hourUTC = dt.toUTC().time().hour()
                     + dt.toUTC().time().minute() / 60.0;

    double latRad = lat * M_PI_VAL / 180.0;

    // Solar declination (radians)
    double declinationDeg = -23.45 * std::cos(2.0 * M_PI_VAL * (doy + 10) / 365.0);
    double declinationRad = declinationDeg * M_PI_VAL / 180.0;

    // Hour angle (radians): solar noon when H = 0
    double hourAngleDeg = (hourUTC - 12.0) * 15.0;
    double hourAngleRad = hourAngleDeg * M_PI_VAL / 180.0;

    double sinAlt = std::sin(latRad) * std::sin(declinationRad)
                  + std::cos(latRad) * std::cos(declinationRad) * std::cos(hourAngleRad);

    // Clamp to [-1, 1] before asin to guard against floating-point drift
    sinAlt = std::max(-1.0, std::min(1.0, sinAlt));
    double altitudeRad = std::asin(sinAlt);
    return altitudeRad * 180.0 / M_PI_VAL;
}

// ---------------------------------------------------------------------------
// Days from nearest fortnightly payday
// Payday assumed to fall every 14 days; use day-of-year modulo 14.
// daysFromPayday = min(d, 14-d) where d = dayOfYear % 14
// ---------------------------------------------------------------------------

int TemporalFeatures::daysFromPayday(const QDate& date)
{
    int d = date.dayOfYear() % 14;
    return std::min(d, 14 - d);
}

// ---------------------------------------------------------------------------
// Main compute method
// ---------------------------------------------------------------------------

TemporalFeatureVector TemporalFeatures::compute(const QDateTime& dt)
{
    TemporalFeatureVector fv{};
    if (!dt.isValid())
        return fv;

    const QDate date = dt.date();
    const QTime time = dt.time();

    int hour  = time.hour();
    int dow   = date.dayOfWeek() - 1;   // Qt: 1=Mon..7=Sun → 0=Mon..6=Sun
    int month = date.month();
    int doy   = date.dayOfYear();

    // Cyclical encodings
    fv.hourSin  = cyclicalSin(hour,  24.0);
    fv.hourCos  = cyclicalCos(hour,  24.0);
    fv.dowSin   = cyclicalSin(dow,   7.0);
    fv.dowCos   = cyclicalCos(dow,   7.0);
    fv.monthSin = cyclicalSin(month - 1, 12.0);   // 0-indexed
    fv.monthCos = cyclicalCos(month - 1, 12.0);
    fv.doySin   = cyclicalSin(doy,   365.0);
    fv.doyCos   = cyclicalCos(doy,   365.0);

    // Raw values
    fv.hourRaw = hour;
    fv.dowRaw  = dow;

    // Boolean features
    fv.isWeekend = (dow >= 5);                      // Sat=5, Sun=6
    fv.isNight   = (hour >= 22 || hour <= 5);

    // Public holiday: placeholder — extend by injecting a holiday set if needed
    fv.isPublicHoliday = false;

    // Payday proximity
    fv.daysFromPayday = daysFromPayday(date);

    // Week of month: 1-indexed; floor((day-1)/7) + 1
    fv.weekOfMonth = (date.day() - 1) / 7 + 1;

    // Lunar phase
    fv.lunarPhase = lunarPhaseApprox(date);

    // Sun altitude and darkness flag
    fv.sunAltitudeDeg = sunAltitudeApprox(dt);
    fv.isDark = (fv.sunAltitudeDeg < -6.0);

    return fv;
}
