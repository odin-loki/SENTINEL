#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// DataExporter — serialise SENTINEL outputs to JSON/CSV/Markdown
//
// Supports export of:
//   • InvestigativeLead  → JSON array, CSV, Markdown table, HTML table
//   • ZoneForecast       → JSON array, CSV
//   • BenchmarkReport    → JSON object, Markdown table
//   • ProvenanceEntry    → JSON array
//   • CrimeEvent         → JSON array, CSV, HTML table
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "core/CrimeEvent.h"
#include "benchmark/BenchmarkMetrics.h"
#include "models/RiskForecaster.h"
#include "audit/ProvenanceLog.h"

class DataExporter {
public:
    // ── Investigative leads ─────────────────────────────────────────────────
    static QJsonArray leadsToJson(const QVector<InvestigativeLead>& leads);
    static QString    leadsToCsv(const QVector<InvestigativeLead>& leads);
    static QString    leadsToMarkdown(const QVector<InvestigativeLead>& leads,
                                       const QString& title = "Investigative Leads");
    static QString    leadsToHtml(const QVector<InvestigativeLead>& leads,
                                   const QString& title = "Investigative Leads");

    // ── Risk forecasts ──────────────────────────────────────────────────────
    static QJsonArray forecastsToJson(const QVector<ZoneForecast>& forecasts);
    static QString    forecastsToCsv(const QVector<ZoneForecast>& forecasts);

    // ── Benchmark report ────────────────────────────────────────────────────
    static QJsonObject benchmarkToJson(const BenchmarkReport& report);
    static QString     benchmarkToMarkdown(const BenchmarkReport& report);

    // ── Provenance chain ────────────────────────────────────────────────────
    static QJsonArray provenanceToJson(const QVector<ProvenanceEntry>& chain);

    // ── Crime events ────────────────────────────────────────────────────────
    static QJsonArray eventsToJson(const QVector<CrimeEvent>& events);
    static QString    eventsToCsv(const QVector<CrimeEvent>& events);
    static QString    eventsToHtml(const QVector<CrimeEvent>& events,
                                    const QString& title = "Crime Events");

    // ── File helpers ────────────────────────────────────────────────────────
    static bool saveJson(const QJsonObject& obj, const QString& filePath);
    static bool saveJson(const QJsonArray& arr,  const QString& filePath);
    static bool saveText(const QString& text,     const QString& filePath);

    static QString lastError() { return s_lastError; }

private:
    static QString escapeCsv(const QString& field);
    static QString s_lastError;
};
