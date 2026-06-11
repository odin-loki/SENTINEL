#include "core/DataExporter.h"

#include <QJsonDocument>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

QString DataExporter::s_lastError;

// ─── CSV escape ───────────────────────────────────────────────────────────────

QString DataExporter::escapeCsv(const QString& field)
{
    if (field.contains(QLatin1Char(',')) ||
        field.contains(QLatin1Char('"')) ||
        field.contains(QLatin1Char('\n')) ||
        field.contains(QLatin1Char('\r'))) {
        return QLatin1Char('"') +
               QString(field).replace(QLatin1Char('"'), QStringLiteral("\"\"")) +
               QLatin1Char('"');
    }
    return field;
}

// ─── Investigative leads ──────────────────────────────────────────────────────

QJsonArray DataExporter::leadsToJson(const QVector<InvestigativeLead>& leads)
{
    QJsonArray arr;
    for (const auto& l : leads) {
        QJsonObject obj;
        obj[QStringLiteral("rank")]             = l.rank;
        obj[QStringLiteral("category")]         = l.category;
        obj[QStringLiteral("headline")]         = l.headline;
        obj[QStringLiteral("detail")]           = l.detail;
        obj[QStringLiteral("confidence")]       = l.confidence;
        obj[QStringLiteral("confidenceMethod")] = l.confidenceMethod;
        obj[QStringLiteral("generatedAt")]      = l.generatedAt.toString(Qt::ISODate);
        obj[QStringLiteral("supportingData")]   = l.supportingData;

        QJsonArray provArr;
        for (const auto& p : l.provenance) provArr.append(p);
        obj[QStringLiteral("provenance")] = provArr;

        QJsonArray contrArr;
        for (const auto& c : l.contradictions) contrArr.append(c);
        obj[QStringLiteral("contradictions")] = contrArr;

        arr.append(obj);
    }
    return arr;
}

QString DataExporter::leadsToCsv(const QVector<InvestigativeLead>& leads)
{
    QString csv;
    QTextStream out(&csv);
    out << "rank,category,headline,detail,confidence,confidence_method,generated_at\n";
    for (const auto& l : leads) {
        out << l.rank << ','
            << escapeCsv(l.category)         << ','
            << escapeCsv(l.headline)         << ','
            << escapeCsv(l.detail)           << ','
            << QString::number(l.confidence, 'f', 4) << ','
            << escapeCsv(l.confidenceMethod) << ','
            << l.generatedAt.toString(Qt::ISODate)
            << '\n';
    }
    return csv;
}

QString DataExporter::leadsToMarkdown(const QVector<InvestigativeLead>& leads,
                                       const QString& title)
{
    QString md;
    QTextStream out(&md);
    out << "# " << title << "\n\n";
    out << "Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n\n";
    out << "| Rank | Category | Headline | Confidence | Method |\n";
    out << "|------|----------|----------|------------|--------|\n";
    for (const auto& l : leads) {
        QString cat  = l.category;          cat.replace('|', '/');  cat.replace('\n', ' ');
        QString hd   = l.headline;          hd.replace('|', '/');   hd.replace('\n', ' ');
        QString meth = l.confidenceMethod;  meth.replace('|', '/'); meth.replace('\n', ' ');
        out << "| " << l.rank
            << " | " << cat
            << " | " << hd
            << " | " << QString::number(l.confidence * 100, 'f', 1) << "%"
            << " | " << meth
            << " |\n";
    }
    if (!leads.isEmpty()) {
        out << "\n## Details\n\n";
        for (const auto& l : leads) {
            out << "### " << l.rank << ". " << l.headline << "\n\n";
            out << l.detail << "\n\n";
            if (!l.provenance.empty()) {
                out << "**Provenance:** ";
                for (const auto& p : l.provenance) out << p << " → ";
                out << "\n\n";
            }
        }
    }
    return md;
}

// ─── Risk forecasts ───────────────────────────────────────────────────────────

QJsonArray DataExporter::forecastsToJson(const QVector<ZoneForecast>& forecasts)
{
    QJsonArray arr;
    for (const auto& zf : forecasts) {
        QJsonObject obj;
        obj[QStringLiteral("zoneId")]     = zf.zoneId;
        obj[QStringLiteral("weeklyRisk")] = zf.weeklyRisk;
        obj[QStringLiteral("alertLevel")] = zf.alertLevel;
        obj[QStringLiteral("alertLabel")] = zf.alertLabel();

        QJsonArray daysArr;
        for (const auto& day : zf.days) {
            QJsonObject d;
            d[QStringLiteral("date")]             = day.date.toString(Qt::ISODate);
            d[QStringLiteral("riskScore")]        = day.riskScore;
            d[QStringLiteral("baselineProb")]     = day.baselineProb;
            d[QStringLiteral("escalation")]       = day.escalationFactor;
            d[QStringLiteral("temporal")]         = day.temporalFactor;
            d[QStringLiteral("expectedCount")]    = day.expectedCount;
            d[QStringLiteral("explanation")]      = day.explanation;
            daysArr.append(d);
        }
        obj[QStringLiteral("days")] = daysArr;
        arr.append(obj);
    }
    return arr;
}

QString DataExporter::forecastsToCsv(const QVector<ZoneForecast>& forecasts)
{
    QString csv;
    QTextStream out(&csv);
    out << "zone_id,date,risk_score,baseline_prob,escalation,temporal,expected_count,alert_level\n";
    for (const auto& zf : forecasts) {
        for (const auto& day : zf.days) {
            out << escapeCsv(zf.zoneId) << ','
                << day.date.toString(Qt::ISODate) << ','
                << QString::number(day.riskScore, 'f', 4)        << ','
                << QString::number(day.baselineProb, 'f', 4)     << ','
                << QString::number(day.escalationFactor, 'f', 3) << ','
                << QString::number(day.temporalFactor, 'f', 3)   << ','
                << QString::number(day.expectedCount, 'f', 3)    << ','
                << zf.alertLevel
                << '\n';
        }
    }
    return csv;
}

// ─── Benchmark report ─────────────────────────────────────────────────────────

QJsonObject DataExporter::benchmarkToJson(const BenchmarkReport& report)
{
    QJsonObject obj;
    obj[QStringLiteral("nSamples")]  = report.nSamples;
    obj[QStringLiteral("pai05")]     = report.pai5pct;
    obj[QStringLiteral("pai10")]     = report.pai10pct;
    obj[QStringLiteral("pai20")]     = report.pai20pct;
    obj[QStringLiteral("pei10")]     = report.pei10pct;
    obj[QStringLiteral("ser")]       = report.ser;
    obj[QStringLiteral("aucRoc")]    = report.aucRoc;
    obj[QStringLiteral("aucPr")]     = report.aucPr;
    obj[QStringLiteral("mae")]       = report.mae;
    obj[QStringLiteral("rmse")]      = report.rmse;
    obj[QStringLiteral("brierScore")]= report.brierScore;
    obj[QStringLiteral("summary")]   = report.reportText();
    return obj;
}

QString DataExporter::benchmarkToMarkdown(const BenchmarkReport& report)
{
    QString md;
    QTextStream out(&md);
    out << "# SENTINEL Benchmark Report\n\n";
    out << "Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
        << "  n=" << report.nSamples << "\n\n";
    out << "| Metric | Value | Target | Status |\n";
    out << "|--------|-------|--------|--------|\n";

    auto row = [&](const QString& name, double val, double target, bool lowerIsBetter) {
        const bool pass = lowerIsBetter ? (val <= target) : (val >= target);
        out << "| " << name
            << " | " << QString::number(val, 'f', 4)
            << " | " << (lowerIsBetter ? "≤ " : "≥ ") << target
            << " | " << (pass ? "✓ PASS" : "✗ WARN")
            << " |\n";
    };

    row("PAI @ 5%",     report.pai5pct,    6.0,  false);
    row("PAI @ 10%",   report.pai10pct,   4.5,  false);
    row("PAI @ 20%",   report.pai20pct,   3.0,  false);
    row("PEI @ 10%",   report.pei10pct,   0.6,  false);
    row("SER",         report.ser,        0.4,  false);
    row("AUC-ROC",     report.aucRoc,     0.85, false);
    row("Brier Score", report.brierScore, 0.10, true);

    out << "\n**Summary:** " << report.reportText() << "\n";
    return md;
}

// ─── Provenance ───────────────────────────────────────────────────────────────

QJsonArray DataExporter::provenanceToJson(const QVector<ProvenanceEntry>& chain)
{
    QJsonArray arr;
    for (const auto& e : chain) {
        QJsonObject obj;
        obj[QStringLiteral("timestamp")] = e.timestamp.toString(Qt::ISODate);
        obj[QStringLiteral("eventId")]   = e.eventId;
        obj[QStringLiteral("stage")]     = e.stage;
        obj[QStringLiteral("action")]    = e.action;
        obj[QStringLiteral("detail")]    = e.detail;
        obj[QStringLiteral("dataHash")]  = e.dataHash;
        arr.append(obj);
    }
    return arr;
}

// ─── Crime events ─────────────────────────────────────────────────────────────

QJsonArray DataExporter::eventsToJson(const QVector<CrimeEvent>& events)
{
    QJsonArray arr;
    for (const auto& e : events) {
        QJsonObject obj;
        obj[QStringLiteral("eventId")]    = e.eventId;
        obj[QStringLiteral("crimeType")]  = e.crimeType;
        obj[QStringLiteral("suburb")]     = e.suburb;
        obj[QStringLiteral("lat")]        = e.lat.value_or(0.0);
        obj[QStringLiteral("lon")]        = e.lon.value_or(0.0);
        obj[QStringLiteral("occurredAt")] = e.occurredAt
            ? e.occurredAt->toString(Qt::ISODate) : QString{};
        obj[QStringLiteral("outcome")]    = e.outcome;
        obj[QStringLiteral("quality")]    = e.qualityScore;
        arr.append(obj);
    }
    return arr;
}

QString DataExporter::eventsToCsv(const QVector<CrimeEvent>& events)
{
    QString csv;
    QTextStream out(&csv);
    out << "event_id,crime_type,suburb,lat,lon,occurred_at,outcome,quality_score\n";
    for (const auto& e : events) {
        out << escapeCsv(e.eventId)    << ','
            << escapeCsv(e.crimeType) << ','
            << escapeCsv(e.suburb)    << ','
            << QString::number(e.lat.value_or(0.0), 'f', 6) << ','
            << QString::number(e.lon.value_or(0.0), 'f', 6) << ','
            << (e.occurredAt ? e.occurredAt->toString(Qt::ISODate) : QString{}) << ','
            << escapeCsv(e.outcome) << ','
            << QString::number(e.qualityScore, 'f', 3)
            << '\n';
    }
    return csv;
}

// ─── HTML export ──────────────────────────────────────────────────────────────

QString DataExporter::leadsToHtml(const QVector<InvestigativeLead>& leads,
                                   const QString& title)
{
    QString html;
    QTextStream out(&html);
    out << "<!DOCTYPE html>\n"
        << "<html>\n<head><meta charset=\"utf-8\"><title>"
        << title.toHtmlEscaped()
        << "</title></head>\n<body>\n"
        << "<h1>" << title.toHtmlEscaped() << "</h1>\n"
        << "<table>\n<thead>\n"
        << "<tr><th>Rank</th><th>Category</th><th>Confidence</th><th>Headline</th><th>Method</th></tr>\n"
        << "</thead>\n<tbody>\n";
    for (const auto& l : leads) {
        out << "<tr>"
            << "<td>" << l.rank << "</td>"
            << "<td>" << l.category.toHtmlEscaped() << "</td>"
            << "<td>" << QString::number(l.confidence * 100.0, 'f', 1) << "%" << "</td>"
            << "<td>" << l.headline.toHtmlEscaped() << "</td>"
            << "<td>" << l.confidenceMethod.toHtmlEscaped() << "</td>"
            << "</tr>\n";
    }
    out << "</tbody>\n</table>\n</body>\n</html>\n";
    return html;
}

QString DataExporter::eventsToHtml(const QVector<CrimeEvent>& events,
                                    const QString& title)
{
    QString html;
    QTextStream out(&html);
    out << "<!DOCTYPE html>\n"
        << "<html>\n<head><meta charset=\"utf-8\"><title>"
        << title.toHtmlEscaped()
        << "</title></head>\n<body>\n"
        << "<h1>" << title.toHtmlEscaped() << "</h1>\n"
        << "<table>\n<thead>\n"
        << "<tr><th>Event ID</th><th>Crime Type</th><th>Lat</th><th>Lon</th><th>Occurred At</th></tr>\n"
        << "</thead>\n<tbody>\n";
    for (const auto& e : events) {
        out << "<tr>"
            << "<td>" << e.eventId.toHtmlEscaped() << "</td>"
            << "<td>" << e.crimeType.toHtmlEscaped() << "</td>"
            << "<td>" << QString::number(e.lat.value_or(0.0), 'f', 6) << "</td>"
            << "<td>" << QString::number(e.lon.value_or(0.0), 'f', 6) << "</td>"
            << "<td>" << (e.occurredAt ? e.occurredAt->toString(Qt::ISODate).toHtmlEscaped() : QString{}) << "</td>"
            << "</tr>\n";
    }
    out << "</tbody>\n</table>\n</body>\n</html>\n";
    return html;
}

// ─── File helpers ─────────────────────────────────────────────────────────────

bool DataExporter::saveJson(const QJsonObject& obj, const QString& filePath)
{
    return saveText(QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Indented)), filePath);
}

bool DataExporter::saveJson(const QJsonArray& arr, const QString& filePath)
{
    return saveText(QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Indented)), filePath);
}

bool DataExporter::saveText(const QString& text, const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        s_lastError = QStringLiteral("Cannot open %1: %2")
                          .arg(filePath, file.errorString());
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    return true;
}
