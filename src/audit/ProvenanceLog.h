#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>
#include <QMutex>

struct ProvenanceEntry {
    QDateTime timestamp;
    QString   eventId;
    QString   stage;       // ingest | nlp | model | inference | output
    QString   source;      // data source (audit/export API)
    QString   model;       // model or pipeline stage name (audit/export API)
    QString   action;
    QString   detail;
    QString   dataHash;    // SHA256 of relevant input data (first 16 chars)
};

class ProvenanceLog {
public:
    void record(const QString& eventId,
                const QString& stage,
                const QString& action,
                const QString& detail,
                const QString& dataHash = {});

    void addEntry(const QString& source,
                  const QString& model,
                  const QString& action,
                  const QString& detail,
                  const QDateTime& timestamp = {});

    QVector<ProvenanceEntry> getEntries() const;
    QVector<ProvenanceEntry> chain(const QString& eventId) const;
    QVector<ProvenanceEntry> recent(int n = 100) const;
    QVector<ProvenanceEntry> filterByStage(const QString& stage) const;
    QVector<ProvenanceEntry> filterByModel(const QString& model) const;
    QVector<ProvenanceEntry> filterBySource(const QString& source) const;
    QVector<ProvenanceEntry> filterByTimeRange(const QDateTime& from,
                                               const QDateTime& to) const;
    int  count() const;
    int  size() const;
    void clear();

    QString exportToJson() const;
    QString exportToCsv() const;

    // Export all entries for an event as a human-readable string
    QString formatChain(const QString& eventId) const;

    // Export all entries for an event as a styled dark-theme HTML table
    QString formatHtml(const QString& eventId) const;

private:
    static QString csvEscape(const QString& field);

    mutable QMutex m_mutex;
    QVector<ProvenanceEntry> m_entries;
};
