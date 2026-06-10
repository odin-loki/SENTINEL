// test_weather_source_advanced.cpp
// Advanced tests for WeatherSource: discomfort index computation,
// caching, data presence, and field logic.
#include <QTest>
#include "ingest/WeatherSource.h"
#include <cmath>

class WeatherSourceAdvancedTest : public QObject
{
    Q_OBJECT

private slots:

    // 1. discomfortIndex: monotone increasing with temperature
    void testDiscomfortIncreasing()
    {
        const double d0  = WeatherSource::discomfortIndex(-10.0);
        const double d20 = WeatherSource::discomfortIndex(20.0);
        const double d40 = WeatherSource::discomfortIndex(40.0);
        QVERIFY2(d40 >= d20, "discomfortIndex(40) should >= discomfortIndex(20)");
        QVERIFY2(d20 >= d0,  "discomfortIndex(20) should >= discomfortIndex(-10)");
    }

    // 2. discomfortIndex: at comfortable temp (18-20C) near 0
    void testDiscomfortComfortableTemp()
    {
        const double d = WeatherSource::discomfortIndex(19.0);
        QVERIFY2(d >= 0.0, "discomfortIndex must be non-negative");
    }

    // 3. discomfortIndex: extreme temp has high discomfort
    void testDiscomfortExtremeHeat()
    {
        const double d0  = WeatherSource::discomfortIndex(20.0);
        const double d45 = WeatherSource::discomfortIndex(45.0);
        QVERIFY2(d45 > d0,
                 qPrintable(QStringLiteral("d45=%1 should > d20=%2").arg(d45).arg(d0)));
    }

    // 4. cachedHourCount() == 0 before any fetch
    void testCacheEmptyBeforeFetch()
    {
        WeatherSource ws;
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    // 5. dataAt() returns nullopt when cache empty
    void testDataAtNulloptWhenEmpty()
    {
        WeatherSource ws;
        const auto d = ws.dataAt(QDateTime::currentDateTimeUtc());
        QVERIFY2(!d.has_value(), "dataAt should return nullopt when cache is empty");
    }

    // 6. WeatherData default values are sensible
    void testWeatherDataDefaults()
    {
        WeatherData wd;
        QVERIFY2(wd.visibilityM > 0.0, "Default visibility must be > 0");
        QVERIFY2(!wd.isRaining, "Default isRaining must be false");
        QVERIFY2(!wd.isExtremeWind, "Default isExtremeWind must be false");
        QVERIFY2(!wd.isLowVisibility, "Default isLowVisibility must be false");
    }

    // 7. fetchComplete and fetchError signals are connectable
    void testSignalsConnectable()
    {
        WeatherSource ws;
        bool connected1 = connect(&ws, &WeatherSource::fetchComplete, [](int) {});
        bool connected2 = connect(&ws, &WeatherSource::fetchError, [](const QString&) {});
        QVERIFY2(connected1, "fetchComplete signal should be connectable");
        QVERIFY2(connected2, "fetchError signal should be connectable");
    }

    // 8. WeatherData: can set all boolean fields
    void testWeatherDataFieldAssignment()
    {
        WeatherData wd;
        wd.isRaining       = true;
        wd.isLowVisibility = true;
        wd.isExtremeWind   = true;
        wd.isDay           = false;
        QVERIFY(wd.isRaining);
        QVERIFY(wd.isLowVisibility);
        QVERIFY(wd.isExtremeWind);
        QVERIFY(!wd.isDay);
    }

    // 9. discomfortIndex: finite for extreme cold
    void testDiscomfortFiniteExtremeCold()
    {
        const double d = WeatherSource::discomfortIndex(-30.0);
        QVERIFY2(std::isfinite(d),
                 qPrintable(QStringLiteral("discomfortIndex(-30) = %1 must be finite").arg(d)));
    }

    // 10. WeatherSource parent property
    void testWeatherSourceParentable()
    {
        QObject parent;
        WeatherSource* ws = new WeatherSource(&parent);
        QVERIFY(ws->parent() == &parent);
    }
};

QTEST_MAIN(WeatherSourceAdvancedTest)
#include "test_weather_source_advanced.moc"
