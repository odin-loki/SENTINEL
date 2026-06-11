#pragma once
#include "ingest/DataSource.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QQueue>
#include <QMap>
#include <QVector>
#include <QUrl>

class UKPoliceSource : public DataSource {
    Q_OBJECT
public:
    explicit UKPoliceSource(double lat, double lon, double radiusKm = 1.0, QObject* parent = nullptr);
    QString sourceId() const override { return "uk_police_v1"; }
    QString displayName() const override { return "UK Police Open Data"; }
    void fetchSince(const QDateTime& since) override;
    bool healthCheck() override;
    void setLocation(double lat, double lon, double radiusKm);

    // Synchronous fetch — blocks until all pending requests complete.
    // Returns the collected events. Intended for use in background threads.
    QVector<CrimeEvent> fetchSync(const QDateTime& since, int timeoutMs = 30000);

    // Expose JSON parsing for testing without network access
    CrimeEvent parseRecord(const QJsonObject& raw) const { return parseRawEvent(raw); }

    // Known UK Police API crime categories (mapped to internal types)
    QStringList availableCategories() const;

    // Build a crimes-street request URL (exposed for offline URL tests)
    static QUrl buildFetchUrl(double lat, double lon, const QString& yyyyMm,
                              const QString& category = QStringLiteral("all-crime"));

private slots:
    void onReplyFinished(QNetworkReply* reply);
    void processNextRequest();

private:
    QNetworkAccessManager* m_nam;
    QTimer* m_rateLimitTimer;
    double m_lat, m_lon, m_radiusKm;
    QQueue<QUrl> m_pendingUrls;
    int m_fetchCount      = 0;
    int m_totalRequests   = 0;
    int m_inFlightRequests = 0;  // requests sent but not yet replied to

    static const QString BASE_URL;
    static const QMap<QString, QString> CRIME_TYPE_MAP;

    CrimeEvent parseRawEvent(const QJsonObject& raw) const;
};
