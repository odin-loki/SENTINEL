#pragma once
#include <QDateTime>
#include <QMap>
#include <QString>
#include <cmath>

struct TemporalFeatureVector {
    double hourSin, hourCos;
    double dowSin, dowCos;       // day-of-week
    double monthSin, monthCos;
    double doySin, doyCos;       // day-of-year
    int hourRaw;
    int dowRaw;                  // 0=Mon, 6=Sun
    bool isWeekend;
    bool isNight;                // hour >= 22 || hour <= 5
    bool isPublicHoliday;
    int daysFromPayday;          // distance to nearest fortnightly payday
    int weekOfMonth;
    double lunarPhase;           // 0=new moon, 0.5=full moon (approximate)
    double sunAltitudeDeg;       // approximate sun altitude
    bool isDark;                 // sun altitude < -6 degrees
};

class TemporalFeatures {
public:
    static TemporalFeatureVector compute(const QDateTime& dt);

private:
    static double cyclicalSin(double val, double period);
    static double cyclicalCos(double val, double period);
    static double lunarPhaseApprox(const QDate& date);
    static double sunAltitudeApprox(const QDateTime& dt, double lat = 51.5);
    static int daysFromPayday(const QDate& date);
};
