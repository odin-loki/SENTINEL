// test_series_cooffend_deep.cpp
// Iteration-7 deep audit tests for SeriesDetector and CoOffendingAnalyser.
#include <QTest>
#include "models/SeriesDetector.h"
#include "inference/CoOffendingAnalyser.h"
#include "core/CrimeEvent.h"
#include <cmath>
#include <algorithm>
#include <numeric>

class SeriesCooffendDeepTest : public QObject
{
    Q_OBJECT

private:
    static SeriesEvent sev(const QString& id, double lat, double lon, double tDays,
                           const QString& crimeType = QStringLiteral("burglary"),
                           const QString& mo = QStringLiteral("forced entry night"))
    {
        SeriesEvent e;
        e.eventId    = id;
        e.lat        = lat;
        e.lon        = lon;
        e.tDays      = tDays;
        e.crimeType  = crimeType;
        e.moText     = mo;
        return e;
    }

    static PersonIncidentRecord pir(const QString& person, const QString& incident,
                                   double weight = 1.0)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.role       = QStringLiteral("suspect");
        r.roleWeight = weight;
        return r;
    }

private slots:

    // ── SeriesDetector ────────────────────────────────────────────────────────

    void testHaversineKmKnownDistance()
    {
        // London (51.5074, -0.1278) → Paris (48.8566, 2.3522) ≈ 342 km
        const double d = SeriesDetector::haversineKm(51.5074, -0.1278, 48.8566, 2.3522);
        QVERIFY2(d > 330.0 && d < 355.0,
                 qPrintable(QStringLiteral("London–Paris expected ~342 km, got %1").arg(d)));
    }

    void testHaversineKmSamePoint()
    {
        const double d = SeriesDetector::haversineKm(51.5, -0.1, 51.5, -0.1);
        QVERIFY2(d < 1e-6,
                 qPrintable(QStringLiteral("Same point haversine should be 0, got %1").arg(d)));
    }

    void testMOJaccardIdentical()
    {
        const QString mo = QStringLiteral("forced entry residential night");
        const double j = SeriesDetector::moJaccard(mo, mo);
        QVERIFY2(std::abs(j - 1.0) < 1e-9,
                 qPrintable(QStringLiteral("Identical MO Jaccard should be 1.0, got %1").arg(j)));
    }

    void testMOJaccardEmpty()
    {
        const double j = SeriesDetector::moJaccard(QString{}, QString{});
        QVERIFY2(std::abs(j) < 1e-9,
                 qPrintable(QStringLiteral("Both-empty MO Jaccard should be 0.0, got %1").arg(j)));
    }

    void testMOJaccardDisjoint()
    {
        const double j = SeriesDetector::moJaccard(
            QStringLiteral("forced entry residential"),
            QStringLiteral("vehicle daytime weapon"));
        QVERIFY2(std::abs(j) < 1e-9,
                 qPrintable(QStringLiteral("Disjoint MO Jaccard should be 0.0, got %1").arg(j)));
    }

    void testNearRepeatForBurglary()
    {
        const auto nr = SeriesDetector::nearRepeatFor(QStringLiteral("burglary"));
        QVERIFY2(nr.distM > 0.0,
                 qPrintable(QStringLiteral("Burglary distM should be > 0, got %1").arg(nr.distM)));
        QVERIFY2(nr.days > 0.0,
                 qPrintable(QStringLiteral("Burglary days should be > 0, got %1").arg(nr.days)));
        QVERIFY2(nr.multiplier > 1.0,
                 qPrintable(QStringLiteral("Burglary multiplier should be > 1, got %1").arg(nr.multiplier)));
    }

    void testNearRepeatForUnknown()
    {
        const auto nr = SeriesDetector::nearRepeatFor(QStringLiteral("totally_unknown_crime_xyz"));
        QVERIFY2(nr.distM > 0.0,
                 qPrintable(QStringLiteral("Unknown type default distM should be > 0, got %1").arg(nr.distM)));
        QVERIFY2(nr.days > 0.0,
                 qPrintable(QStringLiteral("Unknown type default days should be > 0, got %1").arg(nr.days)));
    }

    void testLinkCompositeScoreRange()
    {
        SeriesDetector sd;
        CrimeSeries series;
        series.seriesId = QStringLiteral("SERIES-0001");
        series.dominantCrimeType = QStringLiteral("burglary");
        series.members = {
            sev(QStringLiteral("E1"), 51.5, -0.1, 0.0),
            sev(QStringLiteral("E2"), 51.501, -0.101, 2.0),
            sev(QStringLiteral("E3"), 51.502, -0.102, 5.0),
        };

        const SeriesEvent candidate = sev(QStringLiteral("NEW"), 51.5005, -0.1005, 3.0);
        const SeriesMatch match = sd.linkProbability(candidate, series, 0.8);

        QVERIFY2(match.compositeScore >= 0.0 && match.compositeScore <= 1.0,
                 qPrintable(QStringLiteral("Composite score must be in [0,1], got %1")
                                .arg(match.compositeScore)));
    }

    void testDBSCANCluster()
    {
        SeriesDetector sd(0.5, 20.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 5; ++i)
            evs.append(sev(QStringLiteral("C%1").arg(i),
                           51.5 + i * 0.001, -0.1 + i * 0.001,
                           static_cast<double>(i)));

        const auto result = sd.detectSeries(evs);
        QVERIFY2(result.size() == 1,
                 qPrintable(QStringLiteral("Expected 1 series from 5 close events, got %1")
                                .arg(result.size())));
        QVERIFY2(result.first().members.size() == 5,
                 qPrintable(QStringLiteral("Expected 5 members in cluster, got %1")
                                .arg(result.first().members.size())));
    }

    void testDBSCANNoise()
    {
        SeriesDetector sd(0.3, 14.0, 3);
        QVector<SeriesEvent> evs;
        for (int i = 0; i < 4; ++i)
            evs.append(sev(QStringLiteral("C%1").arg(i),
                           51.5 + i * 0.001, -0.1,
                           static_cast<double>(i)));
        evs.append(sev(QStringLiteral("NOISE"), 55.0, 2.0, 100.0));

        const auto result = sd.detectSeries(evs);
        QVERIFY2(!result.isEmpty(), "Clustered events should form a series");
        int totalMembers = 0;
        for (const auto& s : result)
            totalMembers += s.members.size();
        QVERIFY2(totalMembers == 4,
                 qPrintable(QStringLiteral("Isolated event should be noise; expected 4 members, got %1")
                                .arg(totalMembers)));
    }

    // ── CoOffendingAnalyser ─────────────────────────────────────────────────

    void testCooffendingTwoPersons()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("P1"), QStringLiteral("I1")),
            pir(QStringLiteral("P2"), QStringLiteral("I1")),
        });
        ca.analyse();

        const auto ns = ca.nodes();
        QCOMPARE(ns.size(), 2);
        const auto p1 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("P1"); });
        QVERIFY(p1 != ns.end());
        QVERIFY2(p1->neighbours.contains(QStringLiteral("P2")),
                 "Two persons in same incident should be linked");
    }

    void testCooffendingPageRankSums()
    {
        CoOffendingAnalyser ca;
        QVector<PersonIncidentRecord> recs;
        recs.append(pir(QStringLiteral("H"), QStringLiteral("I1")));
        for (int i = 1; i <= 4; ++i)
            recs.append(pir(QStringLiteral("L%1").arg(i), QStringLiteral("I1")));
        ca.buildGraph(recs);
        ca.analyse();

        double sum = 0.0;
        for (const auto& n : ca.nodes()) sum += n.pageRank;
        QVERIFY2(std::abs(sum - 1.0) < 0.05,
                 qPrintable(QStringLiteral("PageRank sum should be ~1.0, got %1").arg(sum)));
    }

    void testCooffendingBetweennessRange()
    {
        CoOffendingAnalyser ca;
        QVector<PersonIncidentRecord> recs;
        const QVector<QString> nodes = {
            QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
            QStringLiteral("D"), QStringLiteral("E")
        };
        for (int i = 0; i < nodes.size() - 1; ++i) {
            const QString inc = QStringLiteral("CI%1").arg(i);
            recs.append(pir(nodes[i], inc));
            recs.append(pir(nodes[i + 1], inc));
        }
        ca.buildGraph(recs);
        ca.analyse();

        for (const auto& n : ca.nodes()) {
            QVERIFY2(n.betweenness >= 0.0 && n.betweenness <= 1.0,
                     qPrintable(QStringLiteral("%1 betweenness %2 out of [0,1]")
                                    .arg(n.personId).arg(n.betweenness)));
        }
    }

    void testCooffendingCommunityAssignment()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("P1"), QStringLiteral("I1")),
            pir(QStringLiteral("P2"), QStringLiteral("I1")),
            pir(QStringLiteral("P3"), QStringLiteral("I2")),
            pir(QStringLiteral("P4"), QStringLiteral("I2")),
        });
        ca.analyse();

        for (const auto& n : ca.nodes())
            QVERIFY2(n.communityId >= 0,
                     qPrintable(QStringLiteral("%1 communityId should be >= 0, got %2")
                                    .arg(n.personId).arg(n.communityId)));
    }

    void testCooffendingDisconnectedGraph()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("A1"), QStringLiteral("G1")),
            pir(QStringLiteral("A2"), QStringLiteral("G1")),
            pir(QStringLiteral("B1"), QStringLiteral("G2")),
            pir(QStringLiteral("B2"), QStringLiteral("G2")),
        });
        ca.analyse();

        const auto ns = ca.nodes();
        const auto a1 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("A1"); });
        const auto b1 = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("B1"); });
        QVERIFY(a1 != ns.end() && b1 != ns.end());
        QVERIFY2(a1->communityId != b1->communityId,
                 "Disconnected cliques should belong to different communities");
    }

    void testCooffendingHighCentralityFirst()
    {
        CoOffendingAnalyser ca;
        QVector<PersonIncidentRecord> recs;
        recs.append(pir(QStringLiteral("HUB"), QStringLiteral("I1")));
        for (int i = 1; i <= 5; ++i) {
            const QString inc = QStringLiteral("I%1").arg(i);
            recs.append(pir(QStringLiteral("HUB"), inc));
            recs.append(pir(QStringLiteral("L%1").arg(i), inc));
        }
        ca.buildGraph(recs);
        ca.analyse();

        const auto ns = ca.nodes();
        const auto hub = std::find_if(ns.begin(), ns.end(),
            [](const NetworkNode& n) { return n.personId == QStringLiteral("HUB"); });
        QVERIFY(hub != ns.end());

        const auto maxNode = std::max_element(ns.begin(), ns.end(),
            [](const NetworkNode& a, const NetworkNode& b) {
                return a.pageRank < b.pageRank;
            });
        QVERIFY2(hub->pageRank >= maxNode->pageRank * 0.9,
                 qPrintable(QStringLiteral("Hub PageRank %1 should be highest (max %2)")
                                .arg(hub->pageRank).arg(maxNode->pageRank)));
        QVERIFY2(hub->degree >= maxNode->degree,
                 "Most connected node should have highest degree");
    }

    void testCooffendingRiskScorePositive()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("P1"), QStringLiteral("I1")),
            pir(QStringLiteral("P2"), QStringLiteral("I1")),
            pir(QStringLiteral("P2"), QStringLiteral("I2")),
            pir(QStringLiteral("P3"), QStringLiteral("I2")),
        });
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        QVERIFY(!leads.isEmpty());
        for (const auto& lead : leads)
            QVERIFY2(lead.riskScore > 0.0,
                     qPrintable(QStringLiteral("%1 riskScore should be > 0, got %2")
                                    .arg(lead.personId).arg(lead.riskScore)));
    }

    void testCooffendingLeadsRanked()
    {
        CoOffendingAnalyser ca;
        QVector<PersonIncidentRecord> recs;
        recs.append(pir(QStringLiteral("HUB"), QStringLiteral("I1")));
        for (int i = 1; i <= 4; ++i) {
            const QString inc = QStringLiteral("I%1").arg(i);
            recs.append(pir(QStringLiteral("HUB"), inc));
            recs.append(pir(QStringLiteral("L%1").arg(i), inc));
        }
        ca.buildGraph(recs);
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        QVERIFY(leads.size() >= 2);
        for (int i = 0; i + 1 < leads.size(); ++i) {
            QVERIFY2(leads[i].riskScore >= leads[i + 1].riskScore,
                     qPrintable(QStringLiteral("Leads should be sorted by risk descending: %1 < %2")
                                    .arg(leads[i].riskScore).arg(leads[i + 1].riskScore)));
            QVERIFY2(leads[i].centralityScore >= leads[i + 1].centralityScore * 0.5,
                     "Higher-ranked leads should have comparable or higher centrality");
        }
    }

    void testCooffendingSingleNodeNoNetwork()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({ pir(QStringLiteral("SOLO"), QStringLiteral("I1")) });
        ca.analyse();

        QCOMPARE(ca.nodes().size(), 1);
        const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
        QVERIFY2(leads.size() <= 1,
                 qPrintable(QStringLiteral("Single-node network should yield 0 or 1 lead, got %1")
                                .arg(leads.size())));
        if (!leads.isEmpty())
            QCOMPARE(leads.first().personId, QStringLiteral("SOLO"));
    }

    void testCooffendingTriangleStrong()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("T1"), QStringLiteral("TRI")),
            pir(QStringLiteral("T2"), QStringLiteral("TRI")),
            pir(QStringLiteral("T3"), QStringLiteral("TRI")),
        });
        ca.analyse();

        const auto ns = ca.nodes();
        QCOMPARE(ns.size(), 3);
        const int community = ns.first().communityId;
        for (const auto& n : ns) {
            QVERIFY2(n.communityId == community,
                     "Mutually-connected triangle should share one community");
            QVERIFY2(n.degree == 2,
                     qPrintable(QStringLiteral("%1 should have degree 2 in K3, got %2")
                                    .arg(n.personId).arg(n.degree)));
        }
    }
};

QTEST_GUILESS_MAIN(SeriesCooffendDeepTest)
#include "test_series_cooffend_deep.moc"
