#include "ui/CoOffendingGraphWidget.h"

#include "ingest/IngestEnricher.h"

#include "core/CrimeEvent.h"



#include <QPainter>

#include <QVBoxLayout>

#include <QHBoxLayout>

#include <QPushButton>

#include <QFile>

#include <QFileInfo>

#include <QFileDialog>

#include <QMouseEvent>

#include <QWheelEvent>

#include <QToolTip>

#include <QSet>

#include <QTextStream>

#include <QStringConverter>

#include <QtMath>

#include <algorithm>



namespace {



constexpr int kMaxDrawNodes = 80;

constexpr double kZoomFactor = 0.85;

constexpr double kMinZoom = 0.15;

constexpr double kMaxZoom = 8.0;



QString personIdFromMeta(const QJsonObject& meta)

{

    static const QStringList keys = {

        QStringLiteral("person_id"),

        QStringLiteral("personId"),

        QStringLiteral("suspect_id"),

        QStringLiteral("offender_id"),

    };

    for (const QString& key : keys) {

        const QString val = meta.value(key).toString().trimmed();

        if (!val.isEmpty())

            return val;

    }

    return {};

}



QString roleFromMeta(const QJsonObject& meta)

{

    QString role = meta.value(QStringLiteral("role")).toString().trimmed();

    if (role.isEmpty())

        role = meta.value(QStringLiteral("suspect_role")).toString().trimmed();

    if (role.isEmpty())

        role = QStringLiteral("participant");

    return role;

}



} // namespace



// ─────────────────────────────────────────────────────────────────────────────

CoOffendingGraphWidget::CoOffendingGraphWidget(std::shared_ptr<Database> db, QWidget* parent)

    : QWidget(parent)

    , m_db(std::move(db))

    , m_statusLabel(nullptr)

    , m_loadCsvBtn(nullptr)

    , m_loadDbBtn(nullptr)

{
    setupUI();

    m_csvPath = defaultCsvPath();

    refresh();

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::setupUI()

{

    setMinimumHeight(400);

    setMouseTracking(true);

    setStyleSheet(QStringLiteral("background-color: #0d1117;"));

    setCursor(Qt::OpenHandCursor);



    auto* layout = new QVBoxLayout(this);

    layout->setContentsMargins(16, 16, 16, 8);

    layout->setSpacing(8);



    auto* headerRow = new QHBoxLayout();

    auto* titleLbl = new QLabel(QStringLiteral("Co-Offending Network"), this);

    titleLbl->setStyleSheet(QStringLiteral("color: #eaeaea; font-size: 22px; font-weight: bold;"));

    headerRow->addWidget(titleLbl);

    headerRow->addStretch();



    m_loadCsvBtn = new QPushButton(QStringLiteral("Load CSV"), this);

    m_loadCsvBtn->setObjectName(QStringLiteral("networkLoadCsvBtn"));

    m_loadCsvBtn->setStyleSheet(

        QStringLiteral("QPushButton { background-color: #0f3460; color: #eaeaea; border: 1px solid #1a4a80; "

                       "border-radius: 4px; padding: 6px 14px; font-size: 12px; }"

                       "QPushButton:hover { background-color: #1a4a80; }"));

    headerRow->addWidget(m_loadCsvBtn);



    m_loadDbBtn = new QPushButton(QStringLiteral("Load from DB"), this);

    m_loadDbBtn->setObjectName(QStringLiteral("networkLoadDbBtn"));

    m_loadDbBtn->setStyleSheet(

        QStringLiteral("QPushButton { background-color: #0f3460; color: #eaeaea; border: 1px solid #1a4a80; "

                       "border-radius: 4px; padding: 6px 14px; font-size: 12px; }"

                       "QPushButton:hover { background-color: #1a4a80; }"));

    headerRow->addWidget(m_loadDbBtn);

    layout->addLayout(headerRow);



    m_statusLabel = new QLabel(this);

    m_statusLabel->setObjectName(QStringLiteral("networkStatusLabel"));

    m_statusLabel->setStyleSheet(QStringLiteral("color: #4a5568; font-size: 12px;"));

    layout->addWidget(m_statusLabel);



    layout->addStretch();



    connect(m_loadCsvBtn, &QPushButton::clicked, this, &CoOffendingGraphWidget::onLoadCsvClicked);

    connect(m_loadDbBtn, &QPushButton::clicked, this, &CoOffendingGraphWidget::onLoadDbClicked);

}



// ─────────────────────────────────────────────────────────────────────────────

QString CoOffendingGraphWidget::defaultCsvPath()

{

    return IngestEnricher::bundledDataDir()

        + QStringLiteral("/co_offending/chicago_co_offending.csv");

}



// ─────────────────────────────────────────────────────────────────────────────

QVector<PersonIncidentRecord> CoOffendingGraphWidget::parseCsv(const QString& path)

{

    QVector<PersonIncidentRecord> out;

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))

        return out;



    QTextStream stream(&file);

    stream.setEncoding(QStringConverter::Utf8);

    const QString header = stream.readLine();

    const QStringList cols = header.split(QLatin1Char(','));

    const int personCol = cols.indexOf(QStringLiteral("person_id"));

    const int incidentCol = cols.indexOf(QStringLiteral("incident_id"));

    const int roleCol = cols.indexOf(QStringLiteral("role"));

    if (personCol < 0 || incidentCol < 0)

        return out;



    while (!stream.atEnd()) {

        const QString line = stream.readLine().trimmed();

        if (line.isEmpty())

            continue;

        const QStringList parts = line.split(QLatin1Char(','));

        if (parts.size() <= qMax(personCol, incidentCol))

            continue;



        PersonIncidentRecord rec;

        rec.personId   = parts[personCol].trimmed();

        rec.incidentId = parts[incidentCol].trimmed();

        if (roleCol >= 0 && parts.size() > roleCol) {

            rec.role = parts[roleCol].trimmed();

            rec.roleWeight = (rec.role == QStringLiteral("suspect")) ? 1.0 : 0.5;

        } else {

            rec.roleWeight = 1.0;

        }

        out.append(rec);

    }

    return out;

}



int CoOffendingGraphWidget::countPersonLinkedEvents(const QVector<CrimeEvent>& events)

{

    int count = 0;

    for (const CrimeEvent& ev : events) {

        if (!personIdFromMeta(ev.meta).isEmpty())

            ++count;

    }

    return count;

}



QVector<PersonIncidentRecord> CoOffendingGraphWidget::recordsFromEvents(

    const QVector<CrimeEvent>& events)

{

    QVector<PersonIncidentRecord> out;

    out.reserve(events.size());

    for (const CrimeEvent& ev : events) {

        const QString personId = personIdFromMeta(ev.meta);

        if (personId.isEmpty())

            continue;



        PersonIncidentRecord rec;

        rec.personId   = personId;

        rec.incidentId = ev.eventId;

        rec.role       = roleFromMeta(ev.meta);

        rec.roleWeight = (rec.role == QStringLiteral("suspect")) ? 1.0 : 0.5;

        out.append(rec);

    }

    return out;

}



void CoOffendingGraphWidget::applyPersonIncidentRecords(

    const QVector<PersonIncidentRecord>& records,

    const QString& statusLabel)

{

    m_analyser = CoOffendingAnalyser{};

    m_nodes.clear();

    m_nodePositions.clear();

    m_edgeCount = 0;

    resetView();



    if (records.isEmpty()) {

        m_statusLabel->setText(

            QStringLiteral("No co-offending data loaded (%1)").arg(statusLabel));

        update();

        return;

    }



    m_analyser.buildGraph(records);

    m_analyser.analyse();

    m_nodes = m_analyser.nodes();



    for (const NetworkNode& node : m_nodes) {

        for (auto it = node.neighbours.constBegin(); it != node.neighbours.constEnd(); ++it) {

            if (node.personId < it.key())

                ++m_edgeCount;

        }

    }



    layoutNodes();

    m_statusLabel->setText(

        QStringLiteral("%1 nodes · %2 edges · %3")

            .arg(m_nodes.size())

            .arg(m_edgeCount)

            .arg(statusLabel));

    update();

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::resetView()

{

    m_zoom = 1.0;

    m_panOffset = QPointF(0.0, 0.0);

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::onLoadCsvClicked()

{

    const QString path = QFileDialog::getOpenFileName(

        this,

        QStringLiteral("Load Co-Offending CSV"),

        QString(),

        QStringLiteral("CSV Files (*.csv);;All Files (*)"));



    if (path.isEmpty())

        return;



    loadFromCsv(path);

}



void CoOffendingGraphWidget::onLoadDbClicked()

{

    loadFromDatabase();

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::loadFromCsv(const QString& path)

{

    m_csvPath = path;

    applyPersonIncidentRecords(parseCsv(path), QFileInfo(path).fileName());

}



void CoOffendingGraphWidget::loadFromDatabase()

{

    if (!m_db) {

        m_statusLabel->setText(QStringLiteral("No database connected"));

        update();

        return;

    }



    const QVector<CrimeEvent> events = m_db->getRecentEvents(5000);

    const QVector<PersonIncidentRecord> records = recordsFromEvents(events);

    applyPersonIncidentRecords(records, QStringLiteral("SQLite events"));

}



// ─────────────────────────────────────────────────────────────────────────────

QRectF CoOffendingGraphWidget::graphArea() const

{

    const int top = m_statusLabel ? m_statusLabel->geometry().bottom() + 12 : 100;

    return QRectF(40.0, static_cast<double>(top), width() - 80.0, height() - top - 40.0);

}



// ─────────────────────────────────────────────────────────────────────────────

QPointF CoOffendingGraphWidget::graphCentre() const

{

    return graphArea().center();

}



// ─────────────────────────────────────────────────────────────────────────────

QPointF CoOffendingGraphWidget::toScreen(const QPointF& base) const

{

    const QPointF centre = graphCentre();

    return (base - centre) * m_zoom + centre + m_panOffset;

}



// ─────────────────────────────────────────────────────────────────────────────

const NetworkNode* CoOffendingGraphWidget::findNetworkNode(const QString& personId) const

{

    for (const NetworkNode& node : m_nodes) {

        if (node.personId == personId)

            return &node;

    }

    return nullptr;

}



// ─────────────────────────────────────────────────────────────────────────────

double CoOffendingGraphWidget::nodeRadiusFor(const QString& personId) const

{

    double pr = 0.0;

    if (const NetworkNode* node = findNetworkNode(personId))

        pr = node->pageRank;

    return 5.0 + qBound(0.0, pr * 200.0, 8.0);

}



// ─────────────────────────────────────────────────────────────────────────────

QString CoOffendingGraphWidget::findNodeAt(const QPoint& screenPos) const

{

    const QPointF pos(screenPos);

    QString hitId;

    double bestDist = -1.0;

    for (auto it = m_nodePositions.constBegin(); it != m_nodePositions.constEnd(); ++it) {

        const QPointF centre = toScreen(it.value());

        const double radius = nodeRadiusFor(it.key()) + 2.0;

        const QPointF diff = centre - pos;

        const double dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());

        if (dist <= radius && (bestDist < 0.0 || dist < bestDist)) {

            hitId = it.key();

            bestDist = dist;

        }

    }

    return hitId;

}



// ─────────────────────────────────────────────────────────────────────────────

QString CoOffendingGraphWidget::nodeTooltipHtml(const QString& personId) const

{

    const NetworkNode* node = findNetworkNode(personId);

    if (!node)

        return QStringLiteral("<b>Person:</b> %1").arg(personId);



    QStringList incidents = node->incidentIds;

    QString incidentText;

    if (incidents.isEmpty()) {

        incidentText = QStringLiteral("(none)");

    } else if (incidents.size() <= 6) {

        incidentText = incidents.join(QStringLiteral(", "));

    } else {

        incidentText = incidents.mid(0, 6).join(QStringLiteral(", "))

            + QStringLiteral(" … +%1 more").arg(incidents.size() - 6);

    }



    return QStringLiteral("<b>Person:</b> %1<br><b>Incidents:</b> %2")

        .arg(personId, incidentText);

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::layoutNodes()

{

    m_nodePositions.clear();

    const int n = qMin(m_nodes.size(), kMaxDrawNodes);

    if (n == 0)

        return;



    const QRectF area = graphArea();

    const QPointF centre = area.center();

    const double radius = qMin(area.width(), area.height()) * 0.38;



    for (int i = 0; i < n; ++i) {

        const double angle = (2.0 * M_PI * i) / n - M_PI / 2.0;

        m_nodePositions[m_nodes[i].personId] = QPointF(

            centre.x() + radius * qCos(angle),

            centre.y() + radius * qSin(angle));

    }

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::resizeEvent(QResizeEvent* event)

{

    QWidget::resizeEvent(event);

    layoutNodes();

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::mousePressEvent(QMouseEvent* event)

{

    if (event->button() == Qt::LeftButton) {

        m_dragging = true;

        m_dragMoved = false;

        m_lastMousePos = event->pos();

        setCursor(Qt::ClosedHandCursor);

    }

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::mouseReleaseEvent(QMouseEvent* event)

{

    if (event->button() == Qt::LeftButton && m_dragging && !m_dragMoved) {

        const QString personId = findNodeAt(event->pos());

        if (!personId.isEmpty()) {

            QToolTip::showText(

                event->globalPosition().toPoint(),

                nodeTooltipHtml(personId),

                this);

        }

    }



    m_dragging = false;

    m_dragMoved = false;

    setCursor(Qt::OpenHandCursor);

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::mouseMoveEvent(QMouseEvent* event)

{

    if (m_dragging) {

        const QPoint delta = event->pos() - m_lastMousePos;

        if (!delta.isNull())

            m_dragMoved = true;

        m_panOffset += delta;

        m_lastMousePos = event->pos();

        update();

    }

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::wheelEvent(QWheelEvent* event)

{

    const double delta = event->angleDelta().y();

    if (delta > 0)

        m_zoom /= kZoomFactor;

    else if (delta < 0)

        m_zoom *= kZoomFactor;



    m_zoom = std::clamp(m_zoom, kMinZoom, kMaxZoom);

    update();

    event->accept();

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::paintEvent(QPaintEvent* event)

{

    QWidget::paintEvent(event);



    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);



    painter.fillRect(rect(), QColor(QStringLiteral("#0d1117")));



    if (m_nodePositions.isEmpty()) {
        if (!m_nodes.isEmpty())
            layoutNodes();
        if (m_nodePositions.isEmpty())
            return;
    }

    painter.setPen(QPen(QColor(QStringLiteral("#1a2a4a")), 1.5));

    int edgesDrawn = 0;

    for (const NetworkNode& node : m_nodes) {

        if (!m_nodePositions.contains(node.personId))

            continue;

        const QPointF from = toScreen(m_nodePositions[node.personId]);

        for (auto it = node.neighbours.constBegin(); it != node.neighbours.constEnd(); ++it) {

            if (!m_nodePositions.contains(it.key()))

                continue;

            if (node.personId >= it.key())

                continue;

            const QPointF to = toScreen(m_nodePositions[it.key()]);

            const double weight = qBound(0.2, it.value(), 3.0);

            painter.setOpacity(qBound(0.25, 0.35 + weight * 0.15, 0.9));

            painter.drawLine(from, to);

            ++edgesDrawn;

        }

    }

    painter.setOpacity(1.0);



    for (auto it = m_nodePositions.constBegin(); it != m_nodePositions.constEnd(); ++it) {

        const QString& id = it.key();

        double pr = 0.0;

        if (const NetworkNode* node = findNetworkNode(id))

            pr = node->pageRank;



        const double r = nodeRadiusFor(id);

        const QColor fill = pr > 0.01 ? QColor(QStringLiteral("#e94560"))

                                      : QColor(QStringLiteral("#4fc3f7"));

        const QPointF centre = toScreen(it.value());

        painter.setBrush(fill);

        painter.setPen(QPen(QColor(QStringLiteral("#16213e")), 1.5));

        painter.drawEllipse(centre, r, r);

    }



    painter.setPen(QColor(QStringLiteral("#4a5568")));

    painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));

    painter.drawText(16, height() - 12,

        QStringLiteral("Scroll to zoom · drag to pan · click node for details · %1 nodes · %2 edges")

            .arg(m_nodePositions.size())

            .arg(edgesDrawn));

}



// ─────────────────────────────────────────────────────────────────────────────

void CoOffendingGraphWidget::refresh()

{

    if (m_db) {

        const QVector<CrimeEvent> events = m_db->getRecentEvents(5000);

        if (countPersonLinkedEvents(events) >= 2) {

            loadFromDatabase();

            return;

        }

    }

    loadFromCsv(defaultCsvPath());

}

