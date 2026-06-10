// AppConfig.h — Runtime configuration and API key storage
// Serialised to/from QSettings (INI file in AppData).

#pragma once
#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

struct AppConfig {
    // API keys
    QString openWeatherKey;          // optional — falls back to Open-Meteo (free)
    QString socrataDomain;           // e.g. "data.cityofchicago.org"
    QString socrataToken;            // optional — increases rate limit (was socrataAppToken)

    // Default geographic focus area
    double defaultLat       = 51.5074;   // London
    double defaultLon       = -0.1278;
    double defaultRadius    = 5.0;       // km (was defaultRadiusKm)

    // Pipeline parameters
    int hawkesHistoryDays   = 365;
    int seriesMinEvents     = 3;
    double seriesEpsKm      = 0.3;       // ~300 m spatial epsilon
    double seriesEpsDays    = 14.0;
    double qualityThreshold = 0.3;       // below this → quarantine
    bool autoRefreshEnabled = false;
    int refreshIntervalSeconds = 3600;   // was autoRefreshMinutes (in seconds now)

    // Risk alert thresholds (RiskForecaster)
    double alertElevated   = 0.30;   // weekly risk ≥ this → Elevated
    double alertHigh       = 0.50;   // weekly risk ≥ this → High
    double alertCritical   = 0.75;   // weekly risk ≥ this → Critical

    // Forecast horizon
    int forecastHorizonDays = 7;

    // GP Regression hyperparameters
    double gpSigma2      = 1.0;    // signal variance
    double gpLengthscale = 0.5;    // length-scale (degrees)
    double gpNoiseSigma2 = 0.1;    // observation noise variance

    // Ensemble weights
    double ensemblePoissonWeight = 0.5;
    double ensembleHawkesWeight  = 0.5;

    // Database
    QString databasePath;               // resolved on first load

    // UI preferences
    QString theme            = "dark";  // "dark" or "light"
    double  mapZoomLevel     = 14.0;    // initial map zoom level (1–20)
    QString exportDirectory  = "";      // default export path
    int     maxLeadCount     = 50;      // max leads shown in the leads panel

    // ── Load from default QSettings location ──────────────────────────────────
    static AppConfig load() {
        QSettings settings("SENTINEL", "Sentinel");
        return loadFrom(settings);
    }

    // ── Load from an explicit INI file path (useful for tests) ─────────────────
    static AppConfig loadFrom(const QString& iniPath) {
        QSettings settings(iniPath, QSettings::IniFormat);
        return loadFrom(settings);
    }

    // ── Save to default QSettings location ────────────────────────────────────
    void save() const {
        QSettings settings("SENTINEL", "Sentinel");
        saveTo(settings);
    }

    // ── Save to an explicit INI file path (useful for tests) ──────────────────
    void saveTo(const QString& iniPath) const {
        QSettings settings(iniPath, QSettings::IniFormat);
        saveTo(settings);
    }

private:
    static AppConfig loadFrom(QSettings& settings) {
        AppConfig c;
        c.openWeatherKey         = settings.value("api/openweather", "").toString();
        c.socrataDomain          = settings.value("api/socrata_domain", "data.cityofchicago.org").toString();
        c.socrataToken           = settings.value("api/socrata_token", "").toString();
        c.defaultLat             = settings.value("map/default_lat", 51.5074).toDouble();
        c.defaultLon             = settings.value("map/default_lon", -0.1278).toDouble();
        c.defaultRadius          = settings.value("map/radius_km", 5.0).toDouble();
        c.hawkesHistoryDays      = settings.value("model/hawkes_history_days", 365).toInt();
        c.seriesMinEvents        = settings.value("model/series_min_events", 3).toInt();
        c.seriesEpsKm            = settings.value("model/series_eps_km", 0.3).toDouble();
        c.seriesEpsDays          = settings.value("model/series_eps_days", 14.0).toDouble();
        c.qualityThreshold       = settings.value("model/quality_threshold", 0.3).toDouble();
        // Clamp alert thresholds to [0, 1]
        c.alertElevated  = std::clamp(settings.value("alert/elevated",  0.30).toDouble(), 0.0, 1.0);
        c.alertHigh      = std::clamp(settings.value("alert/high",      0.50).toDouble(), 0.0, 1.0);
        c.alertCritical  = std::clamp(settings.value("alert/critical",  0.75).toDouble(), 0.0, 1.0);
        c.forecastHorizonDays    = settings.value("model/forecast_horizon", 7).toInt();
        c.gpSigma2               = settings.value("gp/sigma2",       1.0).toDouble();
        c.gpLengthscale          = settings.value("gp/lengthscale",  0.5).toDouble();
        c.gpNoiseSigma2          = settings.value("gp/noise_sigma2", 0.1).toDouble();
        c.ensemblePoissonWeight  = settings.value("ensemble/poisson_weight", 0.5).toDouble();
        c.ensembleHawkesWeight   = settings.value("ensemble/hawkes_weight",  0.5).toDouble();
        c.autoRefreshEnabled     = settings.value("refresh/enabled", false).toBool();
        c.refreshIntervalSeconds = settings.value("refresh/interval_sec", 3600).toInt();
        c.databasePath           = settings.value("db/path",
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + "/sentinel.db").toString();
        // UI preferences
        c.theme           = settings.value("ui/theme",           "dark").toString();
        c.mapZoomLevel    = std::clamp(settings.value("ui/map_zoom_level", 14.0).toDouble(), 1.0, 20.0);
        c.exportDirectory = settings.value("ui/export_directory", "").toString();
        c.maxLeadCount    = std::clamp(settings.value("ui/max_lead_count", 50).toInt(), 1, 10000);
        return c;
    }

    void saveTo(QSettings& settings) const {
        settings.setValue("api/openweather",        openWeatherKey);
        settings.setValue("api/socrata_domain",     socrataDomain);
        settings.setValue("api/socrata_token",      socrataToken);
        settings.setValue("map/default_lat",        defaultLat);
        settings.setValue("map/default_lon",        defaultLon);
        settings.setValue("map/radius_km",          defaultRadius);
        settings.setValue("model/hawkes_history_days", hawkesHistoryDays);
        settings.setValue("model/series_min_events",   seriesMinEvents);
        settings.setValue("model/series_eps_km",       seriesEpsKm);
        settings.setValue("model/series_eps_days",     seriesEpsDays);
        settings.setValue("model/quality_threshold",   qualityThreshold);
        settings.setValue("alert/elevated",          alertElevated);
        settings.setValue("alert/high",              alertHigh);
        settings.setValue("alert/critical",          alertCritical);
        settings.setValue("model/forecast_horizon",  forecastHorizonDays);
        settings.setValue("gp/sigma2",               gpSigma2);
        settings.setValue("gp/lengthscale",          gpLengthscale);
        settings.setValue("gp/noise_sigma2",         gpNoiseSigma2);
        settings.setValue("ensemble/poisson_weight", ensemblePoissonWeight);
        settings.setValue("ensemble/hawkes_weight",  ensembleHawkesWeight);
        settings.setValue("refresh/enabled",         autoRefreshEnabled);
        settings.setValue("refresh/interval_sec",    refreshIntervalSeconds);
        settings.setValue("db/path",                 databasePath);
        // UI preferences
        settings.setValue("ui/theme",            theme);
        settings.setValue("ui/map_zoom_level",   mapZoomLevel);
        settings.setValue("ui/export_directory", exportDirectory);
        settings.setValue("ui/max_lead_count",   maxLeadCount);
        settings.sync();
    }
};
