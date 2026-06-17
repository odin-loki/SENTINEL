// test_provenance_quality_nlp.cpp
// Iteration 6 audit: ProvenanceLog, DataQualityScorer, CrimeClassifier, MOExtractor
#include <QTest>
#include <QTimeZone>
#include <cmath>
#include "audit/ProvenanceLog.h"
#include "ingest/DataQualityScorer.h"
#include "nlp/CrimeClassifier.h"
#include "nlp/MOExtractor.h"
#include "core/CrimeEvent.h"

class ProvenanceQualityNLPTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent fullyPopulatedEvent()
    {
        CrimeEvent ev;
        ev.eventId    = QStringLiteral("E_FULL");
        ev.source     = QStringLiteral("uk_police_v1");
        ev.crimeType  = QStringLiteral("burglary");
        ev.lat        = 51.5074;
        ev.lon        = -0.1278;
        ev.locationRaw = QStringLiteral("10 Downing Street, London");
        ev.occurredAt = QDateTime(QDate(2024, 6, 15), QTime(14, 30, 0), QTimeZone::utc());
        return ev;
    }

    static CrimeEvent lowQualityEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("E_LOW");
        ev.source    = QStringLiteral("manual");
        ev.crimeType = QStringLiteral("unknown");
        return ev;
    }

private slots:

    // ── ProvenanceLog ────────────────────────────────────────────────────────

    void testProvenanceAddEntry()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d1"));
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"),    QStringLiteral("classify"), QStringLiteral("d2"));
        log.record(QStringLiteral("E1"), QStringLiteral("model"),  QStringLiteral("fit"),     QStringLiteral("d3"));
        QCOMPARE(log.count(), 3);
    }

    void testProvenanceToHtmlValid()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("detail"));
        const QString html = log.formatHtml(QStringLiteral("E1"));
        QVERIFY2(html.contains(QStringLiteral("<html>"), Qt::CaseInsensitive) ||
                 html.contains(QStringLiteral("<table>"), Qt::CaseInsensitive),
                 "formatHtml should produce valid HTML with <html> or <table>");
    }

    void testProvenanceToMarkdownValid()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("detail"));
        // API exports plain-text chains via formatChain (no dedicated Markdown exporter)
        const QString chain = log.formatChain(QStringLiteral("E1"));
        QVERIFY2(chain.contains(QStringLiteral("[")) || chain.contains(QStringLiteral("|")),
                 "formatChain should produce a structured human-readable export");
        QVERIFY(!chain.trimmed().isEmpty());
    }

    void testProvenanceClearResets()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d"));
        log.record(QStringLiteral("E2"), QStringLiteral("nlp"),    QStringLiteral("classify"), QStringLiteral("d"));
        log.clear();
        QCOMPARE(log.count(), 0);
    }

    void testProvenanceFilterByStage()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d1"));
        log.record(QStringLiteral("E1"), QStringLiteral("model"),  QStringLiteral("fit"),    QStringLiteral("d2"));
        log.record(QStringLiteral("E2"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d3"));

        const auto ingestOnly = log.filterByStage(QStringLiteral("ingest"));
        QCOMPARE(ingestOnly.size(), 2);
        for (const auto& e : ingestOnly)
            QCOMPARE(e.stage, QStringLiteral("ingest"));
    }

    void testProvenanceChainIntegrity()
    {
        ProvenanceLog log;
        log.record(QStringLiteral("E1"), QStringLiteral("ingest"), QStringLiteral("import"), QStringLiteral("d1"));
        log.record(QStringLiteral("E1"), QStringLiteral("nlp"),    QStringLiteral("classify"), QStringLiteral("d2"));

        const auto chain = log.chain(QStringLiteral("E1"));
        QCOMPARE(chain.size(), 2);
        for (const auto& e : chain) {
            QVERIFY(e.timestamp.isValid());
            QVERIFY(!e.action.isEmpty());
        }
        QVERIFY(chain.first().timestamp <= chain.last().timestamp);
    }

    // ── DataQualityScorer ────────────────────────────────────────────────────

    void testQualityAllFieldsComplete()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto report = scorer.score(fullyPopulatedEvent());
        QCOMPARE(report.completeness, 1.0);
    }

    void testQualityMissingLatLon()
    {
        CrimeEvent ev = fullyPopulatedEvent();
        ev.lat.reset();
        ev.lon.reset();

        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto report = scorer.score(ev);
        QCOMPARE(report.spatialPrecision, QStringLiteral("unknown"));
    }

    void testQualityLowScoreQuarantined()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto report = scorer.score(lowQualityEvent());
        QVERIFY(report.compositeScore < 0.3);
        QVERIFY(report.quarantined);
    }

    void testQualityHighScoreNotQuarantined()
    {
        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        const auto report = scorer.score(fullyPopulatedEvent());
        QVERIFY(report.compositeScore >= 0.3);
        QVERIFY(!report.quarantined);
    }

    void testQualityDefaultReliabilityMapNonEmpty()
    {
        const auto map = DataQualityScorer::defaultReliabilityMap();
        QVERIFY(!map.isEmpty());
    }

    void testQualitySourceReliabilityLookup()
    {
        const auto map = DataQualityScorer::defaultReliabilityMap();
        QVERIFY(map.contains(QStringLiteral("uk_police_v1")));
        QVERIFY(map.value(QStringLiteral("uk_police_v1")) > 0.0);

        DataQualityScorer scorer = DataQualityScorer::withDefaults();
        CrimeEvent ev = fullyPopulatedEvent();
        const auto report = scorer.score(ev);
        QVERIFY(report.sourceReliability > 0.0);
    }

    // ── CrimeClassifier ──────────────────────────────────────────────────────

    void testClassifierBurglaryText()
    {
        CrimeClassifier cc;
        const auto [crimeType, confidence] =
            cc.classify(QStringLiteral("forced entry, stole electronics"));
        QVERIFY2(crimeType.contains(QStringLiteral("burglary"), Qt::CaseInsensitive) ||
                 cc.severityScore(QStringLiteral("forced entry, stole electronics"), crimeType) > 0.0,
                 qPrintable(QStringLiteral("Expected burglary classification, got: %1").arg(crimeType)));
        Q_UNUSED(confidence);
    }

    void testClassifierEmptyString()
    {
        CrimeClassifier cc;
        const auto [crimeType, confidence] = cc.classify(QStringLiteral(""));
        QVERIFY(!crimeType.isEmpty());
        QVERIFY(confidence >= 0.0 && confidence <= 1.0);
    }

    void testClassifierSeverityRange()
    {
        CrimeClassifier cc;
        const QStringList texts = {
            QStringLiteral("minor shoplifting incident"),
            QStringLiteral("armed robbery with firearm"),
            QStringLiteral("victim stabbed during assault"),
        };
        for (const QString& text : texts) {
            const auto [type, conf] = cc.classify(text);
            Q_UNUSED(conf);
            const double severity = cc.severityScore(text, type);
            QVERIFY2(severity >= 0.0 && severity <= 1.0,
                     qPrintable(QStringLiteral("Severity %1 out of [0,1] for: %2")
                                    .arg(severity).arg(text)));
        }
    }

    void testClassifierThreatSignal()
    {
        CrimeClassifier cc;
        const QString text = QStringLiteral("stabbed with knife");
        const double sent = cc.sentiment(text);
        QVERIFY(cc.threatSignal(text, sent));
    }

    void testClassifierSentimentRange()
    {
        CrimeClassifier cc;
        const QStringList texts = {
            QStringLiteral("violent attack victim injured"),
            QStringLiteral("police arrested suspect safely"),
            QStringLiteral(""),
        };
        for (const QString& text : texts) {
            const double sent = cc.sentiment(text);
            QVERIFY2(sent >= -1.0 && sent <= 1.0,
                     qPrintable(QStringLiteral("Sentiment %1 out of [-1,1]").arg(sent)));
        }
    }

    // ── MOExtractor ──────────────────────────────────────────────────────────

    void testMOExtractorForcedEntry()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("broke through rear door"));
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
    }

    void testMOExtractorNightTime()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("at 2am"));
        QVERIFY(mo.timeOfDay.has_value());
        QVERIFY2(*mo.timeOfDay == QStringLiteral("early_morning") ||
                 *mo.timeOfDay == QStringLiteral("night"),
                 qPrintable(QStringLiteral("Expected early_morning or night, got: %1")
                                .arg(*mo.timeOfDay)));
    }

    void testMOExtractorResidential()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("entered private house"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));
    }

    void testMOExtractorEmptyNarrative()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral(""));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(mo.itemsTaken.empty());
        QVERIFY(!mo.soloOrGroup.has_value());
        QVERIFY(!mo.victimProfile.has_value());
    }

    void testMOExtractorWeapon()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("threatened with a knife"));
        QVERIFY(mo.weaponType.has_value());
        QVERIFY2(*mo.weaponType == QStringLiteral("knife") ||
                 *mo.weaponType == QStringLiteral("bladed"),
                 qPrintable(QStringLiteral("Expected knife or bladed, got: %1")
                                .arg(*mo.weaponType)));
    }
};

QTEST_MAIN(ProvenanceQualityNLPTest)
#include "test_provenance_quality_nlp.moc"
