// test_data_source_interface.cpp
// Tests for DataSource abstract interface via a minimal concrete mock
// implementation. Verifies signals, healthCheck, and async fetch contract.
#include <QTest>
#include <QTimeZone>
#include <QSignalSpy>
#include "ingest/DataSource.h"

class MockDataSource : public DataSource
{
    Q_OBJECT

public:
    explicit MockDataSource(QObject* parent = nullptr)
        : DataSource(parent) {}

    QString sourceId() const override { return QStringLiteral("mock"); }
    QString displayName() const override { return QStringLiteral("Mock Source"); }
    bool healthCheck() override { return m_healthy; }

    void fetchSince(const QDateTime& since) override
    {
        m_lastFetchSince = since;
        for (const auto& ev : m_events) {
            emit eventFetched(ev);
        }
        emit fetchComplete(m_events.size());
    }

    void setHealthy(bool h) { m_healthy = h; }
    void setEvents(const QVector<CrimeEvent>& evs) { m_events = evs; }
    QDateTime lastFetchSince() const { return m_lastFetchSince; }

private:
    bool m_healthy = true;
    QVector<CrimeEvent> m_events;
    QDateTime m_lastFetchSince;
};

class DataSourceInterfaceTest : public QObject
{
    Q_OBJECT

private:
    static CrimeEvent makeEvent(const QString& id)
    {
        CrimeEvent ev;
        ev.id        = id;
        ev.crimeType = QStringLiteral("theft");
        ev.latitude  = 51.5;
        ev.longitude = -0.1;
        ev.timestamp = QDateTime::currentDateTimeUtc();
        return ev;
    }

private slots:

    // 1. sourceId() returns correct id
    void testSourceId()
    {
        MockDataSource src;
        QCOMPARE(src.sourceId(), QStringLiteral("mock"));
    }

    // 2. displayName() is non-empty
    void testDisplayNameNonEmpty()
    {
        MockDataSource src;
        QVERIFY2(!src.displayName().isEmpty(), "displayName must be non-empty");
    }

    // 3. healthCheck() returns true when healthy
    void testHealthCheckTrue()
    {
        MockDataSource src;
        src.setHealthy(true);
        QVERIFY(src.healthCheck());
    }

    // 4. healthCheck() returns false when unhealthy
    void testHealthCheckFalse()
    {
        MockDataSource src;
        src.setHealthy(false);
        QVERIFY(!src.healthCheck());
    }

    // 5. fetchSince() emits fetchComplete
    void testFetchCompleteSignal()
    {
        MockDataSource src;
        src.setEvents({});
        QSignalSpy spy(&src, &DataSource::fetchComplete);
        src.fetchSince(QDateTime::currentDateTimeUtc().addDays(-7));
        QCOMPARE(spy.count(), 1);
    }

    // 6. fetchSince() emits eventFetched for each event
    void testEventFetchedSignal()
    {
        MockDataSource src;
        QVector<CrimeEvent> evs = { makeEvent(QStringLiteral("E1")), makeEvent(QStringLiteral("E2")) };
        src.setEvents(evs);
        QSignalSpy spy(&src, &DataSource::eventFetched);
        src.fetchSince(QDateTime::currentDateTimeUtc().addDays(-7));
        QCOMPARE(spy.count(), 2);
    }

    // 7. fetchComplete count matches number of events
    void testFetchCompleteCount()
    {
        MockDataSource src;
        QVector<CrimeEvent> evs = { makeEvent(QStringLiteral("E3")), makeEvent(QStringLiteral("E4")),
                                     makeEvent(QStringLiteral("E5")) };
        src.setEvents(evs);
        QSignalSpy spy(&src, &DataSource::fetchComplete);
        src.fetchSince(QDateTime::currentDateTimeUtc().addDays(-30));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toInt(), 3);
    }

    // 8. fetchSince() stores the since parameter
    void testFetchSinceStored()
    {
        MockDataSource src;
        const QDateTime since = QDateTime(QDate(2024, 1, 15), QTime(0, 0), QTimeZone::utc());
        src.fetchSince(since);
        QCOMPARE(src.lastFetchSince(), since);
    }

    // 9. Repeated healthCheck calls consistent
    void testHealthCheckConsistent()
    {
        MockDataSource src;
        src.setHealthy(true);
        QVERIFY(src.healthCheck());
        QVERIFY(src.healthCheck());
    }

    // 10. fetchError signal can be emitted
    void testFetchErrorSignalConnectable()
    {
        MockDataSource src;
        QSignalSpy spy(&src, &DataSource::fetchError);
        emit src.fetchError(QStringLiteral("Test error"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.first().first().toString().contains(QStringLiteral("error")));
    }
};

QTEST_MAIN(DataSourceInterfaceTest)
#include "test_data_source_interface.moc"
