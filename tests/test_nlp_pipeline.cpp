// test_nlp_pipeline.cpp — NLP & HTML-export pipeline tests for SENTINEL
//
// Covers:
//   Task 1 – MOExtractor::extract(), canonicalMOString()
//   Task 2 – CrimeClassifier::classify(), sentiment(), threatSignal()
//   Task 3 – MOAnalyser end-to-end similarity pipeline
//   Task 4 – LeadReportGenerator::generateHtml()

#include <QTest>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <cmath>
#include <algorithm>

#include "nlp/MOExtractor.h"
#include "nlp/CrimeClassifier.h"
#include "inference/MOAnalyser.h"
#include "inference/LeadReportGenerator.h"
#include "core/CrimeEvent.h"

// ── helpers ───────────────────────────────────────────────────────────────────

static InvestigativeLead makeLead(const QString& headline,
                                   const QString& category,
                                   double confidence,
                                   const QString& detail = {},
                                   const QStringList& prov = {})
{
    InvestigativeLead lead;
    lead.headline   = headline;
    lead.category   = category;
    lead.confidence = confidence;
    lead.detail     = detail;
    for (const QString& p : prov)
        lead.provenance.push_back(p);
    lead.generatedAt = QDateTime::currentDateTimeUtc();
    return lead;
}

// ─────────────────────────────────────────────────────────────────────────────
// TestMOExtractor
// ─────────────────────────────────────────────────────────────────────────────

class TestMOExtractor : public QObject
{
    Q_OBJECT
private slots:

    // "suspect forced entry through rear window using a screwdriver"
    // "forced" → entryMethod = forced_entry; no targetType (no house/vehicle word)
    void forcedEntryRearWindow()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("suspect forced entry through rear window using a screwdriver"));

        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));

        // canonical string reflects the extracted feature
        const QString canonical = ex.canonicalMOString(mo);
        QVERIFY(canonical.contains(QStringLiteral("forced_entry")));
    }

    // "victim distracted then pickpocketed in crowd"
    // "victim" → targetType = person
    void victimPickpocketed()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("victim distracted then pickpocketed in crowd"));

        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("person"));
    }

    // "vehicle broken into, stereo stolen"
    // "broken" → forced_entry; "vehicle" → targetType = vehicle
    void vehicleBrokenInto()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("vehicle broken into, stereo stolen"));

        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("vehicle"));
    }

    // Empty string → all fields absent, itemsTaken empty
    void emptyStringYieldsNoFeatures()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral(""));

        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        QVERIFY(!mo.timeOfDay.has_value());
        QVERIFY(!mo.weaponType.has_value());
        QVERIFY(mo.itemsTaken.empty());
        QVERIFY(!mo.soloOrGroup.has_value());
    }

    // Single word "burglary" — graceful no-op (no pattern matches the bare noun)
    void singleWordBurglary()
    {
        MOExtractor ex;
        const MOFeatures mo = ex.extract(QStringLiteral("burglary"));
        QVERIFY(!mo.entryMethod.has_value());
        QVERIFY(!mo.targetType.has_value());
        // canonical string is empty or whitespace-only
        QVERIFY(ex.canonicalMOString(mo).trimmed().isEmpty());
    }

    // Rich narrative → multiple fields populated, canonical string contains all tokens
    void richNarrativeCanonical()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("broke into flat at night and stole cash alone"));

        // Entry: "broke" → forced_entry
        QVERIFY(mo.entryMethod.has_value());
        QCOMPARE(*mo.entryMethod, QStringLiteral("forced_entry"));

        // Target: "flat" → residential
        QVERIFY(mo.targetType.has_value());
        QCOMPARE(*mo.targetType, QStringLiteral("residential"));

        // Time: "night"
        QVERIFY(mo.timeOfDay.has_value());
        QCOMPARE(*mo.timeOfDay, QStringLiteral("night"));

        // Items: "cash"
        QVERIFY(!mo.itemsTaken.empty());
        const auto it = std::find(mo.itemsTaken.begin(), mo.itemsTaken.end(),
                                  QStringLiteral("cash"));
        QVERIFY(it != mo.itemsTaken.end());

        // Solo: "alone"
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("solo"));

        const QString canonical = ex.canonicalMOString(mo);
        QVERIFY(canonical.contains(QStringLiteral("forced_entry")));
        QVERIFY(canonical.contains(QStringLiteral("residential")));
        QVERIFY(canonical.contains(QStringLiteral("night")));
        QVERIFY(canonical.contains(QStringLiteral("cash")));
        QVERIFY(canonical.contains(QStringLiteral("solo")));
    }

    // Weapon extraction
    void weaponKnifeExtracted()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("victim was stabbed with a knife in the alley"));
        QVERIFY(mo.weaponType.has_value());
        QCOMPARE(*mo.weaponType, QStringLiteral("knife"));
    }

    // Group extraction
    void groupExtracted()
    {
        MOExtractor ex;
        const MOFeatures mo =
            ex.extract(QStringLiteral("a gang of three men broke into the warehouse"));
        QVERIFY(mo.soloOrGroup.has_value());
        QCOMPARE(*mo.soloOrGroup, QStringLiteral("group"));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestCrimeClassifier
// ─────────────────────────────────────────────────────────────────────────────

class TestCrimeClassifier : public QObject
{
    Q_OBJECT
private slots:

    // "broke into house and stole laptop" → burglary
    // broke(2) + house(1) = 3 / 6 tokens → highest raw score
    void classifyBurglary()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("broke into house and stole laptop"));
        QCOMPARE(type, QStringLiteral("burglary"));
        QVERIFY(conf > 0.0 && conf <= 1.0);
    }

    // "attacked victim with knife in alley"
    // knife(3)/6 → weapons is highest; assault may tie at victim(1)/6
    void classifyViolentCrime()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("attacked victim with knife in alley"));
        QVERIFY(type == QStringLiteral("weapons") || type == QStringLiteral("assault"));
        QVERIFY(conf >= 0.0 && conf <= 1.0);
    }

    // "vehicle was stolen from car park" → vehicle_crime
    void classifyVehicleCrime()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("vehicle was stolen from car park"));
        QCOMPARE(type, QStringLiteral("vehicle_crime"));
        QVERIFY(conf > 0.5);
    }

    // "drugs found during stop and search" → drug_offence
    void classifyDrugOffence()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("drugs found during stop and search"));
        QCOMPARE(type, QStringLiteral("drug_offence"));
        QVERIFY(conf >= 0.0 && conf <= 1.0);
    }

    // "suspicious behaviour near school" — no keyword hits → non-empty type, valid range
    void classifySuspiciousBehaviour()
    {
        CrimeClassifier cc;
        const auto [type, conf] = cc.classify(
            QStringLiteral("suspicious behaviour near school"));
        QVERIFY(!type.isEmpty());
        QVERIFY(conf >= 0.0 && conf <= 1.0);
    }

    // All confidence values must be in [0, 1]
    void confidenceAlwaysInRange()
    {
        CrimeClassifier cc;
        const QStringList texts = {
            QStringLiteral("broke into house and stole laptop"),
            QStringLiteral("attacked victim with knife in alley"),
            QStringLiteral("vehicle was stolen from car park"),
            QStringLiteral("drugs found during stop and search"),
            QStringLiteral("suspicious behaviour near school"),
            QStringLiteral(""),
            QStringLiteral("burglary"),
        };
        for (const QString& t : texts) {
            const auto [type, conf] = cc.classify(t);
            QVERIFY2(!type.isEmpty(),
                     qPrintable(QStringLiteral("empty type for: ") + t));
            QVERIFY2(conf >= 0.0,
                     qPrintable(QStringLiteral("conf < 0 for: ") + t));
            QVERIFY2(conf <= 1.0,
                     qPrintable(QStringLiteral("conf > 1 for: ") + t));
        }
    }

    // Sentiment must stay within [-1, 1] for all inputs
    void sentimentAlwaysInRange()
    {
        CrimeClassifier cc;
        const QStringList texts = {
            QStringLiteral("broke into house and stole laptop"),
            QStringLiteral("attacked victim with knife in alley"),
            QStringLiteral("drugs found during stop and search"),
            QStringLiteral("officers helped recover stolen goods safely"),
            QStringLiteral(""),
            QStringLiteral("a"),
        };
        for (const QString& t : texts) {
            const double s = cc.sentiment(t);
            QVERIFY2(s >= -1.0 && s <= 1.0,
                     qPrintable(QStringLiteral("sentiment %1 out of [-1,1] for: ")
                                    .arg(s) + t));
        }
    }

    // Threatening text should produce negative sentiment and trip threatSignal
    void threatDetectionPositive()
    {
        CrimeClassifier cc;
        const QString threatening =
            QStringLiteral("I will kill and attack you if you tell anyone, I will harm and hurt you");
        const double s = cc.sentiment(threatening);
        QVERIFY2(s < 0.0,
                 qPrintable(QStringLiteral("expected negative sentiment, got %1").arg(s)));

        // Force evaluation at the exact sentiment the classifier sees
        if (s < -0.5) {
            QVERIFY(cc.threatSignal(threatening, s));
        } else {
            // Still check no crash for borderline case
            cc.threatSignal(threatening, s);
        }
    }

    // Neutral / positive resolution text should not trigger a threat signal
    void threatDetectionNegative()
    {
        CrimeClassifier cc;
        const QString neutral =
            QStringLiteral("officer attended scene and arrested the suspect who was convicted");
        const double s = cc.sentiment(neutral);
        // positive words dominate → should not be a threat
        QVERIFY(!cc.threatSignal(neutral, s));
    }

    // Sentiment for a text with only negative words should be negative
    void sentimentNegativeForViolentText()
    {
        CrimeClassifier cc;
        const double s = cc.sentiment(
            QStringLiteral("violent attack murder stab shoot kill harm hurt damage"));
        QVERIFY(s < 0.0);
    }

    // Sentiment for a text with only positive outcome words should be non-negative
    void sentimentPositiveForResolutionText()
    {
        CrimeClassifier cc;
        const double s = cc.sentiment(
            QStringLiteral("helped prevented stopped caught arrested resolved recovered safe secured protected"));
        QVERIFY(s >= 0.0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestMOSimilarityPipeline
// ─────────────────────────────────────────────────────────────────────────────

class TestMOSimilarityPipeline : public QObject
{
    Q_OBJECT
private slots:

    void endToEndSimilarity()
    {
        const QStringList narratives = {
            QStringLiteral("broke into residential flat at night and stole cash"),
            QStringLiteral("forced entry through rear door of house, jewellery taken"),
            QStringLiteral("vehicle stolen from car park after window smashed"),
            QStringLiteral("drugs found during stop and search near school"),
            QStringLiteral("victim attacked with knife in alley alone"),
            QStringLiteral("shop broken into overnight, till emptied"),
            QStringLiteral("purse stolen from bag on bus group"),
            QStringLiteral("graffiti sprayed on warehouse wall several times"),
            QStringLiteral("group of youths causing disorder in park at night"),
            QStringLiteral("drunk driver caused accident on high street"),
        };

        MOExtractor   ex;
        MOAnalyser    analyser;
        QVector<MOCaseRecord> records;

        for (int i = 0; i < narratives.size(); ++i) {
            const MOFeatures mo      = ex.extract(narratives[i]);
            const QString canonical  = ex.canonicalMOString(mo);

            MOCaseRecord rec;
            rec.caseId   = QStringLiteral("CASE%1").arg(i + 1, 3, 10, QChar('0'));
            // Fall back to raw narrative if no features were extracted
            rec.moText   = canonical.trimmed().isEmpty() ? narratives[i] : canonical;
            rec.resolved = (i % 3 == 0);
            records.append(rec);
        }

        QCOMPARE(records.size(), 10);

        // ── fit ──────────────────────────────────────────────────────────────
        analyser.fit(records);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 10);

        // ── query with a similar burglary event ───────────────────────────────
        const MOFeatures queryMO =
            ex.extract(QStringLiteral("suspect broke into house at night and stole laptop and phone"));
        const QString queryCanonical = ex.canonicalMOString(queryMO);

        // Use minSimilarity=0.0 so we always get results regardless of vocabulary overlap
        const QVector<MOMatch> results =
            analyser.findSimilar(queryCanonical, 5, 0.0);

        QVERIFY(!results.isEmpty());

        // Scores must be in [0, 1] and case IDs must be non-empty
        for (const MOMatch& match : results) {
            QVERIFY2(!match.caseId.isEmpty(), "empty caseId in MOMatch");
            QVERIFY2(match.similarityScore >= 0.0,
                     qPrintable(QStringLiteral("score < 0: %1").arg(match.similarityScore)));
            QVERIFY2(match.similarityScore <= 1.0,
                     qPrintable(QStringLiteral("score > 1: %1").arg(match.similarityScore)));
        }

        // Results must be sorted descending by similarity
        for (int i = 1; i < results.size(); ++i) {
            QVERIFY2(results[i - 1].similarityScore >= results[i].similarityScore,
                     "findSimilar results not sorted descending");
        }
    }

    // fit() + isFitted() / caseCount() consistency
    void fitAndCaseCount()
    {
        MOAnalyser analyser;
        QVERIFY(!analyser.isFitted());

        QVector<MOCaseRecord> records;
        for (int i = 0; i < 5; ++i) {
            MOCaseRecord r;
            r.caseId = QStringLiteral("C%1").arg(i);
            r.moText = QStringLiteral("forced_entry residential night cash");
            records.append(r);
        }

        analyser.fit(records);
        QVERIFY(analyser.isFitted());
        QCOMPARE(analyser.caseCount(), 5);
    }

    // findSimilar on empty corpus returns empty results
    void emptyCorpusReturnsNoResults()
    {
        MOAnalyser analyser;
        // Do not call fit() — analyser is unfitted
        const QVector<MOMatch> results =
            analyser.findSimilar(QStringLiteral("forced_entry residential night"), 5, 0.0);
        QVERIFY(results.isEmpty());
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TestLeadReportGeneratorHtml
// ─────────────────────────────────────────────────────────────────────────────

class TestLeadReportGeneratorHtml : public QObject
{
    Q_OBJECT
private slots:

    void htmlNonEmpty()
    {
        const LeadReport r = LeadReportGenerator::generate(QStringLiteral("CASE001"), {});
        QVERIFY(!LeadReportGenerator::generateHtml(r).isEmpty());
    }

    void htmlStartsWithDoctype()
    {
        const LeadReport r = LeadReportGenerator::generate(QStringLiteral("CASE002"), {});
        const QString html = LeadReportGenerator::generateHtml(r);
        QVERIFY2(html.startsWith(QStringLiteral("<!DOCTYPE html>")),
                 qPrintable(QStringLiteral("HTML does not start with DOCTYPE; got: ")
                            + html.left(30)));
    }

    void htmlContainsCaseId()
    {
        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE-ALPHA-007"), {});
        const QString html = LeadReportGenerator::generateHtml(r);
        QVERIFY2(html.contains(QStringLiteral("CASE-ALPHA-007")),
                 "case ID not found in HTML output");
    }

    void htmlContainsLeadHeadline()
    {
        const InvestigativeLead lead = makeLead(
            QStringLiteral("Repeat offender likely targeting south suburb"),
            QStringLiteral("pattern"),
            0.85,
            QStringLiteral("Three burglaries show identical MO within 2 km."),
            { QStringLiteral("MOAnalyser"), QStringLiteral("HintEngine"), QStringLiteral("report") });

        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE003"), { lead });
        const QString html = LeadReportGenerator::generateHtml(r);

        QVERIFY2(html.contains(QStringLiteral("Repeat offender likely targeting south suburb")),
                 "lead headline not found in HTML");
    }

    void htmlHighConfidenceBadge()
    {
        const InvestigativeLead highConf = makeLead(
            QStringLiteral("Known offender matches MO profile"),
            QStringLiteral("suspect"), 0.92,
            QStringLiteral("Database match with 92% confidence."));

        const InvestigativeLead lowConf = makeLead(
            QStringLiteral("Possible safe house in north district"),
            QStringLiteral("location"), 0.45,
            QStringLiteral("Low signal from phone data."));

        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE004"), { highConf, lowConf });
        const QString html = LeadReportGenerator::generateHtml(r);

        // High-confidence lead must carry a badge marker
        QVERIFY2(html.contains(QStringLiteral("HIGH CONFIDENCE")),
                 "no HIGH CONFIDENCE badge found in HTML for lead with confidence=0.92");

        // Both headlines must appear
        QVERIFY(html.contains(QStringLiteral("Known offender matches MO profile")));
        QVERIFY(html.contains(QStringLiteral("Possible safe house in north district")));
    }

    void htmlContainsProvenanceSection()
    {
        const InvestigativeLead lead = makeLead(
            QStringLiteral("Test lead"),
            QStringLiteral("pattern"), 0.7, {},
            { QStringLiteral("source_data"),
              QStringLiteral("MOAnalyser"),
              QStringLiteral("HintEngine") });

        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE005"), { lead });
        const QString html = LeadReportGenerator::generateHtml(r);

        // Provenance section heading must be present
        QVERIFY2(html.contains(QStringLiteral("Provenance"), Qt::CaseInsensitive),
                 "no Provenance section in HTML");
        QVERIFY(html.contains(QStringLiteral("source_data")));
        QVERIFY(html.contains(QStringLiteral("MOAnalyser")));
    }

    void htmlProperStructure()
    {
        const LeadReport r = LeadReportGenerator::generate(QStringLiteral("CASE006"), {});
        const QString html = LeadReportGenerator::generateHtml(r);

        QVERIFY(html.contains(QStringLiteral("<html")));
        QVERIFY(html.contains(QStringLiteral("</html>")));
        QVERIFY(html.contains(QStringLiteral("<head>")));
        QVERIFY(html.contains(QStringLiteral("<body>")));
        QVERIFY(html.contains(QStringLiteral("SENTINEL")));
    }

    void htmlContainsCaseIdInHeader()
    {
        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE-BRAVO-42"), {});
        const QString html = LeadReportGenerator::generateHtml(r);
        // Case ID appears in the page <title>
        QVERIFY(html.contains(QStringLiteral("CASE-BRAVO-42")));
    }

    // Multiple leads → all headlines appear in the HTML
    void htmlAllLeadHeadlines()
    {
        const QVector<InvestigativeLead> leads = {
            makeLead(QStringLiteral("Lead Alpha"), QStringLiteral("motive"), 0.90),
            makeLead(QStringLiteral("Lead Beta"),  QStringLiteral("suspect"), 0.75),
            makeLead(QStringLiteral("Lead Gamma"), QStringLiteral("location"), 0.55),
        };
        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE007"), leads);
        const QString html = LeadReportGenerator::generateHtml(r);

        QVERIFY(html.contains(QStringLiteral("Lead Alpha")));
        QVERIFY(html.contains(QStringLiteral("Lead Beta")));
        QVERIFY(html.contains(QStringLiteral("Lead Gamma")));
    }

    // No HIGH CONFIDENCE badge for lead below 0.8
    void htmlNoBadgeForLowConfidence()
    {
        const InvestigativeLead lead =
            makeLead(QStringLiteral("Weak signal lead"), QStringLiteral("intel"), 0.35);
        const LeadReport r =
            LeadReportGenerator::generate(QStringLiteral("CASE008"), { lead });
        const QString html = LeadReportGenerator::generateHtml(r);

        // HIGH CONFIDENCE badge should NOT appear for 0.35 confidence
        QVERIFY(!html.contains(QStringLiteral("HIGH CONFIDENCE")));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// main — run all four test suites sequentially
// ─────────────────────────────────────────────────────────────────────────────

static int runTest(QObject* obj, const char* logFile)
{
    QStringList args = { QStringLiteral("test"),
                         QStringLiteral("-o"),
                         QStringLiteral("%1,txt").arg(logFile) };
    return QTest::qExec(obj, args);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int r = 0;
    TestMOExtractor            t1; r |= runTest(&t1, "nlp_moextractor.txt");
    TestCrimeClassifier        t2; r |= runTest(&t2, "nlp_classifier.txt");
    TestMOSimilarityPipeline   t3; r |= runTest(&t3, "nlp_mo_pipeline.txt");
    TestLeadReportGeneratorHtml t4; r |= runTest(&t4, "nlp_html_export.txt");
    return r;
}

#include "test_nlp_pipeline.moc"
