// test_core_nlp.cpp — SENTINEL unit tests
// Covers: CrimeEvent struct, Database CRUD, MOExtractor patterns, CrimeClassifier
// Build via CMake sentinel_test(test_core_nlp test_core_nlp.cpp)

#include <QTest>
#include <QCoreApplication>
#include <QDateTime>
#include <QDate>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "core/CrimeEvent.h"
#include "core/AppConfig.h"
#include "core/Database.h"
#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers
// ─────────────────────────────────────────────────────────────────────────────

static AppConfig inMemoryConfig()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    return cfg;
}

static CrimeEvent makeEvent(const QString& id,
                             const QString& crimeType,
                             const QDateTime& occurred = {})
{
    CrimeEvent ev;
    ev.eventId    = id;
    ev.crimeType  = crimeType;
    ev.ingestedAt = QDateTime::currentDateTimeUtc();
    if (occurred.isValid())
        ev.occurredAt = occurred;
    return ev;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestCrimeEvent — struct field and optional semantics
// ─────────────────────────────────────────────────────────────────────────────

class TestCrimeEvent : public QObject
{
    Q_OBJECT
private slots:

    void testDefaultConstruct()
    {
        CrimeEvent ev;
        QVERIFY(ev.eventId.isEmpty());
        QVERIFY(ev.crimeType.isEmpty());
        QVERIFY(ev.suburb.isEmpty());
        QVERIFY(ev.outcome.isEmpty());
        QVERIFY(!ev.occurredAt.has_value());
        QVERIFY(!ev.reportedAt.has_value());
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
        QVERIFY(!ev.locationRaw.has_value());
        QVERIFY(!ev.narrative.has_value());
        QVERIFY(!ev.weapon.has_value());
        QVERIFY(!ev.crimeSubtype.has_value());
        QVERIFY(!ev.lga.has_value());
        QVERIFY(!ev.addressNormalised.has_value());
        QVERIFY(!ev.conviction.has_value());
        QVERIFY(!ev.suspectCount.has_value());
        QVERIFY(!ev.victimCount.has_value());
        // Flat convenience fields
        QCOMPARE(ev.latitude,  0.0);
        QCOMPARE(ev.longitude, 0.0);
        // Default quality score
        QCOMPARE(ev.qualityScore, 0.5);
    }

    void testConvenienceFieldsLatLon()
    {
        CrimeEvent ev;
        ev.latitude  = 51.5074;
        ev.longitude = -0.1278;
        QVERIFY(qAbs(ev.latitude  - 51.5074) < 1e-9);
        QVERIFY(qAbs(ev.longitude - (-0.1278)) < 1e-9);
    }

    void testOptionalLatLonHasValue()
    {
        CrimeEvent ev;
        ev.lat = 51.5;
        ev.lon = -0.1;
        QVERIFY(ev.lat.has_value());
        QVERIFY(ev.lon.has_value());
        QVERIFY(qAbs(*ev.lat  - 51.5)  < 1e-9);
        QVERIFY(qAbs(*ev.lon  - (-0.1)) < 1e-9);
        // value_or returns stored value when present
        QVERIFY(qAbs(ev.lat.value_or(0.0)  - 51.5)  < 1e-9);
        QVERIFY(qAbs(ev.lon.value_or(0.0)  - (-0.1)) < 1e-9);
    }

    void testOptionalLatLonFallback()
    {
        CrimeEvent ev;
        QVERIFY(!ev.lat.has_value());
        QVERIFY(!ev.lon.has_value());
        QCOMPARE(ev.lat.value_or(0.0),   0.0);
        QCOMPARE(ev.lon.value_or(0.0),   0.0);
        QCOMPARE(ev.lat.value_or(-999.0), -999.0);
    }

    void testOccurredAtSet()
    {
        CrimeEvent ev;
        const QDateTime dt = QDateTime(QDate(2025, 3, 14), QTime(15, 9, 26), Qt::UTC);
        ev.occurredAt = dt;
        QVERIFY(ev.occurredAt.has_value());
        QCOMPARE(*ev.occurredAt, dt);
    }

    void testOccurredAtFallback()
    {
        CrimeEvent ev;
        ev.ingestedAt = QDateTime(QDate(2025, 1, 15), QTime(10, 0, 0), Qt::UTC);
        // No occurredAt set — value_or falls back to ingestedAt
        QVERIFY(!ev.occurredAt.has_value());
        QCOMPARE(ev.occurredAt.value_or(ev.ingestedAt), ev.ingestedAt);
    }

    void testLocationRaw()
    {
        CrimeEvent ev;
        ev.locationRaw = QStringLiteral("High Street, London");
        QVERIFY(ev.locationRaw.has_value());
        QCOMPARE(*ev.locationRaw, QStringLiteral("High Street, London"));
        QCOMPARE(ev.locationRaw.value_or(QString{}), QStringLiteral("High Street, London"));
    }

    void testCrimeTypeAssignment()
    {
        CrimeEvent ev;
        ev.crimeType = QStringLiteral("burglary");
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        // crimeType is a plain QString, not optional
        ev.crimeType = {};
        QVERIFY(ev.crimeType.isEmpty());
    }

    void testQualityScoreDefault()
    {
        const CrimeEvent ev;
        QCOMPARE(ev.qualityScore, 0.5);
    }

    void testQualityScoreAssignment()
    {
        CrimeEvent ev;
        ev.qualityScore = 0.95;
        QVERIFY(qAbs(ev.qualityScore - 0.95) < 1e-9);
    }

    void testNarrativeOptional()
    {
        CrimeEvent ev;
        QVERIFY(!ev.narrative.has_value());
        ev.narrative = QStringLiteral("Suspect fled northbound on foot.");
        QVERIFY(ev.narrative.has_value());
        QCOMPARE(*ev.narrative, QStringLiteral("Suspect fled northbound on foot."));
    }

    void testWeaponOptional()
    {
        CrimeEvent ev;
        QVERIFY(!ev.weapon.has_value());
        ev.weapon = QStringLiteral("knife");
        QVERIFY(ev.weapon.has_value());
        QCOMPARE(*ev.weapon, QStringLiteral("knife"));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestDatabase — CRUD, leads, audit, stats using :memory: SQLite
// ─────────────────────────────────────────────────────────────────────────────

class TestDatabase : public QObject
{
    Q_OBJECT

private:
    Database* m_db = nullptr;

private slots:

    // Called before every test method — provides a clean in-memory database.
    void init()
    {
        m_db = new Database(inMemoryConfig());
        QVERIFY(m_db->open());
    }

    // Called after every test method.
    void cleanup()
    {
        delete m_db;
        m_db = nullptr;
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    void testOpenClose()
    {
        QVERIFY(m_db->isOpen());
        // Calling open() on an already-open DB is idempotent
        QVERIFY(m_db->open());
        m_db->close();
        QVERIFY(!m_db->isOpen());
        // cleanup() will call close() again — must be idempotent
    }

    void testEmptyDb()
    {
        QCOMPARE(m_db->eventCount(), 0);
        QCOMPARE(m_db->getTotalEventCount(), 0);
        const auto events = m_db->queryEvents();
        QVERIFY(events.isEmpty());
        const auto counts = m_db->crimeTypeCounts();
        QVERIFY(counts.isEmpty());
        const auto leads = m_db->queryLeads();
        QVERIFY(leads.isEmpty());
    }

    // ── Event CRUD ─────────────────────────────────────────────────────────

    void testInsertAndCount()
    {
        QCOMPARE(m_db->eventCount(), 0);
        QVERIFY(m_db->insertEvent(makeEvent("e1", "burglary")));
        QVERIFY(m_db->insertEvent(makeEvent("e2", "robbery")));
        QVERIFY(m_db->insertEvent(makeEvent("e3", "assault")));
        QCOMPARE(m_db->eventCount(), 3);
    }

    void testInsertAndQueryById()
    {
        CrimeEvent ev = makeEvent("findme-001", "drug_offence");
        ev.suburb      = QStringLiteral("Hackney");
        ev.qualityScore = 0.9;
        ev.lat = 51.545;
        ev.lon = -0.055;

        QVERIFY(m_db->insertEvent(ev));

        const CrimeEvent found = m_db->eventById(QStringLiteral("findme-001"));
        QCOMPARE(found.eventId,   QStringLiteral("findme-001"));
        QCOMPARE(found.crimeType, QStringLiteral("drug_offence"));
        QCOMPARE(found.suburb,    QStringLiteral("Hackney"));
        QVERIFY(qAbs(found.qualityScore - 0.9) < 1e-6);
        QVERIFY(found.lat.has_value());
        QVERIFY(qAbs(*found.lat - 51.545) < 1e-9);
    }

    void testQueryByIdNotFound()
    {
        const CrimeEvent missing = m_db->eventById(QStringLiteral("does-not-exist"));
        QVERIFY(missing.eventId.isEmpty());
    }

    void testQueryByType()
    {
        QVERIFY(m_db->insertEvent(makeEvent("b1", "burglary")));
        QVERIFY(m_db->insertEvent(makeEvent("b2", "burglary")));
        QVERIFY(m_db->insertEvent(makeEvent("r1", "robbery")));

        const auto burglaries = m_db->queryEvents(QStringLiteral("burglary"));
        QCOMPARE((int)burglaries.size(), 2);
        for (const CrimeEvent& e : burglaries)
            QCOMPARE(e.crimeType, QStringLiteral("burglary"));

        const auto robberies = m_db->queryEvents(QStringLiteral("robbery"));
        QCOMPARE((int)robberies.size(), 1);
        QCOMPARE(robberies[0].crimeType, QStringLiteral("robbery"));
    }

    void testQueryByDateRange()
    {
        const QDateTime jan(QDate(2025, 1, 15), QTime(12, 0, 0), Qt::UTC);
        const QDateTime jun(QDate(2025, 6, 15), QTime(12, 0, 0), Qt::UTC);
        const QDateTime dec(QDate(2025, 12, 15), QTime(12, 0, 0), Qt::UTC);

        QVERIFY(m_db->insertEvent(makeEvent("d-jan", "burglary", jan)));
        QVERIFY(m_db->insertEvent(makeEvent("d-jun", "robbery",  jun)));
        QVERIFY(m_db->insertEvent(makeEvent("d-dec", "assault",  dec)));

        const QDateTime from(QDate(2025,  1,  1), QTime(0, 0, 0), Qt::UTC);
        const QDateTime to  (QDate(2025,  7,  1), QTime(0, 0, 0), Qt::UTC);

        const auto results = m_db->queryEvents({}, from, to);
        QCOMPARE((int)results.size(), 2);

        // Only the December event should appear in the second half of the year
        const QDateTime from2(QDate(2025,  7,  2), QTime(0, 0, 0), Qt::UTC);
        const QDateTime to2  (QDate(2025, 12, 31), QTime(23, 59, 59), Qt::UTC);
        const auto lateResults = m_db->queryEvents({}, from2, to2);
        QCOMPARE((int)lateResults.size(), 1);
        QCOMPARE(lateResults[0].crimeType, QStringLiteral("assault"));
    }

    void testUpdateEvent()
    {
        CrimeEvent ev = makeEvent("upd-01", "theft");
        QVERIFY(m_db->insertEvent(ev));

        ev.crimeType = QStringLiteral("robbery");
        ev.suburb    = QStringLiteral("Soho");
        QVERIFY(m_db->updateEvent(ev));  // INSERT OR REPLACE

        const CrimeEvent fetched = m_db->eventById(QStringLiteral("upd-01"));
        QCOMPARE(fetched.crimeType, QStringLiteral("robbery"));
        QCOMPARE(fetched.suburb,    QStringLiteral("Soho"));
    }

    void testGetTotalEventCount()
    {
        QVERIFY(m_db->insertEvent(makeEvent("tc1", "theft")));
        QVERIFY(m_db->insertEvent(makeEvent("tc2", "theft")));
        QCOMPARE(m_db->getTotalEventCount(), 2);
        QCOMPARE(m_db->getTotalEventCount(), m_db->eventCount());
    }

    void testAverageQualityScore()
    {
        CrimeEvent ev1 = makeEvent("qs1", "burglary");
        ev1.qualityScore = 0.8;
        CrimeEvent ev2 = makeEvent("qs2", "robbery");
        ev2.qualityScore = 0.4;

        QVERIFY(m_db->insertEvent(ev1));
        QVERIFY(m_db->insertEvent(ev2));

        const double avg = m_db->getAverageQualityScore();
        QVERIFY(qAbs(avg - 0.6) < 1e-6);
    }

    // ── Leads ──────────────────────────────────────────────────────────────

    void testInsertAndQueryLeads()
    {
        QVERIFY(m_db->insertEvent(makeEvent("ev-lead", "burglary")));

        InvestigativeLead lead;
        lead.rank             = 1;
        lead.category         = QStringLiteral("MO Match");
        lead.headline         = QStringLiteral("Matching entry method detected");
        lead.detail           = QStringLiteral("Forced entry consistent with three prior incidents.");
        lead.confidence       = 0.82;
        lead.confidenceMethod = QStringLiteral("bayesian");
        lead.generatedAt      = QDateTime::currentDateTimeUtc();

        QVERIFY(m_db->insertLead(lead, QStringLiteral("ev-lead")));

        const auto leads = m_db->queryLeads(QStringLiteral("ev-lead"));
        QCOMPARE((int)leads.size(), 1);
        QCOMPARE(leads[0].headline,  QStringLiteral("Matching entry method detected"));
        QCOMPARE(leads[0].category,  QStringLiteral("MO Match"));
        QVERIFY(qAbs(leads[0].confidence - 0.82) < 1e-6);
    }

    void testQueryLeadsForDifferentEvents()
    {
        QVERIFY(m_db->insertEvent(makeEvent("evA", "robbery")));
        QVERIFY(m_db->insertEvent(makeEvent("evB", "assault")));

        InvestigativeLead leadA;
        leadA.rank = 1; leadA.headline = QStringLiteral("Lead for A");
        leadA.generatedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(m_db->insertLead(leadA, QStringLiteral("evA")));

        InvestigativeLead leadB;
        leadB.rank = 1; leadB.headline = QStringLiteral("Lead for B");
        leadB.generatedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(m_db->insertLead(leadB, QStringLiteral("evB")));

        QCOMPARE((int)m_db->queryLeads(QStringLiteral("evA")).size(), 1);
        QCOMPARE((int)m_db->queryLeads(QStringLiteral("evB")).size(), 1);
        QCOMPARE((int)m_db->queryLeads().size(), 2);
    }

    // ── Audit ──────────────────────────────────────────────────────────────

    void testAuditLog()
    {
        QVERIFY(m_db->insertAuditEntry(
            QStringLiteral("audit-ev"),
            QStringLiteral("INSERT"),
            QStringLiteral("Event ingested from CSV")));

        const auto audits = m_db->queryAudit();
        QVERIFY(!audits.isEmpty());

        bool found = false;
        for (const auto& [ts, eventId, action, detail] : audits) {
            if (eventId == QStringLiteral("audit-ev") &&
                action  == QStringLiteral("INSERT") &&
                detail  == QStringLiteral("Event ingested from CSV")) {
                QVERIFY(ts.isValid());
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    void testAuditLogMultipleEntries()
    {
        QVERIFY(m_db->insertAuditEntry("e1", "INSERT", "first"));
        QVERIFY(m_db->insertAuditEntry("e1", "UPDATE", "second"));
        QVERIFY(m_db->insertAuditEntry("e2", "INSERT", "third"));

        const auto all = m_db->queryAudit(10);
        QCOMPARE((int)all.size(), 3);
    }

    // ── Statistics ─────────────────────────────────────────────────────────

    void testCrimeTypeCounts()
    {
        QVERIFY(m_db->insertEvent(makeEvent("cnt-b1", "burglary")));
        QVERIFY(m_db->insertEvent(makeEvent("cnt-b2", "burglary")));
        QVERIFY(m_db->insertEvent(makeEvent("cnt-b3", "burglary")));
        QVERIFY(m_db->insertEvent(makeEvent("cnt-r1", "robbery")));
        QVERIFY(m_db->insertEvent(makeEvent("cnt-r2", "robbery")));

        const auto counts = m_db->crimeTypeCounts();
        QCOMPARE(counts.value(QStringLiteral("burglary")), 3);
        QCOMPARE(counts.value(QStringLiteral("robbery")),  2);
        QVERIFY(!counts.contains(QStringLiteral("assault")));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestMOExtractor — exhaustive pattern matching
// ─────────────────────────────────────────────────────────────────────────────

class TestMOExtractor : public QObject
{
    Q_OBJECT

private:
    MOExtractor m_ex;

private slots:

    // ── Entry method ───────────────────────────────────────────────────────

    void testForcedEntry()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("He smashed the window and forced his way in"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testForcedEntryBroken()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The rear door had been broken open."));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testUnlockedEntry()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The door was left open and unsecured."));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("unlocked"));
    }

    void testDeceptionEntry()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("He pretended to be a plumber and tricked his way in."));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("deception"));
    }

    void testTailgating()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The suspect tailgated through the secure entrance."));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("tailgating"));
    }

    // ── Target type ────────────────────────────────────────────────────────

    void testResidentialTarget()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("Offender broke into the house late at night."));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));
    }

    void testCommercialTarget()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("He robbed the shop at closing time."));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("commercial"));
    }

    void testVehicleTarget()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The car was broken into on the high street."));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("vehicle"));
    }

    void testPersonTarget()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("A pedestrian was accosted near the station."));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("person"));
    }

    // ── Weapon type ────────────────────────────────────────────────────────

    void testFirearmWeapon()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The suspect was seen brandishing a gun."));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));
    }

    void testKnifeWeapon()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("He stabbed the victim in the arm."));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    void testBluntWeapon()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The victim was hit with a bat from behind."));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("blunt"));
    }

    // ── Items taken ────────────────────────────────────────────────────────

    void testItemsTaken()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The offender stole cash, a phone, and a wallet."));
        QVERIFY(mo.itemsTaken.size() == 3u);

        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        QVERIFY(items.contains(QStringLiteral("cash")));
        QVERIFY(items.contains(QStringLiteral("phone")));
        QVERIFY(items.contains(QStringLiteral("wallet")));
    }

    void testItemsDeduplication()
    {
        // "cash" mentioned twice — should appear only once
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("Stole cash from the till and took cash from the safe."));
        QVERIFY(mo.itemsTaken.size() == 1u);
        QCOMPARE(QString::fromStdString(mo.itemsTaken[0].toStdString()),
                 QStringLiteral("cash"));
    }

    void testJewelryNormalization()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The thief stole jewelry and a watch."));
        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        // American spelling must be normalised to British "jewellery"
        QVERIFY(items.contains(QStringLiteral("jewellery")));
        QVERIFY(!items.contains(QStringLiteral("jewelry")));
    }

    void testJewelleryVariant()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("Offender took jewellery and a laptop."));
        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        QVERIFY(items.contains(QStringLiteral("jewellery")));
        QVERIFY(items.contains(QStringLiteral("laptop")));
    }

    // ── Solo / group ───────────────────────────────────────────────────────

    void testSolo()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The offender was acting alone near the property."));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));
    }

    void testGroup()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("A group of three entered through the rear."));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    void testGroupPrecedenceOverSolo()
    {
        // Both "alone" (solo) and "group" appear — group must win.
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("He was not alone; the group split up before entry."));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    void testGangKeyword()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("A gang of offenders was responsible."));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }

    // ── Time of day ────────────────────────────────────────────────────────

    void testEarlyMorning()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The break-in occurred at 3am before dawn."));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("early_morning"));
    }

    void testMorning()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The incident was reported at 9am in the morning."));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("morning"));
    }

    void testEvening()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("Approached the victim at dusk near the park."));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("evening"));
    }

    void testNight()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("Entry was gained at midnight in the dark."));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));
    }

    void testNightKeyword()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("During the late night raid on the premises."));
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));
    }

    // ── Edge cases ─────────────────────────────────────────────────────────

    void testEmptyText()
    {
        const MOFeatures mo = m_ex.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(!mo.victimProfile.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }

    void testNoMatchReturnsNullopt()
    {
        const MOFeatures mo = m_ex.extract(
            QStringLiteral("The weather was pleasant and the sky was blue."));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(mo.itemsTaken.empty());
    }

    // ── canonicalMOString ──────────────────────────────────────────────────

    void testCanonicalMOString()
    {
        MOFeatures mo;
        mo.entryMethod = QStringLiteral("forced_entry");
        mo.targetType  = QStringLiteral("residential");
        mo.timeOfDay   = QStringLiteral("night");
        mo.weaponType  = QStringLiteral("firearm");
        mo.itemsTaken  = { QStringLiteral("cash"), QStringLiteral("jewellery") };
        mo.soloOrGroup = QStringLiteral("solo");

        const QString canonical = m_ex.canonicalMOString(mo);
        QVERIFY(!canonical.isEmpty());
        QVERIFY(canonical.contains(QStringLiteral("forced_entry")));
        QVERIFY(canonical.contains(QStringLiteral("residential")));
        QVERIFY(canonical.contains(QStringLiteral("night")));
        QVERIFY(canonical.contains(QStringLiteral("firearm")));
        QVERIFY(canonical.contains(QStringLiteral("cash")));
        QVERIFY(canonical.contains(QStringLiteral("jewellery")));
        QVERIFY(canonical.contains(QStringLiteral("solo")));
    }

    void testCanonicalMOStringEmpty()
    {
        const MOFeatures mo;  // all nullopt, empty vector
        const QString canonical = m_ex.canonicalMOString(mo);
        QVERIFY(canonical.isEmpty());
    }

    // ── Full scenario ──────────────────────────────────────────────────────

    void testFullScenario()
    {
        // All major features present in a single narrative
        const QString text =
            QStringLiteral("A group broke the door of the house at night. "
                           "The offender was armed with a gun and stole cash and laptop.");

        const MOFeatures mo = m_ex.extract(text);

        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));  // "broke"

        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));    // "house"

        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));           // "night"

        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("firearm"));        // "gun"

        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));         // "group"

        const QStringList items(mo.itemsTaken.begin(), mo.itemsTaken.end());
        QVERIFY(items.contains(QStringLiteral("cash")));
        QVERIFY(items.contains(QStringLiteral("laptop")));

        // canonical string should include all extracted tokens
        const QString canonical = m_ex.canonicalMOString(mo);
        QVERIFY(canonical.contains(QStringLiteral("forced_entry")));
        QVERIFY(canonical.contains(QStringLiteral("residential")));
        QVERIFY(canonical.contains(QStringLiteral("night")));
        QVERIFY(canonical.contains(QStringLiteral("firearm")));
        QVERIFY(canonical.contains(QStringLiteral("group")));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestCrimeClassifier — keyword scoring, severity, sentiment, threat signal
// ─────────────────────────────────────────────────────────────────────────────

class TestCrimeClassifier : public QObject
{
    Q_OBJECT

private:
    CrimeClassifier m_clf;

private slots:

    // ── classify() ─────────────────────────────────────────────────────────

    void testBurglaryClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "The burglar forced entry and ransacked the house; stolen items found."));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY(conf > 0.0);
    }

    void testRobberyClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "He robbed the victim at gunpoint and demanded her bag."));
        // "robbed" (3) + "demanded" (2) = robbery; confidence must be positive
        QCOMPARE(type, QStringLiteral("robbery"));
        QVERIFY(conf > 0.0);
    }

    void testDrugClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "cocaine heroin drugs dealing possession"));
        QCOMPARE(type, QStringLiteral("drug_offence"));
        QVERIFY(conf > 0.5);
    }

    void testVehicleCrimeClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "stolen car vehicle break-in window smashed on driveway"));
        QCOMPARE(type, QStringLiteral("vehicle_crime"));
        QVERIFY(conf > 0.0);
    }

    void testWeaponsClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "armed with a knife and a firearm carrying a gun blade"));
        QCOMPARE(type, QStringLiteral("weapons"));
        QVERIFY(conf > 0.5);
    }

    void testFraudClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "online fraud scam phishing identity impersonation"));
        QCOMPARE(type, QStringLiteral("fraud"));
        QVERIFY(conf > 0.5);
    }

    void testEmptyText()
    {
        // Must not crash and must return a defined result
        const auto [type, conf] = m_clf.classify(QStringLiteral(""));
        QVERIFY(!type.isEmpty());
        QVERIFY(conf >= 0.0);
    }

    void testConfidenceRange()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral(
                "The burglar burglary broke entry house home stolen forced"));
        QVERIFY(conf >= 0.0);
        QVERIFY(conf <= 1.0);
    }

    void testAssaultClassification()
    {
        const auto [type, conf] =
            m_clf.classify(QStringLiteral("assault attack fight punch violence"));
        QCOMPARE(type, QStringLiteral("assault"));
        QVERIFY(conf > 0.5);
    }

    // ── severityScore() ────────────────────────────────────────────────────

    void testMurderSeverity()
    {
        // Base severity for "murder" is 1.0; boost words keep it at 1.0
        const double sev =
            m_clf.severityScore(
                QStringLiteral("murder victim found dead at the scene"),
                QStringLiteral("murder"));
        QCOMPARE(sev, 1.0);
    }

    void testAssaultSeverity()
    {
        // Base for "assault" is 0.8; no boost words → exactly 0.8
        const double sev =
            m_clf.severityScore(
                QStringLiteral("assault fight victim"),
                QStringLiteral("assault"));
        QVERIFY(qAbs(sev - 0.8) < 1e-9);
    }

    void testSeverityBoostFromKeyword()
    {
        // Adding "gun" (a boost word) should raise burglary base (0.6) by 0.05
        const double noBoost  = m_clf.severityScore(
            QStringLiteral("burglary broke house entry stolen"), QStringLiteral("burglary"));
        const double withBoost = m_clf.severityScore(
            QStringLiteral("burglary broke house entry stolen armed gun"), QStringLiteral("burglary"));
        QVERIFY(withBoost > noBoost);
    }

    void testSeverityCapAt1()
    {
        // Even with multiple boost words the score must not exceed 1.0
        const double sev = m_clf.severityScore(
            QStringLiteral("murder kill dead death gun firearm bomb stab armed"),
            QStringLiteral("murder"));
        QVERIFY(sev <= 1.0);
    }

    void testSeverityUnknownTypeFallback()
    {
        // Unknown crime type uses the default base of 0.3
        const double sev = m_clf.severityScore(
            QStringLiteral("some unrelated text"), QStringLiteral("unknown_type"));
        QVERIFY(sev >= 0.3);  // 0.3 base + possible boost
    }

    // ── sentiment() ────────────────────────────────────────────────────────

    void testNegativeSentiment()
    {
        const double sent = m_clf.sentiment(
            QStringLiteral("violent attack murder stab shot dead beaten"));
        QVERIFY(sent < -0.3);
    }

    void testPositiveSentiment()
    {
        const double sent = m_clf.sentiment(
            QStringLiteral("arrested caught prevented rescued apprehended convicted"));
        QVERIFY(sent > 0.2);
    }

    void testSentimentNeutral()
    {
        // No sentiment words → returns 0.0 (no signal)
        const double sent = m_clf.sentiment(
            QStringLiteral("the incident occurred at noon today near the park"));
        QCOMPARE(sent, 0.0);
    }

    void testSentimentEmptyText()
    {
        const double sent = m_clf.sentiment(QStringLiteral(""));
        QCOMPARE(sent, 0.0);
    }

    void testSentimentRange()
    {
        const double s1 = m_clf.sentiment(
            QStringLiteral("violent murder attack kill stab"));
        const double s2 = m_clf.sentiment(
            QStringLiteral("arrested resolved safe peaceful helped"));
        QVERIFY(s1 >= -1.0 && s1 <= 1.0);
        QVERIFY(s2 >= -1.0 && s2 <= 1.0);
        QVERIFY(s1 < s2);  // negative text scores lower than positive
    }

    // ── threatSignal() ─────────────────────────────────────────────────────

    void testThreatSignalTrue()
    {
        // Strong negative sentiment + threat keywords → true
        const QString text =
            QStringLiteral("kill threat bomb attack murder violent crime shoot stab");
        const double sent = m_clf.sentiment(text);
        QVERIFY(sent < -0.5);
        QVERIFY(m_clf.threatSignal(text, sent));
    }

    void testThreatSignalFalse()
    {
        // Positive sentiment → no threat
        const QString text =
            QStringLiteral("police arrested suspect in connection with theft");
        const double sent = m_clf.sentiment(text);
        QVERIFY(!m_clf.threatSignal(text, sent));
    }

    void testThreatSignalRequiresNegativeSentiment()
    {
        // Threat keywords present but sentiment is not sufficiently negative
        const QString text = QStringLiteral("kill attack bomb");
        // Force a non-negative sentiment score to prove gate condition
        QVERIFY(!m_clf.threatSignal(text, 0.0));   // > -0.5 → false
        QVERIFY(!m_clf.threatSignal(text, -0.49)); // > -0.5 → false
        // At -0.5 the condition is > -0.5 (strict), so -0.5 is still false
        QVERIFY(!m_clf.threatSignal(text, -0.5));
        // Below -0.5 with threat keyword → true
        QVERIFY(m_clf.threatSignal(text, -0.51));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main — run all four test classes
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { "test", "-o", QString("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestCrimeEvent      t1; r |= runTest(&t1, "core_event.txt");
    TestDatabase        t2; r |= runTest(&t2, "core_db.txt");
    TestMOExtractor     t3; r |= runTest(&t3, "core_moex.txt");
    TestCrimeClassifier t4; r |= runTest(&t4, "core_clf.txt");
    return r;
}

#include "test_core_nlp.moc"
