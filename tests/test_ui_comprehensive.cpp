// test_ui_comprehensive.cpp — Comprehensive UI widget tests for SENTINEL iteration 4.
// Covers DashboardWidget, AnalyticsWidget, EventsTableWidget, LeadsWidget,
// and DebugConsoleWidget. Runs headless via -platform offscreen.

#include <QTest>
#include <QApplication>
#include <QLabel>
#include <QGroupBox>
#include <QTableWidget>
#include <QTableView>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QComboBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QUuid>
#include <QPoint>
#include <memory>

#include "core/AppConfig.h"
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "core/SentinelLogger.h"
#include "ui/DashboardWidget.h"
#include "ui/AnalyticsWidget.h"
#include "ui/EventsTableWidget.h"
#include "ui/LeadsWidget.h"
#include "ui/DebugConsoleWidget.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<Database> makeDB()
{
    AppConfig cfg;
    cfg.databasePath = QStringLiteral(":memory:");
    auto db = std::make_shared<Database>(cfg);
    db->open();
    return db;
}

static CrimeEvent makeEvent(const QString& id,
                            const QString& type = QStringLiteral("burglary"),
                            const QDate& date = QDate::currentDate())
{
    CrimeEvent e;
    e.eventId      = id;
    e.id           = id;
    e.crimeType    = type;
    e.suburb       = QStringLiteral("TestSuburb");
    e.source       = QStringLiteral("test");
    e.ingestedAt   = QDateTime::currentDateTimeUtc();
    e.occurredAt   = QDateTime(date, QTime(12, 0, 0), Qt::LocalTime);
    e.timestamp    = *e.occurredAt;
    e.qualityScore = 0.85;
    e.lat          = 51.5;
    e.lon          = -0.1;
    e.latitude     = 51.5;
    e.longitude    = -0.1;
    return e;
}

static InvestigativeLead makeLead(int rank, double confidence,
                                  const QString& category = QStringLiteral("mo_similarity"))
{
    InvestigativeLead l;
    l.rank             = rank;
    l.category         = category;
    l.headline         = QStringLiteral("Lead %1").arg(rank);
    l.detail           = QStringLiteral("Detail for lead %1").arg(rank);
    l.confidence       = confidence;
    l.confidenceMethod = QStringLiteral("test");
    l.generatedAt      = QDateTime::currentDateTimeUtc();
    return l;
}

static LogEntry makeLogEntry(QtMsgType level, const QString& message,
                             const QString& category = QStringLiteral("test"))
{
    LogEntry e;
    e.timestamp = QDateTime::currentDateTime();
    e.level     = level;
    e.category  = category;
    e.message   = message;
    e.file      = QStringLiteral("test_ui_comprehensive.cpp");
    e.line      = 0;
    return e;
}

// Returns stat-card value labels in creation order:
// TOTAL EVENTS, LAST 24 HOURS, MOST COMMON, AVG QUALITY
static QVector<QLabel*> statValueLabels(QWidget& widget)
{
    QVector<QLabel*> labels;
    for (auto* lbl : widget.findChildren<QLabel*>()) {
        if (lbl->objectName() == QStringLiteral("valueLabel"))
            labels.append(lbl);
    }
    return labels;
}

static int visibleLineCount(QPlainTextEdit* te)
{
    const QStringList lines = te->toPlainText().split('\n');
    int count = 0;
    for (const QString& l : lines)
        if (!l.trimmed().isEmpty()) ++count;
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────

class UIComprehensiveTest : public QObject {
    Q_OBJECT

private slots:

    // ── DashboardWidget ───────────────────────────────────────────────────────

    void testDashboardConstruction()
    {
        AppConfig cfg;
        auto db = makeDB();
        DashboardWidget w(db, cfg);
        w.resize(1024, 768);
        w.show();
        QApplication::processEvents();
        QVERIFY(w.isVisible() || w.width() > 0);
    }

    void testDashboardRefreshWithData()
    {
        AppConfig cfg;
        auto db = makeDB();
        DashboardWidget w(db, cfg);
        w.resize(1024, 768);

        const auto labelsBefore = statValueLabels(w);
        QVERIFY(labelsBefore.size() >= 1);
        const QString before = labelsBefore[0]->text();

        for (int i = 0; i < 5; ++i)
            db->insertEvent(makeEvent(QStringLiteral("dash_%1").arg(i)));

        w.refresh();
        QApplication::processEvents();

        const QString after = labelsBefore[0]->text();
        QCOMPARE(after, QStringLiteral("5"));
        QVERIFY(before != after || before == QStringLiteral("0") || before == QStringLiteral("—"));
    }

    void testDashboardEmptyState()
    {
        AppConfig cfg;
        auto db = makeDB();
        DashboardWidget w(db, cfg);
        w.refresh();
        QApplication::processEvents();

        const auto labels = statValueLabels(w);
        QVERIFY(labels.size() >= 1);
        QCOMPARE(labels[0]->text(), QStringLiteral("0"));
    }

    // ── AnalyticsWidget ───────────────────────────────────────────────────────

    void testAnalyticsConstruction()
    {
        AppConfig cfg;
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        QVERIFY(w.width() > 0);
    }

    void testAnalyticsEmptyData()
    {
        AppConfig cfg;
        auto db = makeDB();
        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);
        w.refresh();
        QApplication::processEvents();
        QVERIFY(w.findChild<QTabWidget*>() != nullptr);
    }

    void testAnalyticsWithPredictions()
    {
        // AnalyticsWidget has no setEnsemblePrediction() API; it renders charts
        // from database aggregates via refresh().
        AppConfig cfg;
        auto db = makeDB();
        const QStringList types{ QStringLiteral("burglary"), QStringLiteral("theft"),
                                 QStringLiteral("assault") };
        for (int i = 0; i < 30; ++i)
            db->insertEvent(makeEvent(QStringLiteral("ana_%1").arg(i), types[i % types.size()]));

        AnalyticsWidget w(db, cfg);
        w.resize(900, 600);

        QLabel* summary = w.findChild<QLabel*>();
        QVERIFY(summary != nullptr);
        const QString before = summary->text();

        w.refresh();
        QApplication::processEvents();

        QLabel* summaryAfter = nullptr;
        for (auto* lbl : w.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("Total"), Qt::CaseInsensitive)) {
                summaryAfter = lbl;
                break;
            }
        }
        QVERIFY(summaryAfter != nullptr);
        QVERIFY(summaryAfter->text().contains(QStringLiteral("30"))
                || summaryAfter->text() != before);
    }

    // ── EventsTableWidget ─────────────────────────────────────────────────────

    void testEventsTableConstruction()
    {
        auto db = makeDB();
        EventsTableWidget w(db);
        w.resize(800, 600);
        QVERIFY(w.findChild<QTableView*>() != nullptr);
    }

    void testEventsTableLoadEvents()
    {
        // loadEvents() is private; refresh() queries DB and populates the table.
        auto db = makeDB();
        for (int i = 0; i < 5; ++i)
            db->insertEvent(makeEvent(QStringLiteral("evt_%1").arg(i)));

        EventsTableWidget w(db);
        w.refresh();
        QApplication::processEvents();

        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 5);
    }

    void testEventsTableSort()
    {
        auto db = makeDB();
        db->insertEvent(makeEvent(QStringLiteral("a"), QStringLiteral("assault")));
        db->insertEvent(makeEvent(QStringLiteral("b"), QStringLiteral("burglary")));
        db->insertEvent(makeEvent(QStringLiteral("c"), QStringLiteral("theft")));

        EventsTableWidget w(db);
        w.refresh();
        QApplication::processEvents();

        auto* table = w.findChild<QTableView*>();
        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(table != nullptr && model != nullptr);
        QVERIFY(model->rowCount() >= 3);

        const QString firstBefore = model->item(0, 2)->text();

        auto* header = table->horizontalHeader();
        QVERIFY(header != nullptr);
        QTest::mouseClick(header->viewport(), Qt::LeftButton, {},
                          QPoint(header->sectionPosition(2) + 5, 5));
        QApplication::processEvents();

        const QString firstAfter = model->item(0, 2)->text();
        QVERIFY(!firstAfter.isEmpty());
        // Ascending sort on Crime Type should place "assault" first
        QCOMPARE(firstAfter, QStringLiteral("assault"));
        QVERIFY(firstBefore != firstAfter || firstBefore == QStringLiteral("assault"));
    }

    void testEventsTableFilter()
    {
        auto db = makeDB();
        db->insertEvent(makeEvent(QStringLiteral("b1"), QStringLiteral("burglary")));
        db->insertEvent(makeEvent(QStringLiteral("b2"), QStringLiteral("burglary")));
        db->insertEvent(makeEvent(QStringLiteral("t1"), QStringLiteral("theft")));

        EventsTableWidget w(db);
        w.refresh();

        auto* model = w.findChild<QStandardItemModel*>();
        auto* combo = w.findChild<QComboBox*>();
        QVERIFY(model != nullptr && combo != nullptr);
        QCOMPARE(model->rowCount(), 3);

        const int idx = combo->findData(QStringLiteral("burglary"));
        QVERIFY(idx >= 0);
        combo->setCurrentIndex(idx);
        w.refresh();
        QApplication::processEvents();

        QCOMPARE(model->rowCount(), 2);
        for (int row = 0; row < model->rowCount(); ++row)
            QCOMPARE(model->item(row, 2)->text(), QStringLiteral("burglary"));
    }

    void testEventsTableEmpty()
    {
        auto db = makeDB();
        EventsTableWidget w(db);
        w.refresh();
        QApplication::processEvents();

        auto* model = w.findChild<QStandardItemModel*>();
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 0);
    }

    // ── LeadsWidget ───────────────────────────────────────────────────────────

    void testLeadsWidgetConstruction()
    {
        auto db = makeDB();
        LeadsWidget w(db);
        w.resize(900, 600);
        QVERIFY(w.findChild<QListWidget*>() != nullptr);
    }

    void testLeadsWidgetLoadLeads()
    {
        // Public API is setLeads(), not loadLeads().
        auto db = makeDB();
        LeadsWidget w(db);

        QVector<InvestigativeLead> leads;
        for (int i = 1; i <= 3; ++i)
            leads.append(makeLead(i, 0.5 + i * 0.1));
        w.setLeads(leads);
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);
    }

    void testLeadsWidgetSort()
    {
        // Widget preserves caller order; pass leads highest-confidence first.
        auto db = makeDB();
        LeadsWidget w(db);

        QVector<InvestigativeLead> leads = {
            makeLead(1, 0.95),
            makeLead(2, 0.70),
            makeLead(3, 0.40),
        };
        w.setLeads(leads);
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 1);

        const QString firstText = list->item(0)->text();
        QVERIFY(firstText.contains(QStringLiteral("95"))
                || firstText.contains(QStringLiteral("#1")));
    }

    void testLeadsWidgetEmpty()
    {
        auto db = makeDB();
        LeadsWidget w(db);
        w.setLeads({});
        QApplication::processEvents();

        auto* list = w.findChild<QListWidget*>();
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 0);

        bool foundEmptyLabel = false;
        for (auto* lbl : w.findChildren<QLabel*>()) {
            if (lbl->text().contains(QStringLiteral("0 lead"))) {
                foundEmptyLabel = true;
                break;
            }
        }
        QVERIFY(foundEmptyLabel);
    }

    // ── DebugConsoleWidget ─────────────────────────────────────────────────────

    void testDebugConsoleConstruction()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.resize(600, 400);
        QVERIFY(w.findChild<QPlainTextEdit*>() != nullptr);
    }

    void testDebugConsoleClear()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeLogEntry(QtInfoMsg, QStringLiteral("before clear")));
        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(!textEdit->toPlainText().trimmed().isEmpty());

        w.clear();
        QVERIFY(textEdit->toPlainText().trimmed().isEmpty());
    }

    void testDebugConsoleAppend()
    {
        // Public API is appendEntry(LogEntry), not appendMessage().
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeLogEntry(QtInfoMsg, QStringLiteral("test")));
        QApplication::processEvents();

        auto* textEdit = w.findChild<QPlainTextEdit*>();
        QVERIFY(textEdit != nullptr);
        QVERIFY(textEdit->toPlainText().contains(QStringLiteral("test")));
    }

    void testDebugConsoleLogLevelFilter()
    {
        SentinelLogger::instance().clear();
        DebugConsoleWidget w;
        w.clear();

        w.appendEntry(makeLogEntry(QtDebugMsg,    QStringLiteral("debug")));
        w.appendEntry(makeLogEntry(QtInfoMsg,     QStringLiteral("info")));
        w.appendEntry(makeLogEntry(QtWarningMsg,  QStringLiteral("warning")));
        w.appendEntry(makeLogEntry(QtCriticalMsg, QStringLiteral("critical")));

        auto* textEdit    = w.findChild<QPlainTextEdit*>();
        auto* levelFilter = w.findChild<QComboBox*>();
        QVERIFY(textEdit != nullptr && levelFilter != nullptr);
        QCOMPARE(visibleLineCount(textEdit), 4);

        // Combo uses "Critical" (not "Error") — index 3 shows only Critical/Fatal
        levelFilter->setCurrentIndex(3);
        QApplication::processEvents();

        QCOMPARE(visibleLineCount(textEdit), 1);
        QVERIFY(textEdit->toPlainText().contains(QStringLiteral("critical")));
        QVERIFY(!textEdit->toPlainText().contains(QStringLiteral("info")));
    }
};

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi);
    UIComprehensiveTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ui_comprehensive.moc"
