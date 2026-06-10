#pragma once
#include <QString>
#include <QVector>
#include <QMap>
#include <functional>
#include "core/CrimeEvent.h"

struct CsvColumnMap {
    int idCol        = -1;
    int dateCol      = -1;
    int crimeTypeCol = -1;
    int descCol      = -1;
    int latCol       = -1;
    int lonCol       = -1;
    int addressCol   = -1;
    int outcomeCol   = -1;
    int locationCol  = -1;
    QString sourceTag;   // identifier for the source city/agency
};

class CsvImporter {
public:
    // Auto-detect column mapping from header row
    static CsvColumnMap detectColumns(const QStringList& headers);

    // Import a CSV file; emit progress via callback(done, total)
    static QVector<CrimeEvent> importFile(
        const QString& filePath,
        const QString& sourceTag = QStringLiteral("csv_import"),
        std::function<void(int, int)> progressCb = nullptr);

    // Parse a single data row given a column map
    static CrimeEvent parseRow(const QStringList& fields,
                                const CsvColumnMap& map,
                                const QString& sourceTag);
};
