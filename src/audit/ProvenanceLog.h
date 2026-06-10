#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>

struct ProvenanceEntry {
    QDateTime timestamp;
    QString   eventId;
    QString   stage;       // ingest | nlp | model | inference | output
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

    QVector<ProvenanceEntry> chain(const QString& eventId) const;
    QVector<ProvenanceEntry> recent(int n = 100) const;
    QVector<ProvenanceEntry> filterByStage(const QString& stage) const;
    int  count() const { return m_entries.size(); }
    void clear();

    // Export all entries for an event as a human-readable string
    QString formatChain(const QString& eventId) const;

    // Export all entries for an event as a styled dark-theme HTML table
    QString formatHtml(const QString& eventId) const;

private:
    QVector<ProvenanceEntry> m_entries;
};
