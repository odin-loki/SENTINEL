// test_hint_engine_deep9.cpp — Deep audit iteration 27: HintEngine
// MO lead cap, dedup headlines, kMaxLeads resize, resolved MO detail.
#include <QTest>
#include "inference/HintEngine.h"
#include "core/CrimeEvent.h"

class TestHintEngineDeep9 : public QObject
{
    Q_OBJECT

    static CrimeEvent makeEvent()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("HE9-1");
        ev.crimeType = QStringLiteral("burglary");
        ev.suburb    = QStringLiteral("Test");
        return ev;
    }

    static MOMatch mo(const QString& id, double score, bool resolved = false)
    {
        MOMatch m;
        m.caseId          = id;
        m.similarityScore = score;
        m.resolved        = resolved;
        m.sharedFeatures  = { QStringLiteral("forced_entry") };
        m.outcome         = resolved ? QStringLiteral("convicted") : QString{};
        return m;
    }

    static NetworkLead net(const QString& person)
    {
        NetworkLead nl;
        nl.personId        = person;
        nl.connectionType  = QStringLiteral("direct_cooffender");
        nl.sharedIncidents = 2;
        nl.centralityScore = 0.7;
        nl.riskScore       = 0.65;
        nl.reasoning       = QStringLiteral("hub");
        return nl;
    }

private slots:

    void testMoLeadsCappedAtThree()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.8;
        for (int i = 0; i < 6; ++i)
            input.moMatches.append(mo(QStringLiteral("MO-%1").arg(i), 0.8));

        const auto leads = HintEngine().generate(input);
        int moCount = 0;
        for (const auto& l : leads) {
            if (l.category == QStringLiteral("mo_similarity"))
                ++moCount;
        }
        QCOMPARE(moCount, 3);
    }

    void testDuplicateHeadlinesDeduped()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.8;

        NetworkLead a = net(QStringLiteral("P-A"));
        NetworkLead b = net(QStringLiteral("P-B"));
        b.personId = QStringLiteral("P-A");
        input.networkLeads = { a, b };

        const auto leads = HintEngine().generate(input);
        QCOMPARE(leads.size(), 1);
    }

    void testMaxLeadsCapAtFifty()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.9;
        for (int i = 0; i < 60; ++i)
            input.networkLeads.append(net(QStringLiteral("P-%1").arg(i)));

        const auto leads = HintEngine().generate(input);
        QCOMPARE(leads.size(), HintEngine::kMaxLeads);
    }

    void testResolvedMoIncludesOutcome()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.85;
        input.moMatches   = { mo(QStringLiteral("RES-1"), 0.75, true) };

        const auto leads = HintEngine().generate(input);
        QVERIFY(!leads.isEmpty());
        QVERIFY(leads.first().detail.contains(QStringLiteral("convicted")));
    }

    void testLowMoSimilarityFiltered()
    {
        HintEngineInput input;
        input.event       = makeEvent();
        input.dataQuality = 0.8;
        input.moMatches   = { mo(QStringLiteral("LOW"), 0.1) };

        const auto leads = HintEngine().generate(input);
        for (const auto& l : leads)
            QVERIFY(l.category != QStringLiteral("mo_similarity"));
    }
};

QTEST_GUILESS_MAIN(TestHintEngineDeep9)
#include "test_hint_engine_deep9.moc"
