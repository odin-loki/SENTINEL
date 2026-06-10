#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QVector>
#include <QMap>
#include <QDate>
#include <QDateTime>
#include <QPair>
#include "core/CrimeEvent.h"
#include "core/AppConfig.h"

class Database {
public:
    explicit Database(const AppConfig& cfg);
    ~Database();

    bool open();
    void close();
    bool isOpen() const;
    QString lastError() const;

    // Event CRUD
    bool insertEvent(const CrimeEvent& ev);
    bool updateEvent(const CrimeEvent& ev);
    QVector<CrimeEvent> queryEvents(const QString& crimeType = {},
                                    const QDateTime& from = {},
                                    const QDateTime& to = {},
                                    double latMin = -90, double latMax = 90,
                                    double lonMin = -180, double lonMax = 180,
                                    int limit = 5000) const;
    // UI-friendly overload: QDate range + keyword search
    QVector<CrimeEvent> queryEvents(const QString& crimeType,
                                    const QDate& from,
                                    const QDate& to,
                                    const QString& search = {},
                                    int limit = 5000) const;
    CrimeEvent eventById(const QString& id) const;
    int eventCount() const;

    // Leads
    bool insertLead(const InvestigativeLead& lead, const QString& eventId);
    QVector<InvestigativeLead> queryLeads(const QString& eventId = {}) const;

    // Audit
    bool insertAuditEntry(const QString& eventId, const QString& action,
                          const QString& detail);
    QVector<std::tuple<QDateTime,QString,QString,QString>> queryAudit(int limit = 200) const;

    // Stats helpers
    QMap<QString,int> crimeTypeCounts() const;
    QVector<std::pair<QDateTime,int>> eventsByHour(const QDateTime& from,
                                                    const QDateTime& to) const;

    // ── UI convenience aliases ─────────────────────────────────────────────
    // These match the method names expected by the UI layer.
    int getTotalEventCount() const { return eventCount(); }

    QVector<CrimeEvent> getRecentEvents(int n) const {
        return queryEvents(QString{}, QDateTime{}, QDateTime{}, -90, 90, -180, 180, n);
    }

    QVector<CrimeEvent> getEventsSince(const QDateTime& since) const {
        return queryEvents(QString{}, since, QDateTime{});
    }

    QVector<CrimeEvent> getAllEvents() const {
        return queryEvents(QString{}, QDateTime{}, QDateTime{});
    }

    QMap<QString,int> getCrimeTypeCounts() const { return crimeTypeCounts(); }

    // Returns vector[0..23] of crime counts per hour for the last `days` days
    QVector<int> getHourlyCounts(int days = 30) const {
        const QDateTime from = QDateTime::currentDateTimeUtc().addDays(-days);
        const auto raw = eventsByHour(from, QDateTime::currentDateTimeUtc());
        QVector<int> result(24, 0);
        for (const auto& [dt, cnt] : raw) {
            int h = dt.toLocalTime().time().hour();
            if (h >= 0 && h < 24) result[h] += cnt;
        }
        return result;
    }

    // Returns (date, count) pairs sorted ascending for the last `days` days
    QVector<QPair<QDate,int>> getDailyTrend(int days = 30) const;

    double getAverageQualityScore() const;

    QVector<InvestigativeLead> getLeads(const QString& eventId = {}) const {
        return queryLeads(eventId);
    }

    // ── Schema versioning ─────────────────────────────────────────────────────
    static constexpr int SCHEMA_VERSION = 3;
    int currentSchemaVersion() const;
    bool migrateSchema(int fromVersion);

private:
    void createSchema();
    QSqlDatabase m_db;
    QString m_connName;   // unique per-instance to avoid Qt SQL pool collisions
    QString m_path;
    QString m_lastError;

    static CrimeEvent rowToEvent(const QSqlQuery& q);
    static InvestigativeLead rowToLead(const QSqlQuery& q);
};
