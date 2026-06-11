// test_database_performance.cpp — Database performance and correctness benchmarks
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTimeZone>
#include <QThread>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <QFile>
#include <QUuid>
#include <atomic>
#include <QSet>
#include <cmath>

#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

namespace {

AppConfig inMemoryCfg()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    return cfg;
}

QString tempDbPath()
{
    return QDir::tempPath() + QStringLiteral("/sentinel_perf_")
           + QUuid::createUuid().toString(QUuid::Id128)
           + QStringLiteral(".db");
}

void removeTempDb(const QString& path)
{
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

CrimeEvent makeEvent(int idx,
                     const QString& crimeType = QStringLiteral("theft"),
                     const QDateTime& occurred = {},
                     double quality = 0.75)
{
    CrimeEvent ev;
    ev.eventId      = QStringLiteral("PERF-%1").arg(idx, 5, 10, QChar('0'));
    ev.id           = ev.eventId;
    ev.source       = QStringLiteral("perf_test");
    ev.ingestedAt   = QDateTime::currentDateTimeUtc();
    ev.occurredAt   = occurred.isValid()
                          ? occurred
                          : QDateTime::currentDateTimeUtc().addDays(-(idx % 30));
    ev.timestamp    = ev.occurredAt.value_or(ev.ingestedAt);
    ev.crimeType    = crimeType;
    ev.suburb       = QStringLiteral("London");
    ev.lat          = 51.5 + idx * 0.0001;
    ev.lon          = -0.12;
    ev.latitude     = ev.lat.value_or(0.0);
    ev.longitude    = ev.lon.value_or(0.0);
    ev.qualityScore = quality;
    return ev;
}

InvestigativeLead makeLead(int rank, const QString& headline)
{
    InvestigativeLead lead;
    lead.rank             = rank;
    lead.category         = QStringLiteral("pattern");
    lead.headline         = headline;
    lead.detail           = QStringLiteral("Performance test lead detail");
    lead.confidence       = 0.72;
    lead.confidenceMethod = QStringLiteral("unit_test");
    lead.generatedAt      = QDateTime::currentDateTimeUtc();
    return lead;
}

class InsertThread : public QThread
{
public:
    InsertThread(const QString& dbPath, int startIdx, int count, std::atomic<int>* okCount)
        : m_dbPath(dbPath)
        , m_startIdx(startIdx)
        , m_count(count)
        , m_okCount(okCount)
    {}

protected:
    void run() override
    {
        AppConfig cfg;
        cfg.databasePath = m_dbPath;
        Database db(cfg);
        if (!db.open())
            return;

        for (int i = 0; i < m_count; ++i) {
            const int idx = m_startIdx + i;
            CrimeEvent ev = makeEvent(idx, QStringLiteral("theft"));
            if (db.insertEvent(ev))
                m_okCount->fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    QString m_dbPath;
    int m_startIdx;
    int m_count;
    std::atomic<int>* m_okCount;
};

} // namespace

class DatabasePerformanceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_cfg = inMemoryCfg();
        m_db  = std::make_unique<Database>(m_cfg);
        QVERIFY(m_db->open());
    }

    void cleanupTestCase()
    {
        m_db.reset();
    }

    void testInsert1000Events()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        QElapsedTimer timer;
        timer.start();

        for (int i = 0; i < 1000; ++i)
            QVERIFY(db.insertEvent(makeEvent(i)));

        const qint64 elapsed = timer.elapsed();
        QCOMPARE(db.eventCount(), 1000);
        QVERIFY2(elapsed < 5000,
                 qPrintable(QStringLiteral("1000 inserts took %1 ms, expected < 5000 ms")
                                .arg(elapsed)));

        QBENCHMARK {
            CrimeEvent ev = makeEvent(100000);
            ev.eventId = ev.id = QStringLiteral("BENCH-INS");
            db.insertEvent(ev);
        }
    }

    void testQueryAllEvents1000()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        for (int i = 0; i < 1000; ++i)
            QVERIFY(db.insertEvent(makeEvent(i)));

        QElapsedTimer timer;
        timer.start();
        const QVector<CrimeEvent> all = db.getAllEvents();
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(all.size(), 1000);
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("getAllEvents took %1 ms, expected < 1000 ms")
                                .arg(elapsed)));

        QBENCHMARK {
            const auto results = db.getAllEvents();
            Q_UNUSED(results);
        }
    }

    void testQueryByType()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        for (int i = 0; i < 100; ++i)
            QVERIFY(db.insertEvent(makeEvent(i, QStringLiteral("burglary"))));
        for (int i = 0; i < 50; ++i)
            QVERIFY(db.insertEvent(makeEvent(100 + i, QStringLiteral("assault"))));

        const auto burglaries = db.queryEvents(QStringLiteral("burglary"));
        const auto assaults   = db.queryEvents(QStringLiteral("assault"));

        QCOMPARE(burglaries.size(), 100);
        QCOMPARE(assaults.size(), 50);

        for (const auto& ev : burglaries)
            QCOMPARE(ev.crimeType, QStringLiteral("burglary"));
        for (const auto& ev : assaults)
            QCOMPARE(ev.crimeType, QStringLiteral("assault"));
    }

    void testQueryBySince()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (int i = 0; i < 20; ++i)
            QVERIFY(db.insertEvent(makeEvent(i, QStringLiteral("theft"),
                                             now.addDays(-11 - i))));
        QSet<QString> recentIds;
        for (int i = 0; i < 10; ++i) {
            CrimeEvent ev = makeEvent(20 + i, QStringLiteral("theft"), now.addDays(-i));
            recentIds.insert(ev.eventId);
            QVERIFY(db.insertEvent(ev));
        }

        const QDateTime since = now.addDays(-9);
        const auto recent = db.getEventsSince(since);

        QCOMPARE(recent.size(), 10);
        for (const auto& ev : recent)
            QVERIFY2(recentIds.contains(ev.eventId),
                     qPrintable(QStringLiteral("Unexpected event %1 in since-filtered results")
                                    .arg(ev.eventId)));

        const auto older = db.getEventsSince(now.addDays(-3));
        QCOMPARE(older.size(), 4);
    }

    void testGetHourlyCounts()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        const QDateTime anchor = QDateTime::currentDateTimeUtc().addDays(-1);
        int inserted = 0;
        for (int h = 0; h < 24; ++h) {
            for (int j = 0; j < 3; ++j) {
                const QDateTime ts = anchor.addSecs(static_cast<qint64>(h) * 3600 + j * 60);
                QVERIFY(db.insertEvent(makeEvent(inserted++, QStringLiteral("theft"), ts)));
            }
        }

        const QVector<int> hourly = db.getHourlyCounts(30);
        QCOMPARE(hourly.size(), 24);

        int total = 0;
        for (int count : hourly)
            total += count;
        QCOMPARE(total, inserted);
        QVERIFY2(total >= 24, "Hourly buckets should reflect inserted events within the window");
    }

    void testGetDailyTrend()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        const QDateTime now = QDateTime::currentDateTimeUtc();
        QMap<QDate, int> expected;

        for (int day = 0; day < 30; ++day) {
            const int count = (day % 5) + 1;
            expected[now.addDays(-day).date()] = count;
            for (int j = 0; j < count; ++j) {
                const QDateTime ts = QDateTime(now.addDays(-day).date(), QTime(12, j, 0),
                                               QTimeZone::utc());
                QVERIFY(db.insertEvent(makeEvent(day * 10 + j, QStringLiteral("robbery"), ts)));
            }
        }

        const auto trend = db.getDailyTrend(30);
        QVERIFY2(!trend.isEmpty(), "Daily trend should not be empty");

        int trendTotal = 0;
        for (const auto& [day, count] : trend) {
            trendTotal += count;
            if (expected.contains(day))
                QCOMPARE(count, expected.value(day));
        }
        QCOMPARE(trendTotal, db.eventCount());
    }

    void testGetCrimeTypeCounts()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        for (int i = 0; i < 40; ++i)
            QVERIFY(db.insertEvent(makeEvent(i, QStringLiteral("burglary"))));
        for (int i = 0; i < 25; ++i)
            QVERIFY(db.insertEvent(makeEvent(40 + i, QStringLiteral("theft"))));
        for (int i = 0; i < 15; ++i)
            QVERIFY(db.insertEvent(makeEvent(65 + i, QStringLiteral("assault"))));

        const QMap<QString, int> counts = db.getCrimeTypeCounts();
        QCOMPARE(counts.value(QStringLiteral("burglary")), 40);
        QCOMPARE(counts.value(QStringLiteral("theft")), 25);
        QCOMPARE(counts.value(QStringLiteral("assault")), 15);
    }

    void testConcurrentInsert()
    {
        const QString path = tempDbPath();
        {
            AppConfig cfg;
            cfg.databasePath = path;
            Database db(cfg);
            QVERIFY(db.open());
        }

        std::atomic<int> okCount{0};
        InsertThread t1(path, 0, 500, &okCount);
        InsertThread t2(path, 500, 500, &okCount);

        t1.start();
        t2.start();
        t1.wait();
        t2.wait();

        AppConfig cfg;
        cfg.databasePath = path;
        Database db(cfg);
        QVERIFY(db.open());

        QCOMPARE(okCount.load(), 1000);
        QCOMPARE(db.eventCount(), 1000);
        removeTempDb(path);
    }

    void testWALModeEnabled()
    {
        const QString path = tempDbPath();
        {
            AppConfig cfg;
            cfg.databasePath = path;
            Database db(cfg);
            QVERIFY(db.open());
        }

        const QString connName = QStringLiteral("wal_perf_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
        {
            QSqlDatabase rawDb =
                QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            rawDb.setDatabaseName(path);
            QVERIFY(rawDb.open());

            QSqlQuery q(rawDb);
            QVERIFY(q.exec(QStringLiteral("PRAGMA journal_mode")));
            QVERIFY(q.next());
            QCOMPARE(q.value(0).toString(), QStringLiteral("wal"));
            rawDb.close();
        }
        QSqlDatabase::removeDatabase(connName);
        removeTempDb(path);
    }

    void testSchemaVersioning()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());
        QCOMPARE(db.currentSchemaVersion(), Database::SCHEMA_VERSION);
        QVERIFY(db.currentSchemaVersion() >= 1);
    }

    void testEventByIdFound()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        CrimeEvent ev = makeEvent(42, QStringLiteral("fraud"), {}, 0.91);
        ev.suburb = QStringLiteral("Camden");
        QVERIFY(db.insertEvent(ev));

        const CrimeEvent found = db.eventById(ev.eventId);
        QCOMPARE(found.eventId, ev.eventId);
        QCOMPARE(found.crimeType, QStringLiteral("fraud"));
        QCOMPARE(found.suburb, QStringLiteral("Camden"));
        QVERIFY(std::abs(found.qualityScore - 0.91) < 1e-6);
    }

    void testInsertAndQueryLeads()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        const CrimeEvent ev = makeEvent(7, QStringLiteral("burglary"));
        QVERIFY(db.insertEvent(ev));

        InvestigativeLead lead = makeLead(1, QStringLiteral("Repeat MO pattern"));
        QVERIFY(db.insertLead(lead, ev.eventId));

        const auto allLeads = db.queryLeads();
        QCOMPARE(allLeads.size(), 1);
        QCOMPARE(allLeads[0].headline, lead.headline);
        QCOMPARE(allLeads[0].rank, 1);

        const auto eventLeads = db.queryLeads(ev.eventId);
        QCOMPARE(eventLeads.size(), 1);
        QCOMPARE(eventLeads[0].headline, lead.headline);
    }

    void testAverageQualityScore()
    {
        Database db(inMemoryCfg());
        QVERIFY(db.open());

        QVERIFY(db.insertEvent(makeEvent(1, QStringLiteral("theft"), {}, 0.2)));
        QVERIFY(db.insertEvent(makeEvent(2, QStringLiteral("theft"), {}, 0.8)));

        const double avg = db.getAverageQualityScore();
        QVERIFY2(avg >= 0.0 && avg <= 1.0,
                 qPrintable(QStringLiteral("Average quality score %1 not in [0,1]").arg(avg)));
        QVERIFY(std::abs(avg - 0.5) < 1e-6);
    }

private:
    AppConfig m_cfg;
    std::unique_ptr<Database> m_db;
};

QTEST_MAIN(DatabasePerformanceTest)
#include "test_database_performance.moc"
