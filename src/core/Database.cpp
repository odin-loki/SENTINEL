#include "core/Database.h"
#include "core/SentinelLogger.h"
#include <QSqlRecord>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <QDebug>
#include <QUuid>
#include <QAtomicInt>

// ─────────────────────────────────────────────────────────────────────────────
// Construction / lifecycle
// ─────────────────────────────────────────────────────────────────────────────

Database::Database(const AppConfig& cfg)
    : m_path(cfg.databasePath)
{
    // Each instance gets a unique connection name so Qt SQL pool entries
    // from deleted Database objects never contaminate new ones.
    static QAtomicInt s_counter{0};
    m_connName = QStringLiteral("sentinel_db_%1").arg(s_counter.fetchAndAddOrdered(1));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(m_path);
}

Database::~Database()
{
    close();
    QSqlDatabase::removeDatabase(m_connName);
}

bool Database::open()
{
    if (m_db.isOpen())
        return true;

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        qWarning() << "[Database] Failed to open:" << m_lastError;
        return false;
    }

    qCInfo(lcDatabase) << "Database opened:" << m_path;

    // Enable WAL mode for better concurrent read/write performance
    QSqlQuery pragma(m_db);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) {
        m_lastError = pragma.lastError().text();
        qCWarning(lcDatabase) << "Failed to set WAL journal mode:" << m_lastError;
    }
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA cache_size=4000"));
    pragma.exec(QStringLiteral("PRAGMA temp_store=MEMORY"));

    createSchema();

    // Run migrations if the stored version is behind the compiled version
    const int storedVersion = currentSchemaVersion();
    if (storedVersion < SCHEMA_VERSION) {
        if (!migrateSchema(storedVersion)) {
            qCWarning(lcDatabase) << "Schema migration partially failed;"
                                  << "some features may be unavailable.";
        }
    }

    return true;
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
}

bool Database::isOpen() const
{
    return m_db.isOpen();
}

QString Database::lastError() const
{
    return m_lastError;
}

// ─────────────────────────────────────────────────────────────────────────────
// Schema creation
// ─────────────────────────────────────────────────────────────────────────────

void Database::createSchema()
{
    QSqlQuery q(m_db);

    const QStringList ddl = {
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS events (
                event_id          TEXT PRIMARY KEY,
                source            TEXT,
                source_version    TEXT,
                ingested_at       TEXT,
                occurred_at       TEXT,
                reported_at       TEXT,
                crime_type        TEXT,
                crime_subtype     TEXT,
                location_raw      TEXT,
                lat               REAL,
                lon               REAL,
                address_normalised TEXT,
                lga               TEXT,
                suburb            TEXT,
                narrative         TEXT,
                outcome           TEXT,
                conviction        INTEGER,
                suspect_count     INTEGER,
                victim_count      INTEGER,
                weapon            TEXT,
                meta              TEXT,
                quality_score     REAL
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS leads (
                id                  INTEGER PRIMARY KEY AUTOINCREMENT,
                event_id            TEXT,
                rank                INTEGER,
                category            TEXT,
                headline            TEXT,
                detail              TEXT,
                confidence          REAL,
                confidence_method   TEXT,
                supporting_data     TEXT,
                contradictions      TEXT,
                provenance          TEXT,
                generated_at        TEXT
            )
        )"),
        QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS audit_log (
                id        INTEGER PRIMARY KEY AUTOINCREMENT,
                ts        TEXT,
                event_id  TEXT,
                action    TEXT,
                detail    TEXT
            )
        )"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_occurred ON events(occurred_at)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_type     ON events(crime_type)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_lat      ON events(lat)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_source   ON events(source)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_events_suburb   ON events(suburb)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_leads_event     ON leads(event_id)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_audit_event     ON audit_log(event_id)"),
    };

    for (const QString& stmt : ddl) {
        if (!q.exec(stmt)) {
            m_lastError = q.lastError().text();
            qWarning() << "[Database] Schema DDL failed:" << m_lastError;
        }
    }

    // Create version table and seed version if this is a fresh database
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)"));
    q.exec(QStringLiteral("SELECT COUNT(*) FROM schema_version"));
    if (q.next() && q.value(0).toInt() == 0) {
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral("INSERT INTO schema_version (version) VALUES (:v)"));
        ins.bindValue(QStringLiteral(":v"), SCHEMA_VERSION);
        ins.exec();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Schema versioning
// ─────────────────────────────────────────────────────────────────────────────

int Database::currentSchemaVersion() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1")) && q.next())
        return q.value(0).toInt();
    return 0;
}

bool Database::migrateSchema(int fromVersion)
{
    if (fromVersion >= SCHEMA_VERSION)
        return true;

    qCInfo(lcDatabase) << "Migrating schema from version" << fromVersion
                       << "to" << SCHEMA_VERSION;

    QSqlQuery q(m_db);
    bool ok = true;

    // Migration v1 → v2: add quality_score column if missing (legacy databases)
    if (fromVersion < 2) {
        // SQLite ADD COLUMN is idempotent-safe — silently ignore if column exists
        q.exec(QStringLiteral(
            "ALTER TABLE events ADD COLUMN quality_score REAL DEFAULT 1.0"));
        if (q.lastError().isValid() &&
            !q.lastError().text().contains("duplicate column")) {
            qCWarning(lcDatabase) << "v2 migration failed:" << q.lastError().text();
            ok = false;
        }
    }

    // Migration v2 → v3: add narrative_tags column (comma-separated NLP tags)
    if (fromVersion < 3) {
        q.exec(QStringLiteral(
            "ALTER TABLE events ADD COLUMN narrative_tags TEXT DEFAULT ''"));
        if (q.lastError().isValid() &&
            !q.lastError().text().contains("duplicate column")) {
            qCWarning(lcDatabase) << "v3 migration failed:" << q.lastError().text();
            ok = false;
        }

        // Add a zone_risk_scores table for caching BayesianHierarchical outputs
        q.exec(QStringLiteral(R"(
            CREATE TABLE IF NOT EXISTS zone_risk_scores (
                zone_id       TEXT PRIMARY KEY,
                posterior_mean REAL,
                posterior_var  REAL,
                credible_low   REAL,
                credible_high  REAL,
                updated_at     TEXT
            )
        )"));
        if (q.lastError().isValid()) {
            qCWarning(lcDatabase) << "v3 zone_risk_scores table failed:" << q.lastError().text();
        }
    }

    if (ok) {
        QSqlQuery upd(m_db);
        upd.prepare(QStringLiteral("UPDATE schema_version SET version = :v"));
        upd.bindValue(QStringLiteral(":v"), SCHEMA_VERSION);
        ok = upd.exec();
    }

    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: optional value → QVariant (NULL when absent)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

template<typename T>
QVariant optVar(const std::optional<T>& opt) {
    if (opt.has_value())
        return QVariant::fromValue(*opt);
    return QVariant{};
}

// Non-optional QString → NULL when empty
QVariant strVar(const QString& s) {
    return s.isEmpty() ? QVariant{} : QVariant(s);
}

QVariant optDateVar(const std::optional<QDateTime>& opt) {
    if (opt.has_value())
        return opt->toString(Qt::ISODate);
    return QVariant{};
}

QString jsonObjToStr(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject strToJsonObj(const QString& s) {
    if (s.isEmpty()) return {};
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

QString strVecToJson(const std::vector<QString>& v) {
    QJsonArray arr;
    for (const auto& s : v)
        arr.append(s);
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

std::vector<QString> jsonToStrVec(const QString& s) {
    std::vector<QString> result;
    if (s.isEmpty()) return result;
    const QJsonArray arr = QJsonDocument::fromJson(s.toUtf8()).array();
    result.reserve(static_cast<size_t>(arr.size()));
    for (const auto& v : arr)
        result.push_back(v.toString());
    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Event CRUD
// ─────────────────────────────────────────────────────────────────────────────

bool Database::insertEvent(const CrimeEvent& ev)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT OR REPLACE INTO events (
            event_id, source, source_version,
            ingested_at, occurred_at, reported_at,
            crime_type, crime_subtype,
            location_raw, lat, lon,
            address_normalised, lga, suburb,
            narrative, outcome,
            conviction, suspect_count, victim_count,
            weapon, meta, quality_score
        ) VALUES (
            :event_id, :source, :source_version,
            :ingested_at, :occurred_at, :reported_at,
            :crime_type, :crime_subtype,
            :location_raw, :lat, :lon,
            :address_normalised, :lga, :suburb,
            :narrative, :outcome,
            :conviction, :suspect_count, :victim_count,
            :weapon, :meta, :quality_score
        )
    )"));

    q.bindValue(QStringLiteral(":event_id"),           ev.eventId);
    q.bindValue(QStringLiteral(":source"),             ev.source);
    q.bindValue(QStringLiteral(":source_version"),     ev.sourceVersion);
    q.bindValue(QStringLiteral(":ingested_at"),        ev.ingestedAt.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":occurred_at"),        optDateVar(ev.occurredAt));
    q.bindValue(QStringLiteral(":reported_at"),        optDateVar(ev.reportedAt));
    q.bindValue(QStringLiteral(":crime_type"),         strVar(ev.crimeType));
    q.bindValue(QStringLiteral(":crime_subtype"),      optVar(ev.crimeSubtype));
    q.bindValue(QStringLiteral(":location_raw"),       optVar(ev.locationRaw));
    if (ev.lat.has_value())
        q.bindValue(QStringLiteral(":lat"), ev.lat.value());
    else if (ev.latitude != 0.0)
        q.bindValue(QStringLiteral(":lat"), ev.latitude);
    else
        q.bindValue(QStringLiteral(":lat"), QVariant{});
    if (ev.lon.has_value())
        q.bindValue(QStringLiteral(":lon"), ev.lon.value());
    else if (ev.longitude != 0.0)
        q.bindValue(QStringLiteral(":lon"), ev.longitude);
    else
        q.bindValue(QStringLiteral(":lon"), QVariant{});
    q.bindValue(QStringLiteral(":address_normalised"), optVar(ev.addressNormalised));
    q.bindValue(QStringLiteral(":lga"),                optVar(ev.lga));
    q.bindValue(QStringLiteral(":suburb"),             strVar(ev.suburb));
    q.bindValue(QStringLiteral(":narrative"),          optVar(ev.narrative));
    q.bindValue(QStringLiteral(":outcome"),            strVar(ev.outcome));

    if (ev.conviction.has_value())
        q.bindValue(QStringLiteral(":conviction"), ev.conviction.value() ? 1 : 0);
    else
        q.bindValue(QStringLiteral(":conviction"), QVariant{});

    q.bindValue(QStringLiteral(":suspect_count"), optVar(ev.suspectCount));
    q.bindValue(QStringLiteral(":victim_count"),  optVar(ev.victimCount));
    q.bindValue(QStringLiteral(":weapon"),        optVar(ev.weapon));
    q.bindValue(QStringLiteral(":meta"),          jsonObjToStr(ev.meta));
    q.bindValue(QStringLiteral(":quality_score"), ev.qualityScore);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qCWarning(lcDatabase) << "insertEvent failed:" << m_lastError;
        return false;
    }
    return true;
}

bool Database::updateEvent(const CrimeEvent& ev)
{
    // INSERT OR REPLACE handles both insert and update for primary key matches
    return insertEvent(ev);
}

bool Database::deleteEvent(const QString& id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM events WHERE event_id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qCWarning(lcDatabase) << "deleteEvent failed:" << m_lastError;
        return false;
    }
    if (q.numRowsAffected() == 0) {
        m_lastError = QStringLiteral("Event not found: %1").arg(id);
        return false;
    }
    return true;
}

int Database::batchInsert(const QVector<CrimeEvent>& events)
{
    if (events.isEmpty()) return 0;
    m_db.transaction();
    int inserted = 0;
    for (const auto& ev : events) {
        if (insertEvent(ev))
            ++inserted;
    }
    m_db.commit();
    return inserted;
}

QVector<CrimeEvent> Database::queryEvents(const QString& crimeType,
                                           const QDateTime& from,
                                           const QDateTime& to,
                                           double latMin, double latMax,
                                           double lonMin, double lonMax,
                                           int limit) const
{
    QString sql = QStringLiteral("SELECT * FROM events WHERE 1=1");
    QVector<std::pair<QString,QVariant>> bindings;

    if (!crimeType.isEmpty()) {
        sql += QStringLiteral(" AND LOWER(crime_type) = LOWER(:crime_type)");
        bindings.append({QStringLiteral(":crime_type"), crimeType});
    }
    if (from.isValid()) {
        sql += QStringLiteral(" AND occurred_at >= :from");
        bindings.append({QStringLiteral(":from"), from.toString(Qt::ISODate)});
    }
    if (to.isValid()) {
        sql += QStringLiteral(" AND occurred_at <= :to");
        bindings.append({QStringLiteral(":to"), to.toString(Qt::ISODate)});
    }

    // Only filter by geo bounds if they differ from the default "accept all"
    constexpr double kLatMin = -90.0, kLatMax = 90.0;
    constexpr double kLonMin = -180.0, kLonMax = 180.0;

    if (latMin > kLatMin || latMax < kLatMax) {
        sql += QStringLiteral(" AND lat >= :lat_min AND lat <= :lat_max");
        bindings.append({QStringLiteral(":lat_min"), latMin});
        bindings.append({QStringLiteral(":lat_max"), latMax});
    }
    if (lonMin > kLonMin || lonMax < kLonMax) {
        sql += QStringLiteral(" AND lon >= :lon_min AND lon <= :lon_max");
        bindings.append({QStringLiteral(":lon_min"), lonMin});
        bindings.append({QStringLiteral(":lon_max"), lonMax});
    }

    sql += QStringLiteral(" ORDER BY occurred_at DESC LIMIT :limit");

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& [name, val] : bindings)
        q.bindValue(name, val);
    q.bindValue(QStringLiteral(":limit"), limit);

    QVector<CrimeEvent> results;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] queryEvents failed:" << m_lastError;
        return results;
    }
    while (q.next())
        results.append(rowToEvent(q));

    return results;
}

CrimeEvent Database::eventById(const QString& id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT * FROM events WHERE event_id = :id LIMIT 1"));
    q.bindValue(QStringLiteral(":id"), id);
    if (q.exec() && q.next())
        return rowToEvent(q);
    return {};
}

int Database::eventCount() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM events")) && q.next())
        return q.value(0).toInt();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Leads
// ─────────────────────────────────────────────────────────────────────────────

bool Database::insertLead(const InvestigativeLead& lead, const QString& eventId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        INSERT INTO leads (
            event_id, rank, category, headline, detail,
            confidence, confidence_method, supporting_data,
            contradictions, provenance, generated_at
        ) VALUES (
            :event_id, :rank, :category, :headline, :detail,
            :confidence, :confidence_method, :supporting_data,
            :contradictions, :provenance, :generated_at
        )
    )"));

    q.bindValue(QStringLiteral(":event_id"),           eventId);
    q.bindValue(QStringLiteral(":rank"),               lead.rank);
    q.bindValue(QStringLiteral(":category"),           lead.category);
    q.bindValue(QStringLiteral(":headline"),           lead.headline);
    q.bindValue(QStringLiteral(":detail"),             lead.detail);
    q.bindValue(QStringLiteral(":confidence"),         lead.confidence);
    q.bindValue(QStringLiteral(":confidence_method"),  lead.confidenceMethod);
    q.bindValue(QStringLiteral(":supporting_data"),    jsonObjToStr(lead.supportingData));
    q.bindValue(QStringLiteral(":contradictions"),     strVecToJson(lead.contradictions));
    q.bindValue(QStringLiteral(":provenance"),         strVecToJson(lead.provenance));
    q.bindValue(QStringLiteral(":generated_at"),       lead.generatedAt.toString(Qt::ISODate));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] insertLead failed:" << m_lastError;
        return false;
    }
    return true;
}

QVector<InvestigativeLead> Database::queryLeads(const QString& eventId) const
{
    QString sql = QStringLiteral("SELECT * FROM leads");
    if (!eventId.isEmpty())
        sql += QStringLiteral(" WHERE event_id = :event_id");
    sql += QStringLiteral(" ORDER BY rank ASC");

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (!eventId.isEmpty())
        q.bindValue(QStringLiteral(":event_id"), eventId);

    QVector<InvestigativeLead> results;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] queryLeads failed:" << m_lastError;
        return results;
    }
    while (q.next())
        results.append(rowToLead(q));

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Audit
// ─────────────────────────────────────────────────────────────────────────────

bool Database::insertAuditEntry(const QString& eventId,
                                 const QString& action,
                                 const QString& detail)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO audit_log (ts, event_id, action, detail) "
        "VALUES (:ts, :event_id, :action, :detail)"
    ));
    q.bindValue(QStringLiteral(":ts"),       QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":event_id"), eventId);
    q.bindValue(QStringLiteral(":action"),   action);
    q.bindValue(QStringLiteral(":detail"),   detail);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] insertAuditEntry failed:" << m_lastError;
        return false;
    }
    return true;
}

QVector<std::tuple<QDateTime,QString,QString,QString>>
Database::queryAudit(int limit) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT ts, event_id, action, detail FROM audit_log "
        "ORDER BY ts DESC, id DESC LIMIT :limit"
    ));
    q.bindValue(QStringLiteral(":limit"), limit);

    QVector<std::tuple<QDateTime,QString,QString,QString>> results;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] queryAudit failed:" << m_lastError;
        return results;
    }
    while (q.next()) {
        results.append({
            QDateTime::fromString(q.value(0).toString(), Qt::ISODate),
            q.value(1).toString(),
            q.value(2).toString(),
            q.value(3).toString()
        });
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats helpers
// ─────────────────────────────────────────────────────────────────────────────

QMap<QString,int> Database::crimeTypeCounts() const
{
    QSqlQuery q(m_db);
    QMap<QString,int> result;
    if (!q.exec(QStringLiteral(
        "SELECT crime_type, COUNT(*) FROM events "
        "WHERE crime_type IS NOT NULL "
        "GROUP BY crime_type ORDER BY COUNT(*) DESC")))
    {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] crimeTypeCounts failed:" << m_lastError;
        return result;
    }
    while (q.next())
        result.insert(q.value(0).toString(), q.value(1).toInt());
    return result;
}

QVector<std::pair<QDateTime,int>>
Database::eventsByHour(const QDateTime& from, const QDateTime& to) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"(
        SELECT strftime('%Y-%m-%dT%H:00:00', occurred_at) AS hour,
               COUNT(*) AS cnt
        FROM events
        WHERE occurred_at >= :from AND occurred_at <= :to
          AND occurred_at IS NOT NULL
        GROUP BY hour
        ORDER BY hour ASC
    )"));
    q.bindValue(QStringLiteral(":from"), from.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":to"),   to.toString(Qt::ISODate));

    QVector<std::pair<QDateTime,int>> result;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] eventsByHour failed:" << m_lastError;
        return result;
    }
    while (q.next()) {
        result.append({
            QDateTime::fromString(q.value(0).toString(), Qt::ISODate),
            q.value(1).toInt()
        });
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private row-mapping helpers
// ─────────────────────────────────────────────────────────────────────────────

CrimeEvent Database::rowToEvent(const QSqlQuery& q)
{
    CrimeEvent ev;
    ev.eventId        = q.value(QStringLiteral("event_id")).toString();
    ev.source         = q.value(QStringLiteral("source")).toString();
    ev.sourceVersion  = q.value(QStringLiteral("source_version")).toString();
    ev.ingestedAt     = QDateTime::fromString(
                            q.value(QStringLiteral("ingested_at")).toString(),
                            Qt::ISODate);

    const QString occ = q.value(QStringLiteral("occurred_at")).toString();
    if (!occ.isEmpty())
        ev.occurredAt = QDateTime::fromString(occ, Qt::ISODate);

    const QString rep = q.value(QStringLiteral("reported_at")).toString();
    if (!rep.isEmpty())
        ev.reportedAt = QDateTime::fromString(rep, Qt::ISODate);

    auto optStr = [&](const QString& col) -> std::optional<QString> {
        const QVariant v = q.value(col);
        if (v.isNull()) return std::nullopt;
        return v.toString();
    };

    ev.crimeType          = optStr(QStringLiteral("crime_type")).value_or(QString{});
    ev.crimeSubtype       = optStr(QStringLiteral("crime_subtype"));
    ev.locationRaw        = optStr(QStringLiteral("location_raw"));
    ev.addressNormalised  = optStr(QStringLiteral("address_normalised"));
    ev.lga                = optStr(QStringLiteral("lga"));
    ev.suburb             = optStr(QStringLiteral("suburb")).value_or(QString{});
    ev.narrative          = optStr(QStringLiteral("narrative"));
    ev.outcome            = optStr(QStringLiteral("outcome")).value_or(QString{});
    ev.weapon             = optStr(QStringLiteral("weapon"));

    const QVariant latV = q.value(QStringLiteral("lat"));
    if (!latV.isNull()) ev.lat = latV.toDouble();
    const QVariant lonV = q.value(QStringLiteral("lon"));
    if (!lonV.isNull()) ev.lon = lonV.toDouble();

    const QVariant convV = q.value(QStringLiteral("conviction"));
    if (!convV.isNull()) ev.conviction = convV.toInt() != 0;

    const QVariant suspV = q.value(QStringLiteral("suspect_count"));
    if (!suspV.isNull()) ev.suspectCount = suspV.toInt();
    const QVariant victV = q.value(QStringLiteral("victim_count"));
    if (!victV.isNull()) ev.victimCount = victV.toInt();

    ev.meta = strToJsonObj(q.value(QStringLiteral("meta")).toString());

    const QVariant qualityV = q.value(QStringLiteral("quality_score"));
    ev.qualityScore = qualityV.isNull() ? 0.5 : qualityV.toDouble();

    // Populate flat UI convenience fields
    ev.id                  = ev.eventId;
    ev.timestamp           = ev.occurredAt.value_or(ev.ingestedAt);
    ev.locationDescription = ev.locationRaw.value_or(ev.addressNormalised.value_or(QString{}));
    ev.latitude            = ev.lat.value_or(0.0);
    ev.longitude           = ev.lon.value_or(0.0);

    return ev;
}

QVector<QPair<QDate,int>> Database::getDailyTrend(int days) const
{
    QVector<QPair<QDate,int>> result;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT date(occurred_at) AS day, COUNT(*) AS cnt "
        "FROM events "
        "WHERE occurred_at >= :since "
        "GROUP BY day ORDER BY day"));
    q.bindValue(":since",
        QDateTime::currentDateTimeUtc().addDays(-days).toString(Qt::ISODate));
    if (q.exec()) {
        while (q.next()) {
            QDate d = QDate::fromString(q.value(0).toString(), "yyyy-MM-dd");
            if (d.isValid())
                result.append({d, q.value(1).toInt()});
        }
    }
    return result;
}

double Database::getAverageQualityScore() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT AVG(quality_score) FROM events")) && q.next())
        return q.value(0).toDouble();
    return 0.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// queryEvents overload: QDate range + keyword search
// ─────────────────────────────────────────────────────────────────────────────

QVector<CrimeEvent> Database::queryEvents(const QString& crimeType,
                                           const QDate& from,
                                           const QDate& to,
                                           const QString& search,
                                           int limit) const
{
    QString sql = QStringLiteral("SELECT * FROM events WHERE 1=1");
    QVector<std::pair<QString,QVariant>> bindings;

    if (!crimeType.isEmpty()) {
        sql += QStringLiteral(" AND LOWER(crime_type) = LOWER(:crime_type)");
        bindings.append({QStringLiteral(":crime_type"), crimeType});
    }
    if (from.isValid()) {
        sql += QStringLiteral(" AND occurred_at >= :from");
        bindings.append({QStringLiteral(":from"), from.toString(Qt::ISODate)});
    }
    if (to.isValid()) {
        sql += QStringLiteral(" AND occurred_at <= :to");
        bindings.append({QStringLiteral(":to"), to.addDays(1).toString(Qt::ISODate)});
    }
    if (!search.isEmpty()) {
        sql += QStringLiteral(
            " AND (event_id LIKE :kw OR crime_type LIKE :kw2 "
            "OR location_raw LIKE :kw3 OR narrative LIKE :kw4)");
        const QString kw = QLatin1Char('%') + search + QLatin1Char('%');
        bindings.append({QStringLiteral(":kw"),  kw});
        bindings.append({QStringLiteral(":kw2"), kw});
        bindings.append({QStringLiteral(":kw3"), kw});
        bindings.append({QStringLiteral(":kw4"), kw});
    }
    sql += QStringLiteral(" ORDER BY occurred_at DESC LIMIT :limit");

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& [name, val] : bindings)
        q.bindValue(name, val);
    q.bindValue(QStringLiteral(":limit"), limit);

    QVector<CrimeEvent> results;
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qWarning() << "[Database] queryEvents(QDate) failed:" << m_lastError;
        return results;
    }
    while (q.next())
        results.append(rowToEvent(q));
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// rowToLead
// ─────────────────────────────────────────────────────────────────────────────

InvestigativeLead Database::rowToLead(const QSqlQuery& q)
{
    InvestigativeLead lead;
    lead.rank             = q.value(QStringLiteral("rank")).toInt();
    lead.category         = q.value(QStringLiteral("category")).toString();
    lead.headline         = q.value(QStringLiteral("headline")).toString();
    lead.detail           = q.value(QStringLiteral("detail")).toString();
    lead.confidence       = q.value(QStringLiteral("confidence")).toDouble();
    lead.confidenceMethod = q.value(QStringLiteral("confidence_method")).toString();
    lead.supportingData   = strToJsonObj(q.value(QStringLiteral("supporting_data")).toString());
    lead.contradictions   = jsonToStrVec(q.value(QStringLiteral("contradictions")).toString());
    lead.provenance       = jsonToStrVec(q.value(QStringLiteral("provenance")).toString());
    lead.generatedAt      = QDateTime::fromString(
                                q.value(QStringLiteral("generated_at")).toString(),
                                Qt::ISODate);
    return lead;
}
