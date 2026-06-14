#pragma once
#include <QVector>
#include <QString>
#include "core/CrimeEvent.h"
#include "core/AppConfig.h"
#include "ingest/DataQualityScorer.h"
#include "ingest/WeatherSource.h"

struct ImportSummary {
    int totalParsed      = 0;
    int passingCount     = 0;
    int quarantinedCount = 0;
    double passRate      = 0.0;
    double avgQuality    = 0.0;
    QVector<QualityReport> reports;
};

// Scores quality, optionally enriches weather from bundled cache, applies threshold.
class IngestEnricher {
public:
    explicit IngestEnricher(const AppConfig& cfg);

    ImportSummary prepare(QVector<CrimeEvent>& events);

    static QString bundledDataDir();
    static QString defaultSampleCsv();

private:
    void scoreEvents(QVector<CrimeEvent>& events, ImportSummary& summary);
    void enrichWeather(QVector<CrimeEvent>& events);
    bool loadWeatherCache(WeatherSource& source, const QString& jsonPath);

    AppConfig m_cfg;
    DataQualityScorer m_scorer;
};
