#pragma once
#include <QObject>
#include <QVector>
#include <QDateTime>
#include <functional>
#include "core/CrimeEvent.h"

class DataSource : public QObject {
    Q_OBJECT
public:
    explicit DataSource(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~DataSource() = default;

    virtual QString sourceId() const = 0;
    virtual QString displayName() const = 0;

    // Async fetch: emits eventFetched() for each event, then fetchComplete() or fetchError()
    virtual void fetchSince(const QDateTime& since) = 0;
    virtual bool healthCheck() = 0;

signals:
    void eventFetched(const CrimeEvent& ev);
    void fetchComplete(int count);
    void fetchError(const QString& message);
    void progress(int done, int total);
};
