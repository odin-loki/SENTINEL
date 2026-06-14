// LocalApiServer.h — Optional localhost read-only REST API (shipped in v1.0)
#pragma once

#include <QObject>
#include <memory>

#include "core/Database.h"

class LocalApiServer : public QObject {
    Q_OBJECT
public:
    explicit LocalApiServer(std::shared_ptr<Database> db, quint16 port = 8765,
                             QObject* parent = nullptr);
    ~LocalApiServer() override;

    bool start();
    void stop();
    bool isRunning() const;
    quint16 port() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    quint16               m_port = 8765;
};
