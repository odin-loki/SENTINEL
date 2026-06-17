// test_case_workspace.cpp — Phase 2 headless CaseWorkspaceWidget test
#include <QTest>
#include <QTimeZone>
#include <QApplication>
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QJsonObject>
#include <QFile>
#include <QTemporaryFile>
#include <memory>
#include "ui/CaseWorkspaceWidget.h"
#include "ui/GeoProfileHeatmapWidget.h"
#include "inference/GeographicProfiler.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestCaseWorkspace : public QObject
{
    Q_OBJECT

    static std::shared_ptr<Database> openDb()
    {
        auto db = std::make_shared<Database>([] {
            AppConfig cfg;
            cfg.databasePath = QStringLiteral(":memory:");
            return cfg;
        }());
        db->open();
        return db;
    }

    static CrimeEvent makeEvent(const QString& id, const QString& type,
                                double lat = 51.5, double lon = -0.12)
    {
        CrimeEvent ev;
        ev.eventId      = id;
        ev.id           = id;
        ev.crimeType    = type;
        ev.suburb       = QStringLiteral("Testville");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = QDateTime(QDate(2024, 6, 10), QTime(14, 0), QTimeZone::utc());
        ev.timestamp    = ev.occurredAt.value();
        ev.qualityScore = 0.85;
        ev.source       = QStringLiteral("case_workspace_test");
        ev.lat          = lat;
        ev.lon          = lon;
        ev.latitude     = lat;
        ev.longitude    = lon;
        return ev;
    }

private slots:
    void testCaseIdFilterExists()
    {
        auto db = openDb();
        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
    }

    void testFilterByCaseIdShowsMatchingEvents()
    {
        auto db = openDb();
        CrimeEvent ev1 = makeEvent(QStringLiteral("case_ABC-001"), QStringLiteral("burglary"));
        CrimeEvent ev2 = makeEvent(QStringLiteral("case_ABC-002"), QStringLiteral("theft"));
        CrimeEvent ev3 = makeEvent(QStringLiteral("other-999"), QStringLiteral("assault"));
        QVERIFY(db->insertEvent(ev1));
        QVERIFY(db->insertEvent(ev2));
        QVERIFY(db->insertEvent(ev3));

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_ABC"));
        widget.refresh();
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>(QStringLiteral("caseEventsTable"));
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 2);
    }

    void testSeriesCountLabelUpdates()
    {
        auto db = openDb();
        for (int i = 0; i < 4; ++i) {
            CrimeEvent ev = makeEvent(
                QStringLiteral("case_SER-%1").arg(i),
                QStringLiteral("burglary"),
                51.50 + i * 0.00005,
                -0.12 + i * 0.00005);
            ev.occurredAt = QDateTime(QDate(2024, 6, 10), QTime(12, i * 10),
                                      QTimeZone::utc());
            ev.timestamp = ev.occurredAt.value();
            ev.narrative = QStringLiteral("forced entry rear window");
            QVERIFY(db->insertEvent(ev));
        }

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_SER"));
        widget.refresh();
        QApplication::processEvents();

        auto* seriesLbl = widget.findChild<QLabel*>(QStringLiteral("seriesCountLabel"));
        QVERIFY(seriesLbl != nullptr);
        QVERIFY(seriesLbl->text().contains(QStringLiteral("Series count:")));
        QVERIFY2(widget.lastSeriesCount() >= 1,
                 qPrintable(QStringLiteral("expected at least 1 series, got %1")
                                .arg(widget.lastSeriesCount())));
    }

    void testGeoProfilePlaceholderPresent()
    {
        auto db = openDb();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(db->insertEvent(makeEvent(
                QStringLiteral("case_GEO-%1").arg(i), QStringLiteral("burglary"))));
        }

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_GEO"));
        widget.refresh();
        QApplication::processEvents();

        auto* geoLbl = widget.findChild<QLabel*>(QStringLiteral("geoProfilePlaceholder"));
        QVERIFY(geoLbl != nullptr);
        QVERIFY(geoLbl->text().contains(QStringLiteral("Peak anchor")));
        QVERIFY(geoLbl->text().contains(QStringLiteral("search area")));
    }

    void testLeadHistoryTablePresent()
    {
        auto db = openDb();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(db->insertEvent(makeEvent(
                QStringLiteral("case_LDS-%1").arg(i), QStringLiteral("burglary"))));
        }

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_LDS"));
        widget.refresh();
        QApplication::processEvents();

        auto* leadsTable = widget.findChild<QTableWidget*>(QStringLiteral("caseLeadsTable"));
        QVERIFY(leadsTable != nullptr);
    }

    void testExportCaseReportMarkdown()
    {
        auto db = openDb();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(db->insertEvent(makeEvent(
                QStringLiteral("case_EXP-%1").arg(i), QStringLiteral("burglary"))));
        }

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_EXP"));
        widget.refresh();
        QApplication::processEvents();

        auto* exportBtn = widget.findChild<QPushButton*>(QStringLiteral("caseExportBtn"));
        QVERIFY(exportBtn != nullptr);

        QTemporaryFile tmp;
        tmp.setAutoRemove(true);
        QVERIFY(tmp.open());
        const QString path = tmp.fileName() + QStringLiteral(".md");
        QVERIFY(widget.exportCaseReport(path));

        QFile out(path);
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(out.readAll());
        out.close();
        out.remove();

        QVERIFY(content.contains(QStringLiteral("SENTINEL Investigative Leads Report")));
        QVERIFY(content.contains(QStringLiteral("case_EXP")));
        {
            const QString provHeader = QStringLiteral("Case Events (Provenance)");
            const QString jsonHeader = QStringLiteral("Event Provenance (JSON)");
            QVERIFY(content.contains(provHeader));
            QVERIFY(content.contains(jsonHeader));
        }
    }

    void testFilterButtonTriggersRefresh()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("case_BTN-1"), QStringLiteral("robbery"))));

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        auto* btn = widget.findChild<QPushButton*>(QStringLiteral("caseFilterBtn"));
        QVERIFY(filter != nullptr);
        QVERIFY(btn != nullptr);

        filter->setText(QStringLiteral("case_BTN"));
        QTest::mouseClick(btn, Qt::LeftButton);
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>(QStringLiteral("caseEventsTable"));
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 1);
    }

    void testGeoProfileHeatmapRenders()
    {
        auto db = openDb();
        for (int i = 0; i < 4; ++i) {
            QVERIFY(db->insertEvent(makeEvent(
                QStringLiteral("case_MAP-%1").arg(i),
                QStringLiteral("burglary"),
                51.50 + i * 0.001,
                -0.12 + i * 0.001)));
        }

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_MAP"));
        widget.refresh();
        widget.show();
        QApplication::processEvents();

        auto* heatmap = widget.findChild<GeoProfileHeatmapWidget*>(
            QStringLiteral("geoProfileHeatmap"));
        QVERIFY(heatmap != nullptr);
        QVERIFY2(heatmap->hasProfile(),
                 "expected CGT profile on heatmap after refresh with geo events");

        QVector<QPair<double, double>> sites;
        sites.append(qMakePair(51.50, -0.12));
        sites.append(qMakePair(51.51, -0.11));
        sites.append(qMakePair(51.52, -0.10));
        const GeographicProfile profile = GeographicProfiler().profile(sites);

        GeoProfileHeatmapWidget standalone;
        standalone.setProfile(profile);
        standalone.setCrimeLocations(sites);
        standalone.resize(320, 180);
        QApplication::processEvents();
        QVERIFY(standalone.hasProfile());
        QVERIFY(standalone.minimumHeight() >= 180);
    }

    void testEventCountLabelUpdates()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("case_CNT-1"), QStringLiteral("theft"))));
        QVERIFY(db->insertEvent(makeEvent(QStringLiteral("case_CNT-2"), QStringLiteral("theft"))));

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(QStringLiteral("case_CNT"));
        widget.refresh();
        QApplication::processEvents();

        auto* countLbl = widget.findChild<QLabel*>(QStringLiteral("caseEventCountLabel"));
        QVERIFY(countLbl != nullptr);
        QCOMPARE(countLbl->text(), QStringLiteral("2 events"));
    }

    void testSeriesMergeReducesCount()
    {
        auto db = openDb();
        const QString prefix = QStringLiteral("case_MRG");

        auto insertCluster = [&](const QString& clusterTag, double baseLat, int dayOffset) {
            for (int i = 0; i < 3; ++i) {
                CrimeEvent ev = makeEvent(
                    QStringLiteral("%1-%2-%3").arg(prefix, clusterTag).arg(i),
                    QStringLiteral("burglary"),
                    baseLat + i * 0.00005,
                    -0.12 + i * 0.00005);
                ev.occurredAt = QDateTime(QDate(2024, 6, 10 + dayOffset), QTime(10 + i, 0),
                                          QTimeZone::utc());
                ev.timestamp = ev.occurredAt.value();
                ev.narrative = QStringLiteral("forced entry rear window");
                QVERIFY(db->insertEvent(ev));
            }
        };

        insertCluster(QStringLiteral("A"), 51.50, 0);
        insertCluster(QStringLiteral("B"), 51.60, 4);

        CaseWorkspaceWidget widget(db);
        auto* filter = widget.findChild<QLineEdit*>(QStringLiteral("caseIdFilter"));
        QVERIFY(filter != nullptr);
        filter->setText(prefix);
        widget.refresh();
        QApplication::processEvents();

        auto* table = widget.findChild<QTableWidget*>(QStringLiteral("seriesOverrideTable"));
        auto* mergeBtn = widget.findChild<QPushButton*>(QStringLiteral("seriesMergeBtn"));
        QVERIFY(table != nullptr);
        QVERIFY(mergeBtn != nullptr);

        const int initialCount = widget.lastSeriesCount();
        QVERIFY2(initialCount >= 2,
                 qPrintable(QStringLiteral("expected at least 2 series, got %1")
                                .arg(initialCount)));
        QCOMPARE(table->rowCount(), initialCount);

        for (int row = 0; row < 2; ++row) {
            auto* cb = qobject_cast<QCheckBox*>(table->cellWidget(row, 0));
            QVERIFY2(cb != nullptr, qPrintable(QStringLiteral("missing checkbox row %1").arg(row)));
            cb->setChecked(true);
        }

        QTest::mouseClick(mergeBtn, Qt::LeftButton);
        QApplication::processEvents();

        QCOMPARE(widget.lastSeriesCount(), initialCount - 1);
        QCOMPARE(table->rowCount(), initialCount - 1);

        auto* seriesLbl = widget.findChild<QLabel*>(QStringLiteral("seriesCountLabel"));
        QVERIFY(seriesLbl != nullptr);
        QVERIFY(seriesLbl->text().contains(QStringLiteral("analyst override")));
    }
};

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("SENTINEL_HEADLESS_TEST", "1");
    QApplication app(argc, argv);
    TestCaseWorkspace tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_case_workspace.moc"
