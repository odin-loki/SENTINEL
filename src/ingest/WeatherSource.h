#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QVariantMap>
#include <QMap>
#include <optional>

struct WeatherData {
    double temperatureC    = 0.0;
    double precipitationMm = 0.0;
    double windspeedKmh    = 0.0;
    double visibilityM     = 10000.0;
    bool   isDay           = true;
    int    weatherCode     = 0;
    double tempDiscomfort  = 0.0;   // computed nonlinear discomfort index
    bool   isRaining       = false;
    bool   isLowVisibility = false;
    bool   isExtremeWind   = false;
};

class WeatherSource : public QObject {
    Q_OBJECT
public:
    explicit WeatherSource(QObject* parent = nullptr);

    void fetchHistorical(double lat, double lon, const QDate& start, const QDate& end);
    std::optional<WeatherData> dataAt(const QDateTime& dt) const;

    // Expose for testing
    static double discomfortIndex(double tempC) { return computeDiscomfort(tempC); }
    int cachedHourCount() const { return m_cache.size(); }

signals:
    void fetchComplete(int hours);
    void fetchError(const QString& msg);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_nam;
    QMap<QDateTime, WeatherData> m_cache;   // key: truncated to hour

    static double computeDiscomfort(double tempC);
};
