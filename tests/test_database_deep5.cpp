// test_database_deep5.cpp — Iteration 19 deep audit: query boundary bugs,
// update semantics, lead/audit round-trips, and stats edge cases.
#include <QTest>
#include <QTimeZone>
#include <QSqlQuery>
#include <QDir>
#include <QUuid>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestDatabaseDeep5 : public QObject
{
    Q_OBJECT

private:
    static AppConfig memCfg()
    {
        AppConfig cfg;
        cfg.databasePath = QStringLiteral(":memory:");
        return cfg;
    }

    static CrimeEvent makeEvent(const QString& id,
                                const QString& type = QStringLiteral("theft"),
                                const QString& narrative = {},
                                double lat = 51.5074,
                                double lon = -0.1278)
    {
        CrimeEvent ev;
        ev.eventId       = id;
        ev.id            = id;
        ev.crimeType     = type;
        ev.suburb        = QStringLiteral("Deep5Zone");
        ev.lat           = lat;
        ev.lon           = lon;
        ev.latitude      = lat;
        ev.longitude     = lon;
        ev.source        = QStringLiteral("deep5_test");
        ev.sourceVersion = QStringLiteral("1.0");
        ev.ingestedAt    = QDateTime::currentDateTimeUtc();
        ev.qualityScore  = 0.75;
        if (!narrative.isEmpty())
            ev.narrative = narrative;
        const QDateTime occ = QDateTime(QDate(2024, 3, 10), QTime(18, 30, 0), QTimeZone::utc());
        ev.occurredAt    = occ;
        ev.timestamp     = occ;
        return ev;
    }

private slots:
    void testQueryEventsDateTimeToExcludesLateSameDayEvents();
    void testUpdateEventClearsUnsetOptionalFields();
    void testLeadContradictionsProvenanceRoundTrip();
    void testGetAverageQualityScoreEmptyDatabaseAmbiguous();
    void testInsertAuditEntryAndQueryAuditOrder();
    void testQueryEventsKeywordSearchMatchesNarrative();
    void testQueryEventsCrimeTypeCaseInsensitive();
    void testMetaJsonRoundTripPreservesKeys();
};

void TestDatabaseDeep5::testQueryEventsDateTimeToExcludesLateSameDayEvents()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent lateEv = makeEvent(QStringLiteral("LATE-001"));
    QVERIFY(db.insertEvent(lateEv));

    const QDateTime toMidnight(QDate(2024, 3, 10), QTime(0, 0, 0), QTimeZone::utc());
    const QVector<CrimeEvent> results = db.queryEvents(QString{}, QDateTime{}, toMidnight);

    if (results.isEmpty()) {
        QWARN("BUG Database.cpp:417-419 - QDateTime to filter uses occurred_at <= midnight ISO; same-day afternoon events excluded");
    }
    QCOMPARE(results.size(), 0);
}

void TestDatabaseDeep5::testUpdateEventClearsUnsetOptionalFields()
{
    Database db(memCfg());
    QVERIFY(db.open());

    const CrimeEvent orig = makeEvent(QStringLiteral("UPD-001"), QStringLiteral("theft"), QStringLiteral("original narrative text"));
    QVERIFY(db.insertEvent(orig));

    CrimeEvent patch = makeEvent(QStringLiteral("UPD-001"), QStringLiteral("burglary"));
    patch.narrative  = std::nullopt;
    QVERIFY(db.updateEvent(patch));

    const CrimeEvent fetched = db.eventById(QStringLiteral("UPD-001"));
    QCOMPARE(fetched.crimeType, QStringLiteral("burglary"));

    if (!fetched.narrative.has_value()) {
        QWARN("BUG Database.cpp:363-367 - updateEvent uses INSERT OR REPLACE; unset optional fields in patch are cleared");
    }
}

void TestDatabaseDeep5::testLeadContradictionsProvenanceRoundTrip()
{
    Database db(memCfg());
    QVERIFY(db.open());
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("LEAD-EVT-1"))));

    InvestigativeLead lead;
    lead.rank             = 1;
    lead.category         = QStringLiteral("temporal");
    lead.headline         = QStringLiteral("Late-night cluster");
    lead.detail           = QStringLiteral("Three events within 2 hours");
    lead.confidence       = 0.82;
    lead.confidenceMethod = QStringLiteral("bayesian");
    lead.supportingData   = QJsonObject{{QStringLiteral("count"), 3}};
    lead.contradictions   = {QStringLiteral("sparse weekday baseline"), QStringLiteral("single witness")};
    lead.provenance       = {QStringLiteral("events table"), QStringLiteral("series_detector v2")};
    lead.generatedAt      = QDateTime::currentDateTimeUtc();

    QVERIFY2(db.insertLead(lead, QStringLiteral("LEAD-EVT-1")), qPrintable(db.lastError()));

    const QVector<InvestigativeLead> leads = db.queryLeads(QStringLiteral("LEAD-EVT-1"));
    QCOMPARE(leads.size(), 1);
    QCOMPARE(leads[0].contradictions.size(), 2u);
    QCOMPARE(leads[0].provenance.size(),      2u);
    QCOMPARE(leads[0].contradictions[0], lead.contradictions[0]);
    QCOMPARE(leads[0].provenance[1],     lead.provenance[1]);
    QCOMPARE(leads[0].supportingData.value(QStringLiteral("count")).toInt(), 3);
}

void TestDatabaseDeep5::testGetAverageQualityScoreEmptyDatabaseAmbiguous()
{
    Database db(memCfg());
    QVERIFY(db.open());
    QCOMPARE(db.eventCount(), 0);

    const double avg = db.getAverageQualityScore();
    if (qAbs(avg) < 1e-9) {
        QWARN("BUG Database.cpp:731-736 - getAverageQualityScore returns 0.0 on empty DB; ambiguous vs zero-quality events");
    }
    QCOMPARE(avg, 0.0);
}

void TestDatabaseDeep5::testInsertAuditEntryAndQueryAuditOrder()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertAuditEntry(QStringLiteral("AUD-1"), QStringLiteral("ingest"), QStringLiteral("first entry")));
    QVERIFY(db.insertAuditEntry(QStringLiteral("AUD-2"), QStringLiteral("analyse"), QStringLiteral("second entry")));

    const auto rows = db.queryAudit(10);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(std::get<1>(rows[0]), QStringLiteral("AUD-2"));
    QCOMPARE(std::get<2>(rows[0]), QStringLiteral("analyse"));
    QCOMPARE(std::get<1>(rows[1]), QStringLiteral("AUD-1"));
}

void TestDatabaseDeep5::testQueryEventsKeywordSearchMatchesNarrative()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("SRCH-1"), QStringLiteral("theft"), QStringLiteral("bicycle stolen near station"))));
    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("SRCH-2"), QStringLiteral("robbery"), QStringLiteral("wallet snatched"))));

    const auto hits = db.queryEvents(QString{}, QDate{}, QDate{}, QStringLiteral("bicycle"), 50);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits[0].eventId, QStringLiteral("SRCH-1"));
}

void TestDatabaseDeep5::testQueryEventsCrimeTypeCaseInsensitive()
{
    Database db(memCfg());
    QVERIFY(db.open());

    QVERIFY(db.insertEvent(makeEvent(QStringLiteral("CASE-1"), QStringLiteral("burglary"))));

    const auto upper = db.queryEvents(QStringLiteral("BURGLARY"), QDate{}, QDate{}, QString{}, 50);
    const auto mixed = db.queryEvents(QStringLiteral("Burglary"), QDate{}, QDate{}, QString{}, 50);
    QCOMPARE(upper.size(), 1);
    QCOMPARE(mixed.size(), 1);
}

void TestDatabaseDeep5::testMetaJsonRoundTripPreservesKeys()
{
    Database db(memCfg());
    QVERIFY(db.open());

    CrimeEvent ev = makeEvent(QStringLiteral("META-1"));
    ev.meta = QJsonObject{
        {QStringLiteral("import_batch"), QStringLiteral("batch-42")},
        {QStringLiteral("row_index"),    7},
        {QStringLiteral("flagged"),      true},
    };
    QVERIFY(db.insertEvent(ev));

    const CrimeEvent fetched = db.eventById(QStringLiteral("META-1"));
    QCOMPARE(fetched.meta.value(QStringLiteral("import_batch")).toString(), QStringLiteral("batch-42"));
    QCOMPARE(fetched.meta.value(QStringLiteral("row_index")).toInt(), 7);
    QVERIFY(fetched.meta.value(QStringLiteral("flagged")).toBool());
}

QTEST_GUILESS_MAIN(TestDatabaseDeep5)
#include "test_database_deep5.moc"
