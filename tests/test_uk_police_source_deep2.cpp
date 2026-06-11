// test_uk_police_source_deep2.cpp
// Deep tests of UKPoliceSource JSON parsing and URL construction — no network access.
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "ingest/UKPoliceSource.h"
#include "core/CrimeEvent.h"
#include <cmath>

class UKPoliceSourceDeep2Test : public QObject
{
    Q_OBJECT

private:
    UKPoliceSource* m_src = nullptr;

    // Build a UK Police API crime record JSON object
    static QJsonObject makeCrimeRecord(
        const QString& id,
        const QString& category,
        const QString& month,
        const QString& lat       = QString(),
        const QString& lon       = QString(),
        const QString& streetName = QString(),
        const QString& outcome   = QString())
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]       = id;
        obj[QStringLiteral("category")] = category;
        obj[QStringLiteral("month")]    = month;

        QJsonObject locObj;
        if (!lat.isEmpty())  locObj[QStringLiteral("latitude")]  = lat;
        if (!lon.isEmpty())  locObj[QStringLiteral("longitude")] = lon;
        if (!streetName.isEmpty()) {
            QJsonObject street;
            street[QStringLiteral("id")]   = 1;
            street[QStringLiteral("name")] = streetName;
            locObj[QStringLiteral("street")] = street;
        }
        obj[QStringLiteral("location")] = locObj;

        if (!outcome.isEmpty()) {
            QJsonObject outcomeStatus;
            outcomeStatus[QStringLiteral("category")] = outcome;
            outcomeStatus[QStringLiteral("date")]     = month;
            obj[QStringLiteral("outcome_status")] = outcomeStatus;
        }
        return obj;
    }

private slots:

    void initTestCase()
    {
        m_src = new UKPoliceSource(51.5, -0.1, 1.0, this);
    }

    void cleanupTestCase()
    {
        delete m_src;
        m_src = nullptr;
    }

    // ── 1. parseRecord: crimeType is non-empty for a known category ───────────
    void testParseCrimeTypeNonEmpty()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("1"), QStringLiteral("burglary"),
                            QStringLiteral("2024-03"),
                            QStringLiteral("51.5074"), QStringLiteral("-0.1278")));
        QVERIFY2(!ev.crimeType.isEmpty(), "crimeType must be set from category");
    }

    // ── 2. parseRecord: "burglary" maps to canonical "burglary" ───────────────
    void testParseCrimeTypeMappingBurglary()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("2"), QStringLiteral("burglary"),
                            QStringLiteral("2024-01"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1")));
        QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
    }

    // ── 3. parseRecord: "violent-crime" maps to "assault" ────────────────────
    void testParseCrimeTypeMappingAssault()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("3"), QStringLiteral("violent-crime"),
                            QStringLiteral("2024-02"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1")));
        QCOMPARE(ev.crimeType, QStringLiteral("assault"));
    }

    // ── 4. parseRecord: unknown category is preserved verbatim ───────────────
    void testParseCrimeTypeUnknownPreserved()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("4"), QStringLiteral("custom-unknown-99"),
                            QStringLiteral("2024-03"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1")));
        QCOMPARE(ev.crimeType, QStringLiteral("custom-unknown-99"));
    }

    // ── 5. parseRecord: lat/lon parsed to correct doubles ────────────────────
    void testParseLatLon()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("10"), QStringLiteral("robbery"),
                            QStringLiteral("2024-04"),
                            QStringLiteral("51.5074"), QStringLiteral("-0.1278")));
        QVERIFY2(ev.lat.has_value(), "lat optional must be set");
        QVERIFY2(ev.lon.has_value(), "lon optional must be set");
        QVERIFY2(std::abs(ev.lat.value() - 51.5074) < 0.0001,
                 qPrintable(QStringLiteral("lat %1 should be ~51.5074").arg(ev.lat.value())));
        QVERIFY2(std::abs(ev.lon.value() - (-0.1278)) < 0.0001,
                 qPrintable(QStringLiteral("lon %1 should be ~-0.1278").arg(ev.lon.value())));
    }

    // ── 6. parseRecord: missing lat/lon leaves optional fields unset ──────────
    void testParseMissingLatLonOptionalUnset()
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]       = QStringLiteral("99");
        obj[QStringLiteral("category")] = QStringLiteral("drugs");
        obj[QStringLiteral("month")]    = QStringLiteral("2024-05");
        obj[QStringLiteral("location")] = QJsonObject{};  // empty location

        const auto ev = m_src->parseRecord(obj);
        QVERIFY2(!ev.lat.has_value(), "lat should be unset when location is empty");
        QVERIFY2(!ev.lon.has_value(), "lon should be unset when location is empty");
    }

    // ── 7. parseRecord: empty JSON object does not crash ─────────────────────
    void testParseEmptyObjectNoCrash()
    {
        const auto ev = m_src->parseRecord(QJsonObject{});
        Q_UNUSED(ev);
        QVERIFY(true);  // reaching here means no crash
    }

    // ── 8. parseRecord: eventId carries "uk_" prefix ─────────────────────────
    void testParseEventIdPrefix()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("42abc"), QStringLiteral("shoplifting"),
                            QStringLiteral("2024-06"),
                            QStringLiteral("52.0"), QStringLiteral("-1.0")));
        QVERIFY2(ev.eventId.startsWith(QStringLiteral("uk_")),
                 qPrintable(QStringLiteral("eventId '%1' must start with 'uk_'").arg(ev.eventId)));
    }

    // ── 9. parseRecord: occurredAt set from "month" field ────────────────────
    void testParseOccurredAt()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("20"), QStringLiteral("drugs"),
                            QStringLiteral("2024-07"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1")));
        QVERIFY2(ev.occurredAt.has_value(), "occurredAt must be set from month");
        QVERIFY2(ev.occurredAt->isValid(),  "occurredAt must be a valid datetime");
        QCOMPARE(ev.occurredAt->date().year(),  2024);
        QCOMPARE(ev.occurredAt->date().month(), 7);
    }

    // ── 10. parseRecord: street name populates locationRaw ───────────────────
    void testParseStreetNameInLocationRaw()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("30"), QStringLiteral("anti-social-behaviour"),
                            QStringLiteral("2024-08"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1"),
                            QStringLiteral("On or near High Street")));
        QVERIFY2(ev.locationRaw.has_value(), "locationRaw should be set from street name");
        QVERIFY2(!ev.locationRaw->isEmpty(), "locationRaw must not be empty");
    }

    // ── 11. buildFetchUrl: URL contains lat, lng, date parameters ─────────────
    void testBuildFetchUrlContents()
    {
        const QUrl url = UKPoliceSource::buildFetchUrl(51.5074, -0.1278,
                                                       QStringLiteral("2024-03"));
        const QString urlStr = url.toString();
        QVERIFY2(urlStr.contains(QStringLiteral("lat=")),    "URL must contain lat param");
        QVERIFY2(urlStr.contains(QStringLiteral("lng=")),    "URL must contain lng param");
        QVERIFY2(urlStr.contains(QStringLiteral("date=")),   "URL must contain date param");
        QVERIFY2(urlStr.contains(QStringLiteral("2024-03")), "URL must contain date value");
        QVERIFY2(urlStr.contains(QStringLiteral("51.5074")), "URL must contain latitude value");
    }

    // ── 12. buildFetchUrl: default category is "all-crime" ───────────────────
    void testBuildFetchUrlDefaultCategory()
    {
        const QUrl url = UKPoliceSource::buildFetchUrl(51.5, -0.1,
                                                       QStringLiteral("2024-01"));
        QVERIFY2(url.path().contains(QStringLiteral("all-crime")),
                 "Default category should be all-crime in path");
    }

    // ── 13. buildFetchUrl: custom category appears in URL path ────────────────
    void testBuildFetchUrlCustomCategory()
    {
        const QUrl url = UKPoliceSource::buildFetchUrl(51.5, -0.1,
                                                       QStringLiteral("2024-01"),
                                                       QStringLiteral("burglary"));
        QVERIFY2(url.path().contains(QStringLiteral("burglary")),
                 "Custom category should appear in URL path");
    }

    // ── 14. sourceId returns the expected identifier ──────────────────────────
    void testSourceId()
    {
        QCOMPARE(m_src->sourceId(), QStringLiteral("uk_police_v1"));
    }

    // ── 15. parseRecord: outcome_status.category populates outcome field ──────
    void testParseOutcomeStatus()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("50"), QStringLiteral("robbery"),
                            QStringLiteral("2024-09"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1"),
                            QString{}, QStringLiteral("Under investigation")));
        QVERIFY2(!ev.outcome.isEmpty(),
                 "outcome must be set from outcome_status.category");
    }

    // ── 16. parseRecord: source field is "uk_police_v1" ──────────────────────
    void testParseSourceField()
    {
        const auto ev = m_src->parseRecord(
            makeCrimeRecord(QStringLiteral("60"), QStringLiteral("burglary"),
                            QStringLiteral("2024-10"),
                            QStringLiteral("51.5"), QStringLiteral("-0.1")));
        QCOMPARE(ev.source, QStringLiteral("uk_police_v1"));
    }

    // ── 17. All events in a batch have non-empty crimeType ────────────────────
    void testBatchAllCrimeTypesNonEmpty()
    {
        const QList<QPair<QString, QString>> records = {
            {QStringLiteral("burglary"),              QStringLiteral("burglary")  },
            {QStringLiteral("robbery"),               QStringLiteral("robbery")   },
            {QStringLiteral("violent-crime"),         QStringLiteral("assault")   },
            {QStringLiteral("drugs"),                 QStringLiteral("drug_offence")},
            {QStringLiteral("anti-social-behaviour"), QStringLiteral("antisocial") },
        };
        for (const auto& [category, expected] : records) {
            const auto ev = m_src->parseRecord(
                makeCrimeRecord(QStringLiteral("x"), category, QStringLiteral("2024-01"),
                                QStringLiteral("51.5"), QStringLiteral("-0.1")));
            QVERIFY2(!ev.crimeType.isEmpty(),
                     qPrintable(QStringLiteral("crimeType empty for category '%1'").arg(category)));
            QCOMPARE(ev.crimeType, expected);
        }
    }

    // ── 18. parseRecord: integer id field handled without crash ───────────────
    void testParseIntegerIdField()
    {
        QJsonObject obj;
        obj[QStringLiteral("id")]       = 98765;   // integer, not string
        obj[QStringLiteral("category")] = QStringLiteral("burglary");
        obj[QStringLiteral("month")]    = QStringLiteral("2024-11");
        obj[QStringLiteral("location")] = QJsonObject{};

        const auto ev = m_src->parseRecord(obj);
        QVERIFY2(!ev.eventId.isEmpty(), "eventId must be set even from integer id field");
        QVERIFY2(ev.eventId.contains(QStringLiteral("98765")),
                 qPrintable(QStringLiteral("eventId '%1' should contain '98765'").arg(ev.eventId)));
    }

    // ── 19. availableCategories returns non-empty list ────────────────────────
    void testAvailableCategoriesNonEmpty()
    {
        const QStringList cats = m_src->availableCategories();
        QVERIFY2(!cats.isEmpty(), "availableCategories must return at least one category");
        QVERIFY(cats.contains(QStringLiteral("burglary")));
    }

    // ── 20. displayName is non-empty ──────────────────────────────────────────
    void testDisplayNameNonEmpty()
    {
        QVERIFY2(!m_src->displayName().isEmpty(), "displayName must be non-empty");
    }
};

QTEST_GUILESS_MAIN(UKPoliceSourceDeep2Test)
#include "test_uk_police_source_deep2.moc"
