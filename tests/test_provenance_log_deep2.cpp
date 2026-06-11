#include <QTest>
#include <QThread>
#include <QDateTime>
#include <cmath>
#include "audit/ProvenanceLog.h"

class TestProvenanceLogDeep2 : public QObject {
    Q_OBJECT

private slots:

    void testAddEntryStoresAllFields()
    {
        ProvenanceLog log;
        const QDateTime ts = QDateTime::fromString("2024-06-01T12:00:00", Qt::ISODate);

        log.addEntry("SourceA", "ModelX", "predict", "detail text", ts);

        QCOMPARE(log.count(), 1);
        const QVector<ProvenanceEntry> entries = log.filterByModel("ModelX");
        QCOMPARE(entries.size(), 1);

        const ProvenanceEntry& e = entries.first();
        QCOMPARE(e.source,  QStringLiteral("SourceA"));
        QCOMPARE(e.model,   QStringLiteral("ModelX"));
        QCOMPARE(e.action,  QStringLiteral("predict"));
        QCOMPARE(e.detail,  QStringLiteral("detail text"));
        QCOMPARE(e.timestamp, ts);
    }

    void testRetrievalInInsertionOrder()
    {
        // filterBySource iterates the internal vector forward → insertion order.
        ProvenanceLog log;
        log.addEntry("DataPipe", "M1", "a1", "first");
        log.addEntry("DataPipe", "M2", "a2", "second");
        log.addEntry("DataPipe", "M3", "a3", "third");

        const QVector<ProvenanceEntry> entries = log.filterBySource("DataPipe");
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries[0].model, QStringLiteral("M1"));
        QCOMPARE(entries[1].model, QStringLiteral("M2"));
        QCOMPARE(entries[2].model, QStringLiteral("M3"));
    }

    void testFilterByModelReturnsOnlyMatching()
    {
        ProvenanceLog log;
        log.addEntry("src", "BayesianModel", "fit",     "zone data");
        log.addEntry("src", "GPRegression",  "predict", "spatial");
        log.addEntry("src", "BayesianModel", "predict", "zone forecast");
        log.addEntry("src", "KDEHotspot",    "fit",     "density");

        const QVector<ProvenanceEntry> bayesian = log.filterByModel("BayesianModel");
        QCOMPARE(bayesian.size(), 2);
        for (const ProvenanceEntry& e : bayesian)
            QCOMPARE(e.model, QStringLiteral("BayesianModel"));

        const QVector<ProvenanceEntry> gp = log.filterByModel("GPRegression");
        QCOMPARE(gp.size(), 1);
        QCOMPARE(gp[0].action, QStringLiteral("predict"));

        const QVector<ProvenanceEntry> missing = log.filterByModel("NoSuchModel");
        QCOMPARE(missing.size(), 0);
    }

    void testFilterByTimeRange()
    {
        ProvenanceLog log;
        const QDateTime t1 = QDateTime::fromString("2024-01-01T00:00:00", Qt::ISODate);
        const QDateTime t2 = QDateTime::fromString("2024-06-01T00:00:00", Qt::ISODate);
        const QDateTime t3 = QDateTime::fromString("2024-12-01T00:00:00", Qt::ISODate);

        log.addEntry("s", "m", "a", "early",  t1);
        log.addEntry("s", "m", "a", "mid",    t2);
        log.addEntry("s", "m", "a", "late",   t3);

        const QDateTime from = QDateTime::fromString("2024-03-01T00:00:00", Qt::ISODate);
        const QDateTime to   = QDateTime::fromString("2024-09-01T00:00:00", Qt::ISODate);

        const QVector<ProvenanceEntry> mid = log.filterByTimeRange(from, to);
        QCOMPARE(mid.size(), 1);
        QCOMPARE(mid[0].detail, QStringLiteral("mid"));

        // Inclusive bounds
        const QVector<ProvenanceEntry> allThree = log.filterByTimeRange(t1, t3);
        QCOMPARE(allThree.size(), 3);

        const QVector<ProvenanceEntry> none = log.filterByTimeRange(
            QDateTime::fromString("2025-01-01T00:00:00", Qt::ISODate),
            QDateTime::fromString("2025-12-01T00:00:00", Qt::ISODate));
        QCOMPARE(none.size(), 0);
    }

    void testConcurrentWritesDontCorruptLog()
    {
        ProvenanceLog log;
        constexpr int numThreads = 4;
        constexpr int entriesPerThread = 200;

        QVector<QThread*> threads;
        threads.reserve(numThreads);

        for (int t = 0; t < numThreads; ++t) {
            threads.append(QThread::create([&log, t]() {
                for (int i = 0; i < entriesPerThread; ++i) {
                    log.addEntry("src",
                                 QString("model%1").arg(t),
                                 "write",
                                 QString("entry%1_%2").arg(t).arg(i));
                }
            }));
        }

        for (auto* th : threads) th->start();
        for (auto* th : threads) { th->wait(); delete th; }

        QCOMPARE(log.count(), numThreads * entriesPerThread);

        // Each model should have exactly entriesPerThread entries
        for (int t = 0; t < numThreads; ++t) {
            const auto entries = log.filterByModel(QString("model%1").arg(t));
            QCOMPARE(entries.size(), entriesPerThread);
        }
    }

    void testNonCopyableVerifiedByInspection()
    {
        // ProvenanceLog contains mutable QMutex m_mutex which is non-copyable.
        // Attempting to copy or move a ProvenanceLog is a compile-time error.
        // This test documents that invariant; the static assertion below verifies it.
        static_assert(!std::is_copy_constructible_v<ProvenanceLog>,
                      "ProvenanceLog must not be copy-constructible");
        static_assert(!std::is_move_constructible_v<ProvenanceLog>,
                      "ProvenanceLog must not be move-constructible");
        QVERIFY(true);
    }

    void testAddEntryWithDefaultTimestampUsesCurrentTime()
    {
        ProvenanceLog log;
        const QDateTime before = QDateTime::currentDateTimeUtc();
        log.addEntry("src", "model", "action", "detail");
        const QDateTime after  = QDateTime::currentDateTimeUtc();

        const auto entries = log.filterByModel("model");
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries[0].timestamp >= before);
        QVERIFY(entries[0].timestamp <= after);
    }
};

QTEST_GUILESS_MAIN(TestProvenanceLogDeep2)
#include "test_provenance_log_deep2.moc"
