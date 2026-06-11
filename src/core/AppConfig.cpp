#include "core/AppConfig.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

template<typename T>
bool clampValue(T& value, T lo, T hi)
{
    const T clamped = std::clamp(value, lo, hi);
    if (clamped == value)
        return true;
    value = clamped;
    return false;
}

} // namespace

// ── JSON serialisation ────────────────────────────────────────────────────────

QJsonObject AppConfig::toJson() const
{
    QJsonObject obj;
    obj["open_weather_key"]          = openWeatherKey;
    obj["socrata_domain"]            = socrataDomain;
    obj["socrata_token"]             = socrataToken;
    obj["default_lat"]               = defaultLat;
    obj["default_lon"]               = defaultLon;
    obj["default_radius"]            = defaultRadius;
    obj["hawkes_history_days"]       = hawkesHistoryDays;
    obj["series_min_events"]         = seriesMinEvents;
    obj["series_eps_km"]             = seriesEpsKm;
    obj["series_eps_days"]           = seriesEpsDays;
    obj["quality_threshold"]         = qualityThreshold;
    obj["auto_refresh_enabled"]      = autoRefreshEnabled;
    obj["refresh_interval_seconds"]  = refreshIntervalSeconds;
    obj["alert_elevated"]            = alertElevated;
    obj["alert_high"]                = alertHigh;
    obj["alert_critical"]            = alertCritical;
    obj["forecast_horizon_days"]     = forecastHorizonDays;
    obj["gp_sigma2"]                 = gpSigma2;
    obj["gp_lengthscale"]            = gpLengthscale;
    obj["gp_noise_sigma2"]           = gpNoiseSigma2;
    obj["rossmo_f"]                  = rossmoF;
    obj["rossmo_g"]                  = rossmoG;
    obj["ensemble_poisson_weight"]   = ensemblePoissonWeight;
    obj["ensemble_hawkes_weight"]    = ensembleHawkesWeight;
    obj["database_path"]             = databasePath;
    obj["theme"]                     = theme;
    obj["map_zoom_level"]            = mapZoomLevel;
    obj["export_directory"]          = exportDirectory;
    obj["max_lead_count"]            = maxLeadCount;
    obj["poisson_grid_size"]         = poissonGridSize;
    obj["kde_grid_size"]             = kdeGridSize;
    return obj;
}

AppConfig AppConfig::fromJson(const QJsonObject& obj)
{
    AppConfig c;

    if (obj.contains("open_weather_key"))         c.openWeatherKey         = obj["open_weather_key"].toString();
    if (obj.contains("socrata_domain"))           c.socrataDomain          = obj["socrata_domain"].toString();
    if (obj.contains("socrata_token"))            c.socrataToken           = obj["socrata_token"].toString();
    if (obj.contains("default_lat"))                c.defaultLat             = obj["default_lat"].toDouble();
    if (obj.contains("default_lon"))                c.defaultLon             = obj["default_lon"].toDouble();
    if (obj.contains("default_radius"))             c.defaultRadius          = obj["default_radius"].toDouble();
    if (obj.contains("hawkes_history_days"))        c.hawkesHistoryDays      = obj["hawkes_history_days"].toInt();
    if (obj.contains("series_min_events"))          c.seriesMinEvents        = obj["series_min_events"].toInt();
    if (obj.contains("series_eps_km"))              c.seriesEpsKm            = obj["series_eps_km"].toDouble();
    if (obj.contains("series_eps_days"))            c.seriesEpsDays          = obj["series_eps_days"].toDouble();
    if (obj.contains("quality_threshold"))          c.qualityThreshold       = obj["quality_threshold"].toDouble();
    if (obj.contains("auto_refresh_enabled"))       c.autoRefreshEnabled     = obj["auto_refresh_enabled"].toBool();
    if (obj.contains("refresh_interval_seconds"))   c.refreshIntervalSeconds = obj["refresh_interval_seconds"].toInt();
    if (obj.contains("alert_elevated"))             c.alertElevated          = obj["alert_elevated"].toDouble();
    if (obj.contains("alert_high"))                 c.alertHigh              = obj["alert_high"].toDouble();
    if (obj.contains("alert_critical"))             c.alertCritical          = obj["alert_critical"].toDouble();
    if (obj.contains("forecast_horizon_days"))      c.forecastHorizonDays    = obj["forecast_horizon_days"].toInt();
    if (obj.contains("gp_sigma2"))                  c.gpSigma2               = obj["gp_sigma2"].toDouble();
    if (obj.contains("gp_lengthscale"))             c.gpLengthscale          = obj["gp_lengthscale"].toDouble();
    if (obj.contains("gp_noise_sigma2"))            c.gpNoiseSigma2          = obj["gp_noise_sigma2"].toDouble();
    if (obj.contains("rossmo_f"))                   c.rossmoF                = obj["rossmo_f"].toDouble();
    if (obj.contains("rossmo_g"))                   c.rossmoG                = obj["rossmo_g"].toDouble();
    if (obj.contains("ensemble_poisson_weight"))    c.ensemblePoissonWeight  = obj["ensemble_poisson_weight"].toDouble();
    if (obj.contains("ensemble_hawkes_weight"))     c.ensembleHawkesWeight   = obj["ensemble_hawkes_weight"].toDouble();
    if (obj.contains("database_path"))              c.databasePath           = obj["database_path"].toString();
    if (obj.contains("theme"))                      c.theme                  = obj["theme"].toString();
    if (obj.contains("map_zoom_level"))             c.mapZoomLevel           = obj["map_zoom_level"].toDouble();
    if (obj.contains("export_directory"))           c.exportDirectory        = obj["export_directory"].toString();
    if (obj.contains("max_lead_count"))             c.maxLeadCount           = obj["max_lead_count"].toInt();
    if (obj.contains("poisson_grid_size"))          c.poissonGridSize        = obj["poisson_grid_size"].toInt();
    if (obj.contains("kde_grid_size"))              c.kdeGridSize            = obj["kde_grid_size"].toInt();

    return c;
}

bool AppConfig::saveToFile(const QString& path) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool AppConfig::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    *this = fromJson(doc.object());
    return true;
}

// ── Range validation / clamping ───────────────────────────────────────────────

bool AppConfig::validate()
{
    bool ok = true;

    ok &= clampValue(defaultLat, -90.0, 90.0);
    ok &= clampValue(defaultLon, -180.0, 180.0);
    if (defaultRadius <= 0.0) {
        defaultRadius = 5.0;
        ok = false;
    }
    ok &= clampValue(hawkesHistoryDays, 7, 3650);
    ok &= clampValue(seriesMinEvents, 2, 10000);
    if (seriesEpsKm <= 0.0) {
        seriesEpsKm = 0.3;
        ok = false;
    }
    if (seriesEpsDays <= 0.0) {
        seriesEpsDays = 14.0;
        ok = false;
    }
    ok &= clampValue(qualityThreshold, 0.0, 1.0);
    ok &= clampValue(alertElevated, 0.0, 1.0);
    ok &= clampValue(alertHigh, 0.0, 1.0);
    ok &= clampValue(alertCritical, 0.0, 1.0);
    ok &= clampValue(forecastHorizonDays, 1, 30);
    ok &= clampValue(gpSigma2, 0.001, 100.0);
    ok &= clampValue(gpLengthscale, 0.001, 10.0);
    ok &= clampValue(gpNoiseSigma2, 0.001, 10.0);
    ok &= clampValue(rossmoF, 0.001, 3.0);
    ok &= clampValue(rossmoG, 0.001, 3.0);
    ok &= clampValue(ensemblePoissonWeight, 0.0, 1.0);
    ok &= clampValue(ensembleHawkesWeight, 0.0, 1.0);
    ok &= clampValue(refreshIntervalSeconds, 10, 86400);
    ok &= clampValue(mapZoomLevel, 1.0, 20.0);
    ok &= clampValue(maxLeadCount, 1, 10000);
    ok &= clampValue(poissonGridSize, 10, 500);
    ok &= clampValue(kdeGridSize, 10, 500);

    if (databasePath.trimmed().isEmpty()) {
        databasePath = QStringLiteral(":memory:");
        ok = false;
    }

    if (alertElevated >= alertHigh) {
        alertHigh = std::min(1.0, alertElevated + 0.1);
        ok = false;
    }
    if (alertHigh >= alertCritical) {
        alertCritical = std::min(1.0, alertHigh + 0.1);
        ok = false;
    }
    // Chain fixup may push both alertHigh and alertCritical to 1.0 when
    // alertElevated is very high (≥ 0.9). Reset to safe defaults in that case.
    if (alertElevated >= alertHigh || alertHigh >= alertCritical) {
        alertElevated = 0.30;
        alertHigh     = 0.50;
        alertCritical = 0.75;
        ok = false;
    }

    return ok;
}
