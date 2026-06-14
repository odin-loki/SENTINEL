// test_data_exporter_deep8.cpp — Deep audit iteration 30: DataExporter
// leads JSON fields, events CSV, saveJson/saveText roundtrip, forecasts JSON.
#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QFile>
#include "core/DataExporter.h"
#include "core/CrimeEvent.h"
#include "models/RiskForecaster.h"

class DataExporterDeep8Test : public QObject
{
    Q_OBJECT

    static InvestigativeLead sampleLead()
    {
        InvestigativeLead l;
        l.rank       = 1;
        l.category   = QStringLiteral("series_linkage");
        l.headline   = QStringLiteral("Series match");
        l.detail     = QStringLiteral("Linked to S-8");
        l.confidence = 0.82;
        return l;
    }

private slots:

    void testLeadsToJsonContainsConfidence()
    {
        const QJsonArray arr = DataExporter::leadsToJson({ sampleLead() });
        QCOMPARE(arr.size(), 1);
        const QJsonObject obj = arr.first().toObject();
        QVERIFY(obj.contains(QStringLiteral("confidence")));
        QVERIFY(obj[QStringLiteral("confidence")].toDouble() > 0.0);
    }

    void testEventsToCsvHasHeaderAndId()
    {
        CrimeEvent ev;
        ev.eventId   = QStringLiteral("EXP-8");
        ev.crimeType = QStringLiteral("theft");

        const QString csv = DataExporter::eventsToCsv({ ev });
        QVERIFY(csv.contains(QStringLiteral("event_id")));
        QVERIFY(csv.contains(QStringLiteral("EXP-8")));
    }

    void testSaveTextRoundtrip()
    {
        QTemporaryFile f;
        QVERIFY(f.open());
        const QString path = f.fileName();
        f.close();

        const QString payload = QStringLiteral("sentinel deep8 export");
        QVERIFY(DataExporter::saveText(payload, path));

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly | QIODevice::Text));
        QCOMPARE(QString::fromUtf8(in.readAll()), payload);
    }

    void testSaveJsonArrayRoundtrip()
    {
        QTemporaryFile f;
        QVERIFY(f.open());
        const QString path = f.fileName();
        f.close();

        QJsonArray arr;
        arr.append(QJsonObject{
            { QStringLiteral("key"), QStringLiteral("value") }
        });
        QVERIFY(DataExporter::saveJson(arr, path));

        QFile in(path);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const auto parsed = QJsonDocument::fromJson(in.readAll()).array();
        QCOMPARE(parsed.size(), 1);
    }

    void testForecastsToJsonZoneField()
    {
        ZoneForecast zf;
        zf.zoneId = QStringLiteral("ZONE-8");
        ForecastDay day;
        day.date = QDate(2024, 9, 1);
        day.riskScore = 0.55;
        zf.days.append(day);

        const QJsonArray arr = DataExporter::forecastsToJson({ zf });
        QVERIFY(!arr.isEmpty());
        QVERIFY(arr.first().toObject().contains(QStringLiteral("zoneId")));
    }
};

QTEST_GUILESS_MAIN(DataExporterDeep8Test)
#include "test_data_exporter_deep8.moc"
