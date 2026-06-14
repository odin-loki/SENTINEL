// test_provenance_deep7.cpp — Deep audit iteration 28: ProvenanceLog
// recent(), time filter, clear/count, CSV export, filterByModel.
#include <QTest>
#include <QTimeZone>
#include "audit/ProvenanceLog.h"

class TestProvenanceDeep7 : public QObject
{
    Q_OBJECT

private slots:

    void testRecentReturnsLastN()
    {
        ProvenanceLog log;
        for (int i = 0; i < 5; ++i)
            log.record(QStringLiteral("R%1").arg(i), QStringLiteral("ingest"),
                       QStringLiteral("load"), QStringLiteral("row"));

        const auto recent = log.recent(3);
        QCOMPARE(recent.size(), 3);
        QCOMPARE(recent.first().eventId, QStringLiteral("R4"));
        QCOMPARE(recent.last().eventId, QStringLiteral("R2"));
    }

    void testFilterByTimeRange()
    {
        ProvenanceLog log;
        const QDateTime t1(QDate(2024, 1, 1), QTime(10, 0), QTimeZone::utc());
        const QDateTime t2(QDate(2024, 6, 1), QTime(10, 0), QTimeZone::utc());

        log.addEntry(QStringLiteral("ingest"), QStringLiteral("CsvImporter"),
                     QStringLiteral("load"), QStringLiteral("a"), t1);
        log.addEntry(QStringLiteral("model"), QStringLiteral("PoissonBaseline"),
                     QStringLiteral("fit"), QStringLiteral("b"), t2);

        const auto entries = log.filterByTimeRange(t2.addDays(-1), t2.addDays(1));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().model, QStringLiteral("PoissonBaseline"));
    }

    void testClearResetsCount()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("C1"), QStringLiteral("ingest"),
                   QStringLiteral("load"), QStringLiteral("x"));
        QCOMPARE(log.count(), 1);
        log.clear();
        QCOMPARE(log.count(), 0);
        QVERIFY(log.getEntries().isEmpty());
    }

    void testExportToCsvHasHeader()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("CSV-1"), QStringLiteral("output"),
                   QStringLiteral("export"), QStringLiteral("file"));
        const QString csv = log.exportToCsv();
        QVERIFY(csv.contains(QStringLiteral("timestamp")));
    }

    void testFilterByModel()
    {
        ProvenanceLog log;
        log.addEntry(QStringLiteral("PoissonBaseline"), QStringLiteral("PoissonBaseline"),
                     QStringLiteral("fit"), QStringLiteral("zone rates"));
        log.addEntry(QStringLiteral("HintEngine"), QStringLiteral("HintEngine"),
                     QStringLiteral("generate"), QStringLiteral("leads"));

        const auto poisson = log.filterByModel(QStringLiteral("PoissonBaseline"));
        QCOMPARE(poisson.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestProvenanceDeep7)
#include "test_provenance_deep7.moc"
