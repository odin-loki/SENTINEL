#include "api/LocalApiServer.h"

#include "core/DataExporter.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#if defined(SENTINEL_HAS_QT_HTTPSERVER)
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#else
#include <QTcpServer>
#include <QTcpSocket>
#endif

namespace {

QJsonObject healthJson()
{
    QJsonObject obj;
    obj[QStringLiteral("status")] = QStringLiteral("ok");
#ifdef SENTINEL_VERSION
    obj[QStringLiteral("version")] = QStringLiteral(SENTINEL_VERSION);
#else
    obj[QStringLiteral("version")] = QStringLiteral("dev");
#endif
    return obj;
}

QJsonObject eventsJson(const Database* db, const QUrlQuery& query)
{
    const QString sinceStr = query.queryItemValue(QStringLiteral("since"));
    QVector<CrimeEvent> events;
    if (!sinceStr.isEmpty()) {
        QDateTime since = QDateTime::fromString(sinceStr, Qt::ISODateWithMs);
        if (!since.isValid())
            since = QDateTime::fromString(sinceStr, Qt::ISODate);
        if (!since.isValid())
            return {};
        events = db->getEventsSince(since);
    } else {
        events = db->getAllEvents();
    }

    QJsonObject obj;
    obj[QStringLiteral("count")]  = events.size();
    obj[QStringLiteral("events")] = DataExporter::eventsToJson(events);
    return obj;
}

QJsonObject leadsJson(const Database* db)
{
    const QVector<InvestigativeLead> leads = db->queryLeads();
    QJsonObject obj;
    obj[QStringLiteral("count")] = leads.size();
    obj[QStringLiteral("leads")] = DataExporter::leadsToJson(leads);
    return obj;
}

QString normalisePath(QString path)
{
    if (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const int qIdx = path.indexOf(QLatin1Char('?'));
    if (qIdx >= 0)
        path.truncate(qIdx);
    return path;
}

#if !defined(SENTINEL_HAS_QT_HTTPSERVER)

QByteArray httpEnvelope(int status, const QByteArray& body, const char* statusText)
{
    QByteArray out;
    out += "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText + "\r\n";
    out += "Content-Type: application/json\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "\r\n";
    out += body;
    return out;
}

QByteArray makeError(int status, const char* statusText, const QString& message)
{
    QJsonObject obj;
    obj[QStringLiteral("error")] = message;
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return httpEnvelope(status, body, statusText);
}

QByteArray dispatchRequest(const Database* db, const QString& method, const QString& rawPath,
                           const QUrlQuery& query)
{
    if (method != QStringLiteral("GET"))
        return makeError(405, "Method Not Allowed", QStringLiteral("Read-only API supports GET only"));

    const QString path = normalisePath(rawPath);
    if (path == QStringLiteral("/api/v1/health")) {
        const QByteArray body = QJsonDocument(healthJson()).toJson(QJsonDocument::Compact);
        return httpEnvelope(200, body, "OK");
    }
    if (path == QStringLiteral("/api/v1/events")) {
        const QString sinceStr = query.queryItemValue(QStringLiteral("since"));
        if (!sinceStr.isEmpty()) {
            QDateTime since = QDateTime::fromString(sinceStr, Qt::ISODateWithMs);
            if (!since.isValid())
                since = QDateTime::fromString(sinceStr, Qt::ISODate);
            if (!since.isValid())
                return makeError(400, "Bad Request", QStringLiteral("Invalid since timestamp"));
        }
        const QByteArray body = QJsonDocument(eventsJson(db, query)).toJson(QJsonDocument::Compact);
        return httpEnvelope(200, body, "OK");
    }
    if (path == QStringLiteral("/api/v1/leads")) {
        const QByteArray body = QJsonDocument(leadsJson(db)).toJson(QJsonDocument::Compact);
        return httpEnvelope(200, body, "OK");
    }

    return makeError(404, "Not Found", QStringLiteral("Unknown endpoint"));
}

class TcpApiServer final : public QTcpServer {
public:
    explicit TcpApiServer(Database* db, QObject* parent = nullptr)
        : QTcpServer(parent)
        , m_db(db)
    {}

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(handle)) {
            socket->deleteLater();
            return;
        }

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            if (socket->property("handled").toBool())
                return;
            socket->setProperty("handled", true);

            const QByteArray request = socket->readAll();
            const QList<QByteArray> lines = request.split('\n');
            if (lines.isEmpty()) {
                socket->write(makeError(400, "Bad Request", QStringLiteral("Malformed request")));
                socket->disconnectFromHost();
                return;
            }

            const QList<QByteArray> parts = lines.first().trimmed().split(' ');
            if (parts.size() < 2) {
                socket->write(makeError(400, "Bad Request", QStringLiteral("Malformed request line")));
                socket->disconnectFromHost();
                return;
            }

            const QString method = QString::fromLatin1(parts.at(0));
            const QString target = QString::fromLatin1(parts.at(1));
            const int qIdx = target.indexOf(QLatin1Char('?'));
            const QString path = (qIdx >= 0) ? target.left(qIdx) : target;
            QUrlQuery query;
            if (qIdx >= 0)
                query = QUrlQuery(target.mid(qIdx + 1));

            socket->write(dispatchRequest(m_db, method, path, query));
            socket->disconnectFromHost();
        });

        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    Database* m_db = nullptr;
};

#endif

} // namespace

class LocalApiServer::Impl {
public:
    explicit Impl(LocalApiServer* owner, std::shared_ptr<Database> db, quint16 port)
        : m_owner(owner)
        , m_db(std::move(db))
        , m_port(port)
    {}

    bool start()
    {
        stop();
#if defined(SENTINEL_HAS_QT_HTTPSERVER)
        m_http = std::make_unique<QHttpServer>();

        m_http->route(QStringLiteral("/api/v1/health"), QHttpServerRequest::Method::Get,
                      [](const QHttpServerRequest&) {
                          return QHttpServerResponse(healthJson());
                      });

        m_http->route(QStringLiteral("/api/v1/events"), QHttpServerRequest::Method::Get,
                      [this](const QHttpServerRequest& req) {
                          QUrlQuery query(req.url().query(QUrl::FullyDecoded));
                          const QString sinceStr = query.queryItemValue(QStringLiteral("since"));
                          if (!sinceStr.isEmpty()) {
                              QDateTime since = QDateTime::fromString(sinceStr, Qt::ISODateWithMs);
                              if (!since.isValid())
                                  since = QDateTime::fromString(sinceStr, Qt::ISODate);
                              if (!since.isValid()) {
                                  QJsonObject err;
                                  err[QStringLiteral("error")] =
                                      QStringLiteral("Invalid since timestamp");
                                  return QHttpServerResponse(QHttpServerResponse::StatusCode(400),
                                                             err);
                              }
                          }
                          return QHttpServerResponse(eventsJson(m_db.get(), query));
                      });

        m_http->route(QStringLiteral("/api/v1/leads"), QHttpServerRequest::Method::Get,
                      [this](const QHttpServerRequest&) {
                          return QHttpServerResponse(leadsJson(m_db.get()));
                      });

        const auto bound = m_http->listen(QHostAddress::LocalHost, m_port);
        if (!bound)
            return false;
        m_port = bound->serverPort();
        return true;
#else
        m_tcp = new TcpApiServer(m_db.get(), m_owner);
        if (!m_tcp->listen(QHostAddress::LocalHost, m_port))
            return false;
        if (m_port == 0)
            m_port = static_cast<quint16>(m_tcp->serverPort());
        return true;
#endif
    }

    void stop()
    {
#if defined(SENTINEL_HAS_QT_HTTPSERVER)
        m_http.reset();
#else
        if (m_tcp) {
            m_tcp->close();
            m_tcp->deleteLater();
            m_tcp = nullptr;
        }
#endif
    }

    bool isRunning() const
    {
#if defined(SENTINEL_HAS_QT_HTTPSERVER)
        return m_http != nullptr;
#else
        return m_tcp && m_tcp->isListening();
#endif
    }

    quint16 port() const { return m_port; }

private:
    LocalApiServer*           m_owner = nullptr;
    std::shared_ptr<Database> m_db;
    quint16                   m_port  = 8765;
#if defined(SENTINEL_HAS_QT_HTTPSERVER)
    std::unique_ptr<QHttpServer> m_http;
#else
    TcpApiServer* m_tcp = nullptr;
#endif
};

LocalApiServer::LocalApiServer(std::shared_ptr<Database> db, quint16 port, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, std::move(db), port))
    , m_port(port)
{
}

LocalApiServer::~LocalApiServer()
{
    stop();
}

bool LocalApiServer::start()
{
    if (!m_impl || !m_impl->start())
        return false;
    m_port = m_impl->port();
    return true;
}

void LocalApiServer::stop()
{
    if (m_impl)
        m_impl->stop();
}

bool LocalApiServer::isRunning() const
{
    return m_impl && m_impl->isRunning();
}

quint16 LocalApiServer::port() const
{
    return m_impl ? m_impl->port() : m_port;
}
