// test_database_deep7.cpp — Deep audit iteration 30: Database
// location_raw search, crimeTypeCounts, insertLead query, getHourlyCounts shape.
#include <QTest>
#include <QTimeZone>
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseDeep7 : public QObject
{
    Q_OBJECT

    static AppConfig memCfg()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type,
                                const QString& location = {})
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("Deep7");
        ev.locationRaw  = location;
        ev.lat          = 51.5;
        ev.lon          = -0.1;
        ev.latitude     = 51.5;
        ev.longitude    = -0.1;
        ev.source       = QStringLiteral("deep7_test");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = QDateTime(QDate(2024, 6, 15), QTime(14, 0), QTimeZone::utc());
        ev.timestamp    = ev.occurredAt.value();
        ev.qualityScore = 0.75;
        return ev;
    }

private slots:

    void testLocationRawKeywordSearch()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("LOC-A"), QStringLiteral("theft"),
                                         QStringLiteral("Camden High Street shop"))));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("LOC-B"), QStringLiteral("theft"),
                                         QStringLiteral("Other area"))));

        const auto hits = db.queryEvents(QString{}, QDate(2024, 1, 1), QDate(2024, 12, 31),
                                         QStringLiteral("Camden"), 100);
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().eventId, QStringLiteral("LOC-A"));
    }

    void testCrimeTypeCountsAggregates()
    {
        Database db(memCfg());
        QVERIFY(db.open());

        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("C1"), QStringLiteral("burglary"))));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("C2"), QStringLiteral("burglary"))));
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("C3"), QStringLiteral("theft"))));

        const auto counts = db.crimeTypeCounts();
        QCOMPARE(counts.value(QStringLiteral("burglary")), 2);
        QCOMPARE(counts.value(QStringLiteral("theft")), 1);
    }

    void testInsertLeadQueryable()
    {
        Database db(memCfg());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("LEAD-EV"), QStringLiteral("assault"))));

        InvestigativeLead lead;
        lead.rank       = 1;
        lead.category   = QStringLiteral("mo_similarity");
        lead.headline   = QStringLiteral("MO match");
        lead.confidence = 0.7;

        QVERIFY(db.insertLead(lead, QStringLiteral("LEAD-EV")));
        const auto leads = db.queryLeads(QStringLiteral("LEAD-EV"));
        QCOMPARE(leads.size(), 1);
        QCOMPARE(leads.first().category, QStringLiteral("mo_similarity"));
    }

    void testGetHourlyCountsTwentyFourSlots()
    {
        Database db(memCfg());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("H1"), QStringLiteral("theft"))));

        const auto hourly = db.getHourlyCounts(30);
        QCOMPARE(hourly.size(), 24);
    }

    void testDeleteEventRemovesRow()
    {
        Database db(memCfg());
        QVERIFY(db.open());
        QVERIFY(db.insertEvent(makeEvent(QStringLiteral("DEL-7"), QStringLiteral("theft"))));
        QCOMPARE(db.eventCount(), 1);
        QVERIFY(db.deleteEvent(QStringLiteral("DEL-7")));
        QCOMPARE(db.eventCount(), 0);
    }

    void testQualityThresholdFiltersAnalysisQueries()
    {
        AppConfig cfg = memCfg();
        cfg.qualityThreshold = 0.6;
        Database db(cfg);
        QVERIFY(db.open());
        QCOMPARE(db.qualityThreshold(), 0.6);

        CrimeEvent hi = makeEvent(QStringLiteral("Q-HI"), QStringLiteral("theft"));
        hi.qualityScore = 0.85;
        CrimeEvent lo = makeEvent(QStringLiteral("Q-LO"), QStringLiteral("theft"));
        lo.qualityScore = 0.25;
        QVERIFY(db.insertEvent(hi));
        QVERIFY(db.insertEvent(lo));
        QCOMPARE(db.eventCount(), 2);

        const auto filtered = db.getAllEvents();
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first().eventId, QStringLiteral("Q-HI"));

        db.setQualityThreshold(0.2);
        QCOMPARE(db.getAllEvents().size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestDatabaseDeep7)
#include "test_database_deep7.moc"
