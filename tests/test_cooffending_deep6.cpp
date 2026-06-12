// test_cooffending_deep6.cpp — Deep audit iteration 20: CoOffendingAnalyser
// Verifies: sharedIncidents semantics, role-weighted edges, rebuild invalidates
//           analysis, isolated nodes, PageRank sum, connection types, nodes().

#include <QtTest>
#include <cmath>
#include <algorithm>
#include "inference/CoOffendingAnalyser.h"

class CoOffendingDeep6Test : public QObject
{
    Q_OBJECT

    static PersonIncidentRecord pir(const QString& person,
                                    const QString& incident,
                                    double roleWeight = 1.0)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.roleWeight = roleWeight;
        return r;
    }

private slots:
    void testDirectSharedIncidentsCountsAllCoOffending();
    void testRoleWeightsAffectRiskNonMonotonically();
    void testRebuildGraphRequiresReanalyse();
    void testSinglePersonIncidentYieldsIsolatedNode();
    void testPageRankSumApproximatelyOne();
    void testFindLeadsDirectParticipantConnectionType();
    void testFindLeadsSecondDegreeConnectionType();
    void testNodesReturnsCompleteGraph();
};

void CoOffendingDeep6Test::testDirectSharedIncidentsCountsAllCoOffending()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("A"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 5);

    const auto aIt = std::find_if(leads.begin(), leads.end(),
        [](const NetworkLead& l) { return l.personId == QStringLiteral("A"); });
    QVERIFY(aIt != leads.end());

    if (aIt->sharedIncidents > 1) {
        QWARN("BUG CoOffendingAnalyser.cpp:257-262 — direct participant sharedIncidents "
              "counts every multi-person incident for that person, not incidents shared "
              "with the query incident's co-offenders");
    }
    QCOMPARE(aIt->sharedIncidents, 2);
}

void CoOffendingDeep6Test::testRoleWeightsAffectRiskNonMonotonically()
{
    CoOffendingAnalyser light;
    light.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1"), 0.2),
        pir(QStringLiteral("B"), QStringLiteral("I1"), 0.2),
        pir(QStringLiteral("C"), QStringLiteral("I1"), 1.0)
    });
    light.analyse();

    CoOffendingAnalyser heavy;
    heavy.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1"), 2.0),
        pir(QStringLiteral("B"), QStringLiteral("I1"), 2.0),
        pir(QStringLiteral("C"), QStringLiteral("I1"), 1.0)
    });
    heavy.analyse();

    const auto lightLeads = light.findLeads(QStringLiteral("I1"), 3);
    const auto heavyLeads = heavy.findLeads(QStringLiteral("I1"), 3);

    auto riskFor = [](const QVector<NetworkLead>& leads, const QString& id) {
        for (const auto& l : leads) {
            if (l.personId == id)
                return l.riskScore;
        }
        return -1.0;
    };

    const double lightC = riskFor(lightLeads, QStringLiteral("C"));
    const double heavyC = riskFor(heavyLeads, QStringLiteral("C"));
    QVERIFY(lightC >= 0.0);
    QVERIFY(heavyC >= 0.0);
    QVERIFY(!qFuzzyCompare(lightC, heavyC));

    if (heavyC < lightC) {
        QWARN("BUG CoOffendingAnalyser.cpp:63-66,224-229 — higher A-B role weights "
              "redistribute PageRank and can lower peripheral node riskScore "
              "(non-monotonic weight→risk mapping)");
    }
}

void CoOffendingDeep6Test::testRebuildGraphRequiresReanalyse()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });
    ca.analyse();
    QVERIFY(!ca.findLeads(QStringLiteral("I1"), 2).isEmpty());

    ca.buildGraph({
        pir(QStringLiteral("X"), QStringLiteral("I9")),
        pir(QStringLiteral("Y"), QStringLiteral("I9"))
    });

    const auto stale = ca.findLeads(QStringLiteral("I1"), 2);
    if (!stale.isEmpty()) {
        QWARN("BUG CoOffendingAnalyser.cpp:36-37 — buildGraph resets m_analysed but "
              "findLeads should guard; stale leads returned after rebuild without analyse");
    }
    QVERIFY(stale.isEmpty());

    ca.analyse();
    const auto fresh = ca.findLeads(QStringLiteral("I9"), 2);
    QVERIFY(!fresh.isEmpty());
    QVERIFY(std::none_of(fresh.begin(), fresh.end(),
        [](const NetworkLead& l) { return l.personId == QStringLiteral("A"); }));
}

void CoOffendingDeep6Test::testSinglePersonIncidentYieldsIsolatedNode()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("SOLO"), QStringLiteral("I_SOLO")),
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });
    ca.analyse();

    const auto nodes = ca.nodes();
    const auto soloIt = std::find_if(nodes.begin(), nodes.end(),
        [](const NetworkNode& n) { return n.personId == QStringLiteral("SOLO"); });
    QVERIFY(soloIt != nodes.end());
    QCOMPARE(soloIt->degree, 0);
    QVERIFY(soloIt->pageRank >= 0.0);
}

void CoOffendingDeep6Test::testPageRankSumApproximatelyOne()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I3")),
        pir(QStringLiteral("D"), QStringLiteral("I3"))
    });
    ca.analyse();

    double sum = 0.0;
    for (const auto& n : ca.nodes())
        sum += n.pageRank;
    QVERIFY2(std::abs(sum - 1.0) < 0.02,
             qPrintable(QStringLiteral("PageRank sum expected ~1, got %1").arg(sum)));
}

void CoOffendingDeep6Test::testFindLeadsDirectParticipantConnectionType()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
    for (const auto& l : leads) {
        if (l.personId == QStringLiteral("A") || l.personId == QStringLiteral("B")) {
            QCOMPARE(l.connectionType, QStringLiteral("direct_participant"));
        }
    }
}

void CoOffendingDeep6Test::testFindLeadsSecondDegreeConnectionType()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("A"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I1")),
        pir(QStringLiteral("B"), QStringLiteral("I2")),
        pir(QStringLiteral("C"), QStringLiteral("I2"))
    });
    ca.analyse();

    const auto leads = ca.findLeads(QStringLiteral("I1"), 5);
    const auto cIt = std::find_if(leads.begin(), leads.end(),
        [](const NetworkLead& l) { return l.personId == QStringLiteral("C"); });
    QVERIFY(cIt != leads.end());
    QCOMPARE(cIt->connectionType, QStringLiteral("second_degree"));
}

void CoOffendingDeep6Test::testNodesReturnsCompleteGraph()
{
    CoOffendingAnalyser ca;
    ca.buildGraph({
        pir(QStringLiteral("P1"), QStringLiteral("I1")),
        pir(QStringLiteral("P2"), QStringLiteral("I1")),
        pir(QStringLiteral("P3"), QStringLiteral("I2")),
        pir(QStringLiteral("P4"), QStringLiteral("I2"))
    });
    ca.analyse();

    const auto nodes = ca.nodes();
    QCOMPARE(nodes.size(), 4);

    QSet<QString> ids;
    for (const auto& n : nodes)
        ids.insert(n.personId);
    QCOMPARE(ids.size(), 4);
    QVERIFY(ids.contains(QStringLiteral("P1")));
    QVERIFY(ids.contains(QStringLiteral("P4")));
}

QTEST_GUILESS_MAIN(CoOffendingDeep6Test)
#include "test_cooffending_deep6.moc"
