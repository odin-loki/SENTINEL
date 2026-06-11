#include "ingest/CsvImporter.h"
#include "core/SentinelLogger.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDateTime>
#include <QTimeZone>
#include <QUuid>
#include <QtMath>
#include <algorithm>

// ---------------------------------------------------------------------------
// CSV parsing helper: handles quoted fields with embedded commas and newlines
// ---------------------------------------------------------------------------
static QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString field;
    bool inQuotes = false;

    bool wasQuoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (ch == QLatin1Char('"')) {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == QLatin1Char('"')) {
                field += QLatin1Char('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
                wasQuoted = true;
            }
        } else if (ch == QLatin1Char(',') && !inQuotes) {
            fields.append(wasQuoted ? field : field.trimmed());
            field.clear();
            wasQuoted = false;
        } else {
            field += ch;
        }
    }
    fields.append(wasQuoted ? field : field.trimmed());
    return fields;
}

// ---------------------------------------------------------------------------
// Column detection helpers
// ---------------------------------------------------------------------------
static bool headerMatches(const QString& header, const QStringList& keywords)
{
    const QString lower = header.toLower();
    for (const QString& kw : keywords) {
        if (lower.contains(kw)) return true;
    }
    return false;
}

CsvColumnMap CsvImporter::detectColumns(const QStringList& headers)
{
    CsvColumnMap map;
    for (int i = 0; i < headers.size(); ++i) {
        const QString& h = headers[i];
        if (map.idCol < 0 && headerMatches(h, {QStringLiteral("id"), QStringLiteral("case_number"), QStringLiteral("case number"), QStringLiteral("incident"), QStringLiteral("crime id")}))
            map.idCol = i;
        else if (map.dateCol < 0 && headerMatches(h, {QStringLiteral("date"), QStringLiteral("occurred"), QStringLiteral("datetime"), QStringLiteral("timestamp"), QStringLiteral("reported_date"), QStringLiteral("month")}))
            map.dateCol = i;
        else if (map.crimeTypeCol < 0 && headerMatches(h, {QStringLiteral("crime_type"), QStringLiteral("primary_type"), QStringLiteral("category"), QStringLiteral("offense"), QStringLiteral("offence"), QStringLiteral("crime type"), QStringLiteral("crimetype"), QStringLiteral("type")}))
            map.crimeTypeCol = i;
        else if (map.descCol < 0 && headerMatches(h, {QStringLiteral("description"), QStringLiteral("narrative"), QStringLiteral("detail"), QStringLiteral("summary")}))
            map.descCol = i;
        else if (map.latCol < 0 && headerMatches(h, {QStringLiteral("lat"), QStringLiteral("latitude"), QStringLiteral("y_coord")}))
            map.latCol = i;
        else if (map.lonCol < 0 && headerMatches(h, {QStringLiteral("lon"), QStringLiteral("lng"), QStringLiteral("longitude"), QStringLiteral("x_coord")}))
            map.lonCol = i;
        else if (map.addressCol < 0 && headerMatches(h, {QStringLiteral("address"), QStringLiteral("street"), QStringLiteral("block")}))
            map.addressCol = i;
        else if (map.outcomeCol < 0 && headerMatches(h, {QStringLiteral("outcome"), QStringLiteral("resolution"), QStringLiteral("disposition"), QStringLiteral("arrest"), QStringLiteral("last outcome category")}))
            map.outcomeCol = i;
        else if (map.locationCol < 0 && headerMatches(h, {QStringLiteral("location"), QStringLiteral("location_description"), QStringLiteral("location description")}))
            map.locationCol = i;
    }
    return map;
}

// ---------------------------------------------------------------------------
// Date parsing: try common formats
// ---------------------------------------------------------------------------
static std::optional<QDateTime> parseDate(const QString& raw)
{
    if (raw.isEmpty()) return std::nullopt;

    static const QStringList formats = {
        QStringLiteral("MM/dd/yyyy hh:mm:ss AP"),
        QStringLiteral("MM/dd/yyyy HH:mm:ss"),
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("dd/MM/yyyy HH:mm"),
        QStringLiteral("dd/MM/yyyy"),
        QStringLiteral("M/d/yyyy H:mm"),
    };

    for (const QString& fmt : formats) {
        QDateTime dt = QDateTime::fromString(raw, fmt);
        if (dt.isValid()) {
            if (dt.timeSpec() == Qt::LocalTime)
                dt.setTimeZone(QTimeZone::utc());
            return dt;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Single row parser
// ---------------------------------------------------------------------------
CrimeEvent CsvImporter::parseRow(const QStringList& fields,
                                  const CsvColumnMap& map,
                                  const QString& sourceTag)
{
    auto safeField = [&](int col) -> QString {
        if (col < 0 || col >= fields.size()) return {};
        return fields[col].trimmed();
    };

    CrimeEvent ev;
    ev.source        = sourceTag;
    ev.sourceVersion = QStringLiteral("1.0");
    ev.ingestedAt    = QDateTime::currentDateTimeUtc();

    // Event ID
    const QString rawId = safeField(map.idCol);
    ev.eventId = rawId.isEmpty()
        ? (sourceTag + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::WithoutBraces))
        : (sourceTag + QLatin1Char('_') + rawId);

    // Date
    const auto dt = parseDate(safeField(map.dateCol));
    if (dt) ev.occurredAt = dt;

    // Crime type
    const QString ct = safeField(map.crimeTypeCol);
    if (!ct.isEmpty()) ev.crimeType = ct.toLower();

    // Description / narrative
    const QString desc = safeField(map.descCol);
    if (!desc.isEmpty()) ev.narrative = desc;

    // Lat / lon
    bool latOk = false, lonOk = false;
    const double lat = safeField(map.latCol).toDouble(&latOk);
    const double lon = safeField(map.lonCol).toDouble(&lonOk);
    if (latOk && qAbs(lat) <= 90.0)  ev.lat = lat;
    if (lonOk && qAbs(lon) <= 180.0) ev.lon = lon;

    // Address
    const QString addr = safeField(map.addressCol);
    if (!addr.isEmpty()) ev.addressNormalised = addr;

    // Raw location description
    const QString locDesc = safeField(map.locationCol);
    if (!locDesc.isEmpty()) ev.locationRaw = locDesc;

    // Outcome
    const QString outcome = safeField(map.outcomeCol);
    if (!outcome.isEmpty()) ev.outcome = outcome;

    ev.qualityScore = 0.5;
    return ev;
}

// ---------------------------------------------------------------------------
// File importer
// ---------------------------------------------------------------------------
QVector<CrimeEvent> CsvImporter::importFile(const QString& filePath,
                                              const QString& sourceTag,
                                              std::function<void(int, int)> progressCb)
{
    QVector<CrimeEvent> results;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcIngest) << "Cannot open CSV file:" << filePath;
        return results;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    // Read header
    if (stream.atEnd()) return results;
    const QString headerLine = stream.readLine();
    const QStringList headers = parseCsvLine(headerLine);

    CsvColumnMap map = detectColumns(headers);
    map.sourceTag    = sourceTag;

    // Count lines for progress reporting (cheap re-read via size estimate)
    // We'll do a two-pass only if a progress callback is provided
    qint64 fileSize  = file.size();
    int    estimated = (fileSize > 0) ? static_cast<int>(fileSize / 120) : 1000;
    int    done      = 0;

    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (line.trimmed().isEmpty()) continue;

        // Handle quoted multi-line fields (rare but possible)
        while (line.count(QLatin1Char('"')) % 2 != 0 && !stream.atEnd()) {
            line += QLatin1Char('\n') + stream.readLine();
        }

        const QStringList fields = parseCsvLine(line);
        if (fields.isEmpty()) continue;

        const bool missingDate = (map.dateCol < 0 || map.dateCol >= fields.size()
                                    || fields[map.dateCol].trimmed().isEmpty());
        const bool missingType = (map.crimeTypeCol < 0 || map.crimeTypeCol >= fields.size()
                                    || fields[map.crimeTypeCol].trimmed().isEmpty());
        if (missingDate && missingType) {
            qCWarning(lcIngest) << "Skipping row" << done << "- missing required fields";
        } else {
            results.append(parseRow(fields, map, sourceTag));
        }
        ++done;

        if (progressCb && (done % 500 == 0)) {
            progressCb(done, std::max(done, estimated));
        }
    }

    qCInfo(lcIngest) << "Importing CSV:" << filePath << "rows:" << done;

    if (progressCb) {
        progressCb(done, done);
    }

    return results;
}
