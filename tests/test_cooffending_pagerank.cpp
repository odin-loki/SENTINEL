// test_cooffending_pagerank.cpp
// CoOffendingAnalyser PageRank, betweenness, community detection,
// large network, and findLeads tests.
#include <QTest>
#include "inference/CoOffendingAnalyser.h"
#include <cmath>
#include <numeric>

class CoOffendingPageRankTest : public QObject
{
    Q_OBJECT

private:
    using PIR = PersonIncidentRecord;

    // Star graph: hub "H" connected to 5 leaves via shared incidents
    static QVector<PIR> starGraph()
    {
        QVector<PIR> recs;
        for (int i = 1; i <= 5; ++i) {
            const QString leaf = QStringLiteral("L%1").arg(i);
            const QString inc  = QStringLiteral("INC%1").arg(i);
            recs.append({ QStringLiteral("H"), inc, QStringLiteral("suspect"), 1.0 });
            recs.append({ leaf,                inc, QStringLiteral("suspect"), 1.0 });
        }
        return recs;
    }

    // Chain: A-B-C-D-E (each pair shares one incident)
    static QVector<PIR> chainGraph()
    {
        const QVector<QString> nodes = {
            QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
            QStringLiteral("D"), QStringLiteral("E")
        };
        QVector<PIR> recs;
        for (int i = 0; i < nodes.size() - 1; ++i) {
            const QString inc = QStringLiteral("CI%1").arg(i);
            recs.append({ nodes[i],   inc, QStringLiteral("suspect"), 1.0 });
            recs.append({ nodes[i+1], inc, QStringLiteral("suspect"), 1.0 });
        }
        return recs;
    }

private slots:

    // 1. isBuilt() false before buildGraph
    void testNotBuiltBeforeBuild()
    {
        CoOffendingAnalyser ca;
        QVERIFY(!ca.isBuilt());
    }

    // 2. isBuilt() true after buildGraph
    void testBuiltAfterBuildGraph()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        QVERIFY(ca.isBuilt());
    }

    // 3. nodes() count == 6 for star (1 hub + 5 leaves)
    void testStarNodeCount()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        QCOMPARE(ca.nodes().size(), 6);
    }

    // 4. Hub has highest PageRank in star graph
    void testHubHighestPageRankInStar()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        const auto allNodes = ca.nodes();
        double hubPR = 0.0;
        for (const auto& n : allNodes)
            if (n.personId == QStringLiteral("H")) hubPR = n.pageRank;
        for (const auto& n : allNodes)
            if (n.personId != QStringLiteral("H"))
                QVERIFY2(hubPR >= n.pageRank,
                         qPrintable(QStringLiteral("Hub PR %1 should >= leaf PR %2 for %3")
                            .arg(hubPR).arg(n.pageRank).arg(n.personId)));
    }

    // 5. PageRank sum approximately equals 1.0 (normalised)
    void testPageRankSumIsOne()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        double total = 0.0;
        for (const auto& n : ca.nodes()) total += n.pageRank;
        QVERIFY2(std::abs(total - 1.0) < 0.1,
                 qPrintable(QStringLiteral("PageRank sum %1 should be ~1.0").arg(total)));
    }

    // 6. Chain graph: middle node (C) has higher betweenness than endpoint (A)
    void testChainMiddleHigherBetweenness()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(chainGraph());
        ca.analyse();
        double betA = 0.0, betC = 0.0;
        for (const auto& n : ca.nodes()) {
            if (n.personId == QStringLiteral("A")) betA = n.betweenness;
            if (n.personId == QStringLiteral("C")) betC = n.betweenness;
        }
        QVERIFY2(betC >= betA,
                 qPrintable(QStringLiteral("Middle node C betweenness %1 should >= endpoint A %2")
                    .arg(betC).arg(betA)));
    }

    // 7. Community IDs all >= 0 after analyse
    void testCommunityIdsNonNegative()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        for (const auto& n : ca.nodes()) {
            QVERIFY2(n.communityId >= 0,
                     qPrintable(QStringLiteral("Node %1 communityId %2 should be >= 0")
                        .arg(n.personId).arg(n.communityId)));
        }
    }

    // 8. findLeads: returns non-empty for incident in graph
    void testFindLeadsNonEmpty()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        const auto leads = ca.findLeads(QStringLiteral("INC1"), 3);
        QVERIFY2(!leads.isEmpty(), "findLeads should return leads for known incident");
    }

    // 9. findLeads: personId non-empty
    void testFindLeadsPersonIdNonEmpty()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        const auto leads = ca.findLeads(QStringLiteral("INC1"), 5);
        for (const auto& l : leads)
            QVERIFY2(!l.personId.isEmpty(),
                     "All findLeads results should have non-empty personId");
    }

    // 10. Degree matches expected in star (hub degree == 5)
    void testStarHubDegree()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph(starGraph());
        ca.analyse();
        for (const auto& n : ca.nodes()) {
            if (n.personId == QStringLiteral("H")) {
                QCOMPARE(n.degree, 5);
            }
        }
    }
};

QTEST_MAIN(CoOffendingPageRankTest)
#include "test_cooffending_pagerank.moc"
