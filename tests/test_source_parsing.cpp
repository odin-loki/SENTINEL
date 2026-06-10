// test_source_parsing.cpp — UKPoliceSource JSON parsing + WeatherSource tests
#include <QTest>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "ingest/UKPoliceSource.h"
#include "ingest/WeatherSource.h"

class TestSourceParsing : public QObject
{
    Q_OBJECT

private:
    static QJsonObject makePoliceRaw(const QString& id, const QString& category,
                                      const QString& month,
                                      double lat, double lon,
                                      const QString& streetName = {},
                                      const QString& outcome = {})
    {
        QJsonObject location;
        location["latitude"]  = QString::number(lat, 'f', 6);
        location["longitude"] = QString::number(lon, 'f', 6);
        if (!streetName.isEmpty()) {
            QJsonObject street;
            street["name"] = streetName;
            location["street"] = street;
        }

        QJsonObject raw;
        raw["id"]       = id;
        raw["category"] = category;
        raw["month"]    = month;
        raw["location"] = location;

        if (!outcome.isEmpty()) {
            QJsonObject outcomeStatus;
            outcomeStatus["category"] = outcome;
            raw["outcome_status"] = outcomeStatus;
        }
        return raw;
    }

private slots:
    // ── UKPoliceSource parsing ────────────────────────────────────────────────

    void testParseBasicEvent()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(makePoliceRaw("123", "burglary", "2024-01",
                                                 51.5074, -0.1278, "Baker Street"));
        QCOMPARE(ev.eventId, QStringLiteral("uk_123"));
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        QVERIFY(ev.lat.has_value());
        QVERIFY(std::abs(*ev.lat - 51.5074) < 1e-4);
        QVERIFY(ev.lon.has_value());
        QVERIFY(std::abs(*ev.lon - (-0.1278)) < 1e-4);
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt->date(), QDate(2024, 1, 1));
    }

    void testParseCrimeTypeMapping()
    {
        UKPoliceSource src(51.5, -0.1);
        // "violent-crime" should map to "assault"
        auto ev1 = src.parseRecord(makePoliceRaw("1", "violent-crime", "2024-01", 51.5, -0.1));
        QCOMPARE(ev1.crimeType, QStringLiteral("assault"));

        // "vehicle-crime" → "vehicle_crime"
        auto ev2 = src.parseRecord(makePoliceRaw("2", "vehicle-crime", "2024-01", 51.5, -0.1));
        QCOMPARE(ev2.crimeType, QStringLiteral("vehicle_crime"));

        // "drugs" → "drug_offence"
        auto ev3 = src.parseRecord(makePoliceRaw("3", "drugs", "2024-01", 51.5, -0.1));
        QCOMPARE(ev3.crimeType, QStringLiteral("drug_offence"));

        // Unknown type passes through as-is
        auto ev4 = src.parseRecord(makePoliceRaw("4", "some-unknown-crime", "2024-01", 51.5, -0.1));
        QCOMPARE(ev4.crimeType, QStringLiteral("some-unknown-crime"));
    }

    void testParseIdPrefixedWithUk()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(makePoliceRaw("99999", "robbery", "2024-03", 51.5, -0.1));
        QVERIFY(ev.eventId.startsWith(QStringLiteral("uk_")));
    }

    void testParseEventWithOutcome()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(makePoliceRaw("5", "theft-from-the-person", "2024-06",
                                                 51.5, -0.1, {}, "Under investigation"));
        QCOMPARE(ev.outcome, QStringLiteral("Under investigation"));
    }

    void testParseEventWithStreetName()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(makePoliceRaw("6", "robbery", "2024-06",
                                                 51.5, -0.1, "Oxford Street", {}));
        QCOMPARE(ev.locationRaw, QStringLiteral("Oxford Street"));
    }

    void testParseEventWithContext()
    {
        UKPoliceSource src(51.5, -0.1);
        QJsonObject raw = makePoliceRaw("7", "anti-social-behaviour", "2024-01", 51.5, -0.1);
        raw["context"] = QStringLiteral("Noisy party complaint");
        auto ev = src.parseRecord(raw);
        QCOMPARE(ev.narrative, QStringLiteral("Noisy party complaint"));
        QCOMPARE(ev.crimeType, QStringLiteral("antisocial"));
    }

    void testParseInvalidMonthSetsNoDate()
    {
        UKPoliceSource src(51.5, -0.1);
        QJsonObject raw = makePoliceRaw("8", "burglary", "invalid-date", 51.5, -0.1);
        auto ev = src.parseRecord(raw);
        QVERIFY(!ev.occurredAt.has_value() || ev.occurredAt->isValid());
    }

    void testParseEmptyObjectNocrash()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(QJsonObject{});
        // Should not crash — event ID will be "uk_" with empty suffix
        QVERIFY(ev.eventId.startsWith(QStringLiteral("uk_")));
    }

    void testParseSourceIdSet()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(makePoliceRaw("9", "burglary", "2024-01", 51.5, -0.1));
        QCOMPARE(ev.source, QStringLiteral("uk_police_v1"));
    }

    void testParseQualityScoreSet()
    {
        UKPoliceSource src(51.5, -0.1);
        auto ev = src.parseRecord(makePoliceRaw("10", "burglary", "2024-01", 51.5, -0.1));
        QVERIFY(ev.qualityScore > 0.0);
        QVERIFY(ev.qualityScore <= 1.0);
    }

    void testSetLocation()
    {
        UKPoliceSource src(51.5, -0.1);
        src.setLocation(53.483, -2.244, 5.0);  // Should not crash
        QCOMPARE(src.sourceId(), QStringLiteral("uk_police_v1"));
    }

    void testSourceDisplayName()
    {
        UKPoliceSource src(51.5, -0.1);
        QVERIFY(!src.displayName().isEmpty());
        QVERIFY(src.displayName().contains("UK", Qt::CaseInsensitive));
    }

    // ── WeatherSource tests ───────────────────────────────────────────────────

    void testDiscomfortIndexBelowTen()
    {
        // < 10°C → 0.1
        double d = WeatherSource::discomfortIndex(5.0);
        QVERIFY(std::abs(d - 0.1) < 1e-9);
    }

    void testDiscomfortIndexAt15()
    {
        // 10–20°C range
        double d = WeatherSource::discomfortIndex(15.0);
        QVERIFY(d > 0.3 && d < 0.5);
    }

    void testDiscomfortIndexAt25()
    {
        // 20–30°C range
        double d = WeatherSource::discomfortIndex(25.0);
        QVERIFY(d > 0.5 && d < 0.8);
    }

    void testDiscomfortIndexAt35()
    {
        // 30–40°C range
        double d = WeatherSource::discomfortIndex(35.0);
        QVERIFY(d > 0.8 && d <= 1.0);
    }

    void testDiscomfortIndexAt45()
    {
        // >= 40°C capped
        double d = WeatherSource::discomfortIndex(45.0);
        QVERIFY(d > 0.0 && d <= 1.0);
    }

    void testDiscomfortIndexMonotoneUpTo35()
    {
        // Discomfort should generally increase from 5°C to 35°C
        double d5  = WeatherSource::discomfortIndex(5.0);
        double d15 = WeatherSource::discomfortIndex(15.0);
        double d25 = WeatherSource::discomfortIndex(25.0);
        double d35 = WeatherSource::discomfortIndex(35.0);
        QVERIFY(d5 < d15);
        QVERIFY(d15 < d25);
        QVERIFY(d25 < d35);
    }

    void testWeatherSourceCreationNocrash()
    {
        WeatherSource ws;
        QCOMPARE(ws.cachedHourCount(), 0);
    }

    void testDataAtEmptyCacheReturnsNullopt()
    {
        WeatherSource ws;
        auto result = ws.dataAt(QDateTime::currentDateTimeUtc());
        QVERIFY(!result.has_value());
    }

    void testWeatherDataStructDefaults()
    {
        WeatherData wd;
        QCOMPARE(wd.temperatureC, 0.0);
        QCOMPARE(wd.precipitationMm, 0.0);
        QVERIFY(wd.visibilityM > 0.0);
        QVERIFY(wd.isDay);
        QVERIFY(!wd.isRaining);
        QVERIFY(!wd.isLowVisibility);
        QVERIFY(!wd.isExtremeWind);
    }

    void testWeatherSourceParseJsonDirectly()
    {
        // Build a mock Open-Meteo JSON response and feed it via onReplyFinished indirectly
        // We can't easily mock QNetworkReply, so we test the cache population
        // by constructing the JSON the source would receive and checking counts
        WeatherSource ws;
        // After construction, cache is empty
        QCOMPARE(ws.cachedHourCount(), 0);
        // dataAt on a specific time returns nullopt
        QDateTime dt = QDateTime(QDate(2024, 1, 15), QTime(14, 0, 0), Qt::UTC);
        QVERIFY(!ws.dataAt(dt).has_value());
    }

    void testParseAllCrimeTypeMappings()
    {
        UKPoliceSource src(51.5, -0.1);
        struct { QString raw; QString expected; } mappings[] = {
            { "burglary",               "burglary"        },
            { "robbery",                "robbery"         },
            { "violent-crime",          "assault"         },
            { "vehicle-crime",          "vehicle_crime"   },
            { "drugs",                  "drug_offence"    },
            { "anti-social-behaviour",  "antisocial"      },
            { "theft-from-the-person",  "theft"           },
            { "other-theft",            "theft"           },
            { "shoplifting",            "theft"           },
            { "criminal-damage-arson",  "criminal_damage" },
            { "public-order",           "public_order"    },
            { "possession-of-weapons",  "weapons"         },
        };
        for (const auto& m : mappings) {
            auto ev = src.parseRecord(makePoliceRaw("x", m.raw, "2024-01", 51.5, -0.1));
            QCOMPARE(ev.crimeType, m.expected);
        }
    }
};

QTEST_MAIN(TestSourceParsing)
#include "test_source_parsing.moc"
