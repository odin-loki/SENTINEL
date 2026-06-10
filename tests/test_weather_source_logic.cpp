// test_weather_source_logic.cpp
// Tests WeatherSource discomfort index computation, cached data lookup,
// and WeatherData field logic without network access.
#include <QTest>
#include "ingest/WeatherSource.h"
#include <cmath>

class WeatherSourceLogicTest : public QObject
{
    Q_OBJECT

private slots:

    // ── 1. discomfortIndex is defined and finite ──────────────────────────────
    void testDiscomfortIndexFinite()
    {
        for (double t = -20.0; t <= 40.0; t += 5.0) {
            const double d = WeatherSource::discomfortIndex(t);
            QVERIFY2(std::isfinite(d),
                     qPrintable(QStringLiteral("discomfortIndex(%1) must be finite").arg(t)));
        }
    }

    // ── 2. discomfort: higher temperature → higher discomfort ─────────────────
    void testDiscomfortMonotone()
    {
        const double dLow  = WeatherSource::discomfortIndex(0.0);
        const double dHigh = WeatherSource::discomfortIndex(35.0);
        QVERIFY2(dHigh > dLow,
                 qPrintable(QStringLiteral("Discomfort at 35°C (%1) should exceed 0°C (%2)")
                    .arg(dHigh).arg(dLow)));
    }

    // ── 3. discomfort: cold temperatures have positive index ──────────────────
    void testDiscomfortNonNegative()
    {
        for (double t = -30.0; t <= 50.0; t += 1.0) {
            const double d = WeatherSource::discomfortIndex(t);
            QVERIFY2(d >= 0.0,
                     qPrintable(QStringLiteral("Discomfort at %1°C = %2 should be >= 0").arg(t).arg(d)));
        }
    }

    // ── 4. Construction doesn't crash ────────────────────────────────────────
    void testConstructorNoCrash()
    {
        WeatherSource ws;
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // ── 5. dataAt on empty cache returns nullopt ──────────────────────────────
    void testDataAtEmptyCacheReturnsNull()
    {
        WeatherSource ws;
        const auto data = ws.dataAt(QDateTime::currentDateTimeUtc());
        QVERIFY2(!data.has_value(), "Empty cache dataAt should return nullopt");
    }

    // ── 6. WeatherData fields have correct defaults ───────────────────────────
    void testWeatherDataDefaults()
    {
        WeatherData wd;
        QVERIFY2(wd.visibilityM > 0.0, "Default visibility should be positive");
        QVERIFY2(wd.isDay,  "Default isDay should be true");
        QVERIFY(!wd.isRaining);
        QVERIFY(!wd.isLowVisibility);
        QVERIFY(!wd.isExtremeWind);
    }

    // ── 7. isRaining logic: precipitationMm > 0 should set flag ──────────────
    void testIsRainingFlag()
    {
        WeatherData wd;
        wd.precipitationMm = 2.5;
        wd.isRaining = (wd.precipitationMm > 0.1);
        QVERIFY2(wd.isRaining, "Precipitation > 0.1 should mark as raining");
    }

    // ── 8. isLowVisibility: < 1000m is low ────────────────────────────────────
    void testIsLowVisibilityFlag()
    {
        WeatherData wd;
        wd.visibilityM = 500.0;
        wd.isLowVisibility = (wd.visibilityM < 1000.0);
        QVERIFY2(wd.isLowVisibility, "500m visibility should be low");
    }

    // ── 9. isExtremeWind: > 80 km/h is extreme ────────────────────────────────
    void testIsExtremeWindFlag()
    {
        WeatherData wd;
        wd.windspeedKmh = 90.0;
        wd.isExtremeWind = (wd.windspeedKmh > 80.0);
        QVERIFY2(wd.isExtremeWind, "90 km/h wind should be extreme");
    }

    // ── 10. discomfortIndex: 20°C is lower discomfort than 38°C ──────────────
    void testDiscomfortOrdering()
    {
        const double comfortable = WeatherSource::discomfortIndex(20.0);
        const double veryHot     = WeatherSource::discomfortIndex(38.0);
        QVERIFY2(veryHot > comfortable,
                 qPrintable(QStringLiteral("38°C discomfort (%1) should exceed 20°C (%2)")
                    .arg(veryHot).arg(comfortable)));
    }
};

QTEST_MAIN(WeatherSourceLogicTest)
#include "test_weather_source_logic.moc"
