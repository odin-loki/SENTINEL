// test_co_offending_db.cpp — CoOffendingGraphWidget loadFromDatabase headless test
#include <QTest>
#include <QApplication>
#include <QJsonObject>
#include <memory>

#include "ui/CoOffendingGraphWidget.h"
#include "core/Database.h"
#include "core/AppConfig.h"
#include "core/CrimeEvent.h"

class TestCoOffendingDb : public QObject
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

    static CrimeEvent makePersonEvent(const QString& eventId, const QString& personId,
                                      const QString& role = QStringLiteral("suspect"))
    {
        CrimeEvent ev;
        ev.eventId      = eventId;
        ev.id           = eventId;
        ev.crimeType    = QStringLiteral("theft");
        ev.suburb       = QStringLiteral("Testville");
        ev.ingestedAt   = QDateTime::currentDateTimeUtc();
        ev.occurredAt   = QDateTime(QDate(2024, 6, 10), QTime(14, 0), QTimeZone::utc());
        ev.timestamp    = ev.occurredAt.value();
        ev.qualityScore = 0.85;
        ev.source       = QStringLiteral("co_offending_db_test");
        ev.meta         = QJsonObject{
            {QStringLiteral("person_id"), personId},
            {QStringLiteral("role"), role},
        };
        return ev;
    }

private slots:
    void testLoadFromDatabaseBuildsGraph()
    {
        auto db = openDb();
        QVERIFY(db->insertEvent(makePersonEvent(QStringLiteral("evt-1"), QStringLiteral("p1"))));
        QVERIFY(db->insertEvent(makePersonEvent(QStringLiteral("evt-2"), QStringLiteral("p2"))));

        CoOffendingGraphWidget widget(db);
        widget.loadFromDatabase();
        QApplication::processEvents();

        QVERIFY2(widget.nodeCount() >= 2,
                 qPrintable(QStringLiteral("expected >= 2 nodes, got %1")
                                .arg(widget.nodeCount())));
    }
};

QTEST_MAIN(TestCoOffendingDb)
#include "test_co_offending_db.moc"
