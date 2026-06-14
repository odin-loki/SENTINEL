// test_cooffending_deep8.cpp — Deep audit iteration 27: CoOffendingAnalyser
// topK limit, nodes export, degree counts, isolated person.
#include <QTest>
#include "inference/CoOffendingAnalyser.h"

class CoOffendingDeep8Test : public QObject
{
    Q_OBJECT

    static PersonIncidentRecord pir(const QString& person, const QString& incident)
    {
        PersonIncidentRecord r;
        r.personId   = person;
        r.incidentId = incident;
        r.roleWeight = 1.0;
        return r;
    }

private slots:

    void testFindLeadsRespectsTopK()
    {
        CoOffendingAnalyser ca;
        QVector<PersonIncidentRecord> recs;
        recs.append(pir(QStringLiteral("Hub"), QStringLiteral("I0")));
        for (int i = 1; i <= 8; ++i) {
            recs.append(pir(QStringLiteral("Hub"), QStringLiteral("I%1").arg(i)));
            recs.append(pir(QStringLiteral("S%1").arg(i), QStringLiteral("I%1")));
        }
        ca.buildGraph(recs);
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("I0"), 3);
        QVERIFY(leads.size() <= 3);
    }

    void testNodesIncludesAllPersons()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("A"), QStringLiteral("X")),
            pir(QStringLiteral("B"), QStringLiteral("X")),
            pir(QStringLiteral("C"), QStringLiteral("Y")),
        });
        ca.analyse();
        QCOMPARE(ca.nodes().size(), 3);
    }

    void testSoloIncidentProducesDirectParticipantLead()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("Solo"), QStringLiteral("ONLY")),
            pir(QStringLiteral("A"), QStringLiteral("OTHER")),
            pir(QStringLiteral("B"), QStringLiteral("OTHER")),
        });
        ca.analyse();

        const auto leads = ca.findLeads(QStringLiteral("ONLY"), 5);
        QVERIFY(!leads.isEmpty());
        QCOMPARE(leads.first().personId, QStringLiteral("Solo"));
    }

    void testDegreePositiveForCoOffenders()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("P1"), QStringLiteral("I1")),
            pir(QStringLiteral("P2"), QStringLiteral("I1")),
        });
        ca.analyse();

        for (const auto& n : ca.nodes()) {
            if (n.personId == QStringLiteral("P1") || n.personId == QStringLiteral("P2"))
                QVERIFY(n.degree >= 1);
        }
    }

    void testAnalyseRequiredBeforeLeads()
    {
        CoOffendingAnalyser ca;
        ca.buildGraph({
            pir(QStringLiteral("A"), QStringLiteral("I")),
            pir(QStringLiteral("B"), QStringLiteral("I")),
        });
        QVERIFY(ca.findLeads(QStringLiteral("I"), 5).isEmpty());
    }
};

QTEST_GUILESS_MAIN(CoOffendingDeep8Test)
#include "test_cooffending_deep8.moc"
