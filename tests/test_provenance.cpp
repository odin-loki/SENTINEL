// test_provenance.cpp — Comprehensive tests for ProvenanceLog and LeadReportGenerator
#include <QTest>
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>

#include "audit/ProvenanceLog.h"
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static InvestigativeLead makeLead(const QString& cat,
                                   const QString& headline,
                                   double conf)
{
    InvestigativeLead l;
    l.category         = cat;
    l.headline         = headline;
    l.confidence       = conf;
    l.rank             = 1;
    l.detail           = QStringLiteral("Detail for %1").arg(headline);
    l.confidenceMethod = QStringLiteral("test");
    l.generatedAt      = QDateTime::currentDateTimeUtc();
    return l;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestProvenanceLog
// ─────────────────────────────────────────────────────────────────────────────

class TestProvenanceLog : public QObject {
    Q_OBJECT
private slots:

    void testRecordSingleEntry()
    {
        ProvenanceLog log;
        log.record("event1", "ingest", "import", "Loaded CSV row", "abc123");

        const auto chain = log.chain("event1");
        QCOMPARE(chain.size(), 1);
        QCOMPARE(chain[0].eventId,  QStringLiteral("event1"));
        QCOMPARE(chain[0].stage,    QStringLiteral("ingest"));
        QCOMPARE(chain[0].action,   QStringLiteral("import"));
        QCOMPARE(chain[0].detail,   QStringLiteral("Loaded CSV row"));
        QCOMPARE(chain[0].dataHash, QStringLiteral("abc123"));
    }

    void testRecordMultipleEvents()
    {
        ProvenanceLog log;
        log.record("evtA", "ingest", "import", "A row 1");
        log.record("evtB", "nlp",    "classify", "B row 1");
        log.record("evtC", "model",  "predict", "C row 1");

        QCOMPARE(log.chain("evtA").size(), 1);
        QCOMPARE(log.chain("evtB").size(), 1);
        QCOMPARE(log.chain("evtC").size(), 1);

        QCOMPARE(log.chain("evtA")[0].stage, QStringLiteral("ingest"));
        QCOMPARE(log.chain("evtB")[0].stage, QStringLiteral("nlp"));
        QCOMPARE(log.chain("evtC")[0].stage, QStringLiteral("model"));
    }

    void testChainFiltersCorrectly()
    {
        ProvenanceLog log;
        // Interleave records for two different events
        log.record("alpha", "ingest",    "import",   "alpha 1");
        log.record("beta",  "ingest",    "import",   "beta 1");
        log.record("alpha", "nlp",       "classify", "alpha 2");
        log.record("beta",  "nlp",       "classify", "beta 2");
        log.record("alpha", "inference", "score",    "alpha 3");

        const auto chainAlpha = log.chain("alpha");
        const auto chainBeta  = log.chain("beta");

        QCOMPARE(chainAlpha.size(), 3);
        QCOMPARE(chainBeta.size(),  2);

        // Verify none of beta's entries appear in alpha's chain
        for (const auto& e : chainAlpha)
            QCOMPARE(e.eventId, QStringLiteral("alpha"));

        for (const auto& e : chainBeta)
            QCOMPARE(e.eventId, QStringLiteral("beta"));
    }

    void testRecentReturnsLatest()
    {
        ProvenanceLog log;
        for (int i = 0; i < 150; ++i)
            log.record("evtX", "ingest", "action", QString("detail%1").arg(i));

        const auto recent = log.recent(100);
        QCOMPARE(recent.size(), 100);
    }

    void testClearRemovesAll()
    {
        ProvenanceLog log;
        log.record("evt1", "ingest", "import", "some detail");
        log.record("evt2", "nlp",    "classify", "other detail");
        QVERIFY(!log.chain("evt1").isEmpty());

        log.clear();

        QVERIFY(log.chain("evt1").isEmpty());
        QVERIFY(log.chain("evt2").isEmpty());
        QVERIFY(log.recent(10).isEmpty());
    }

    void testFormatChainNonEmpty()
    {
        ProvenanceLog log;
        log.record("evt42", "nlp", "classify", "Crime type detected");

        const QString formatted = log.formatChain("evt42");

        QVERIFY(!formatted.isEmpty());
        QVERIFY(formatted.contains("evt42") || formatted.contains("nlp"));
    }

    void testFormatChainUnknownEvent()
    {
        ProvenanceLog log;
        log.record("knownEvent", "ingest", "import", "some data");

        const QString formatted = log.formatChain("unknownEvent");

        // Implementation returns empty string when no entries exist
        QVERIFY(formatted.isEmpty());
    }

    void testDataHashStored()
    {
        ProvenanceLog log;
        const QString hash = QStringLiteral("deadbeef12345678");
        log.record("evtHash", "ingest", "import", "CSV row data", hash);

        const auto chain = log.chain("evtHash");
        QCOMPARE(chain.size(), 1);
        QCOMPARE(chain[0].dataHash, hash);
    }

    void testTimestampOrdering()
    {
        ProvenanceLog log;
        // Record entries with small waits to ensure distinct timestamps
        for (int i = 0; i < 5; ++i) {
            log.record("evtOrd", "ingest", "action", QString("step%1").arg(i));
            QTest::qWait(10);
        }

        const auto recent = log.recent(5);
        QCOMPARE(recent.size(), 5);

        // recent() returns newest-first (descending order)
        for (int i = 0; i < recent.size() - 1; ++i)
            QVERIFY(recent[i].timestamp >= recent[i + 1].timestamp);
    }

    void testMultipleStages()
    {
        ProvenanceLog log;
        const QString evId = QStringLiteral("evtPipeline");

        log.record(evId, "ingest",    "import",   "Raw CSV row");
        QTest::qWait(5);
        log.record(evId, "nlp",       "classify", "Classified as burglary");
        QTest::qWait(5);
        log.record(evId, "inference", "score",    "Confidence 0.87");

        const auto chain = log.chain(evId);
        QCOMPARE(chain.size(), 3);

        // chain() returns ascending chronological order
        QCOMPARE(chain[0].stage, QStringLiteral("ingest"));
        QCOMPARE(chain[1].stage, QStringLiteral("nlp"));
        QCOMPARE(chain[2].stage, QStringLiteral("inference"));

        // Verify ascending timestamps
        for (int i = 0; i < chain.size() - 1; ++i)
            QVERIFY(chain[i].timestamp <= chain[i + 1].timestamp);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestLeadReportGenerator
// ─────────────────────────────────────────────────────────────────────────────

class TestLeadReportGenerator : public QObject {
    Q_OBJECT
private slots:

    void testGenerateEmptyLeads()
    {
        const LeadReport report =
            LeadReportGenerator::generate(QStringLiteral("CASE-001"), {});

        QCOMPARE(report.totalLeads, 0);
        QCOMPARE(report.highConfidenceLeads, 0);
        QCOMPARE(report.topConfidence, 0.0);
        QVERIFY(!report.markdownText.isEmpty()); // header is still present
        QVERIFY(report.markdownText.contains("CASE-001"));
    }

    void testGenerateWithLeads()
    {
        QVector<InvestigativeLead> leads = {
            makeLead("Burglary Series", "Near-repeat pattern detected", 0.85),
            makeLead("MO Match",        "Forced entry MO matches case C12", 0.72),
            makeLead("Geographic",      "Anchor point in Suburb X",          0.60),
        };

        const LeadReport report =
            LeadReportGenerator::generate(QStringLiteral("CASE-002"), leads);

        QCOMPARE(report.totalLeads, 3);
        QVERIFY(report.markdownText.contains("Near-repeat pattern detected"));
        QVERIFY(report.markdownText.contains("Forced entry MO matches case C12"));
        QVERIFY(report.markdownText.contains("Anchor point in Suburb X"));
    }

    void testMarkdownFormatting()
    {
        QVector<InvestigativeLead> leads = {
            makeLead("TestCat", "Test Headline", 0.80),
        };
        const LeadReport report =
            LeadReportGenerator::generate(QStringLiteral("CASE-003"), leads);

        // Should contain Markdown structural elements
        QVERIFY(report.markdownText.contains("##"));
        QVERIFY(report.markdownText.contains("**"));
        // Confidence as percentage
        QVERIFY(report.markdownText.contains("80%") ||
                report.markdownText.contains("80 %"));
    }

    void testHighConfidenceCount()
    {
        QVector<InvestigativeLead> leads = {
            makeLead("Cat1", "Lead A", 0.80),
            makeLead("Cat2", "Lead B", 0.75),
            makeLead("Cat3", "Lead C", 0.50),
        };
        const LeadReport report =
            LeadReportGenerator::generate(QStringLiteral("CASE-004"), leads);

        QCOMPARE(report.totalLeads, 3);
        QCOMPARE(report.highConfidenceLeads, 2); // 0.80 and 0.75 >= 0.7
    }

    void testTopConfidence()
    {
        QVector<InvestigativeLead> leads = {
            makeLead("Cat1", "Low",    0.40),
            makeLead("Cat2", "Medium", 0.65),
            makeLead("Cat3", "High",   0.92),
        };
        const LeadReport report =
            LeadReportGenerator::generate(QStringLiteral("CASE-005"), leads);

        QCOMPARE(report.topConfidence, 0.92);
    }

    void testSaveToFile()
    {
        QVector<InvestigativeLead> leads = {
            makeLead("Category", "Test headline", 0.75),
        };
        const LeadReport report =
            LeadReportGenerator::generate(QStringLiteral("CASE-006"), leads);

        // Save to a temp file
        const QString path = QDir::tempPath() + "/sentinel_lead_test.md";
        const bool ok = LeadReportGenerator::saveToFile(report, path, true);
        QVERIFY(ok);

        QFile f(path);
        QVERIFY(f.exists());
        QVERIFY(f.size() > 0);
        f.remove();
    }

    void testFormatSingleLead()
    {
        InvestigativeLead lead = makeLead("Burglary", "Forced entry at night", 0.77);
        lead.rank = 1;

        const QString formatted = LeadReportGenerator::formatLead(lead);

        QVERIFY(formatted.contains("Forced entry at night")); // headline
        QVERIFY(formatted.contains("Burglary"));              // category
        // Confidence formatted as percentage
        QVERIFY(formatted.contains("77%") || formatted.contains("77 %"));
    }

    void testFormatProvenance()
    {
        const QStringList steps = {
            QStringLiteral("CSV Import"),
            QStringLiteral("NLP Classifier"),
            QStringLiteral("HintEngine"),
        };
        const QString result = LeadReportGenerator::formatProvenance(steps);

        // Should produce "CSV Import → NLP Classifier → HintEngine"
        QVERIFY(result.contains("CSV Import"));
        QVERIFY(result.contains("NLP Classifier"));
        QVERIFY(result.contains("HintEngine"));
        // The arrow separator should appear (U+2192 or ASCII ->)
        QVERIFY(result.contains("\u2192") || result.contains("->") || result.contains("→"));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main
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
    { TestProvenanceLog       t1; r |= runTest(&t1, "prov_log.txt");     }
    { TestLeadReportGenerator t2; r |= runTest(&t2, "lead_report.txt");  }
    return r;
}

#include "test_provenance.moc"
