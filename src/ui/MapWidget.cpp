#include "ui/MapWidget.h"

#include <QPaintEvent>
#include <QResizeEvent>
#include <QFontMetrics>
#include <QApplication>
#include <cmath>
#include <algorithm>

static constexpr double ZOOM_FACTOR  = 0.85;
static constexpr int    DOT_RADIUS   = 8;
static constexpr int    COMPASS_SIZE = 40;
static constexpr int    SCALE_WIDTH  = 100;

// ─────────────────────────────────────────────────────────────────────────────
MapWidget::MapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(400, 300);
    setStyleSheet("background-color: #0d1117;");
    setCursor(Qt::CrossCursor);

    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setStyleSheet(
        "QLabel { color: #eaeaea; background: rgba(13,17,23,200); "
        "padding: 2px 8px; border-radius: 4px; font-size: 11px; font-weight: bold; }");
    m_zoomLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_zoomLabel->move(8, 8);
    updateZoomLabel();
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::setEvents(const QVector<CrimeEvent>& events)
{
    m_events = events;
    for (CrimeEvent& ev : m_events) {
        if (ev.lat.has_value() && ev.lon.has_value()) {
            ev.latitude  = ev.lat.value();
            ev.longitude = ev.lon.value();
        } else if (ev.latitude != 0.0 || ev.longitude != 0.0) {
            ev.lat = ev.latitude;
            ev.lon = ev.longitude;
        }
    }
    update();
}

void MapWidget::setRiskSurface(const GeographicProfile& profile)
{
    m_riskProfile = profile;
    update();
}

void MapWidget::clearRiskSurface()
{
    m_riskProfile.reset();
    update();
}

void MapWidget::setKDEHotspots(const QVector<HotspotRegion>& hotspots)
{
    m_kdeHotspots = hotspots;
    update();
}

void MapWidget::clearKDEHotspots()
{
    m_kdeHotspots.clear();
    update();
}

void MapWidget::setHotspots(const QVector<HotspotRegion>& hotspots)
{
    setKDEHotspots(hotspots);
}

void MapWidget::clearOverlays()
{
    m_events.clear();
    m_riskProfile.reset();
    m_kdeHotspots.clear();
    update();
}

int MapWidget::zoomLevelInt() const
{
    // At m_degPerPixel = 0.0001 → zoom level 14 (approx. street level)
    const double z = std::log2(0.0001 / m_degPerPixel) + 14.0;
    return static_cast<int>(std::clamp(std::round(z), 1.0, 20.0));
}

void MapWidget::updateZoomLabel()
{
    if (m_zoomLabel)
        m_zoomLabel->setText(QString("Zoom: %1").arg(zoomLevelInt()));
    if (m_zoomLabel)
        m_zoomLabel->adjustSize();
}

void MapWidget::setCenter(double lat, double lon)
{
    m_centerLat = lat;
    m_centerLon = lon;
    update();
}

void MapWidget::setZoom(double degreesPerPixel)
{
    m_degPerPixel = std::clamp(degreesPerPixel, 1e-6, 1.0);
    updateZoomLabel();
    emit zoomChanged(zoomLevelInt());
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
QPointF MapWidget::latLonToPixel(double lat, double lon) const
{
    const double cx = width()  / 2.0;
    const double cy = height() / 2.0;
    const double px = cx + (lon - m_centerLon) / m_degPerPixel;
    const double py = cy - (lat - m_centerLat) / m_degPerPixel;
    return { px, py };
}

QPair<double, double> MapWidget::pixelToLatLon(const QPoint& px) const
{
    const double cx = width()  / 2.0;
    const double cy = height() / 2.0;
    const double lon = m_centerLon + (px.x() - cx) * m_degPerPixel;
    const double lat = m_centerLat - (px.y() - cy) * m_degPerPixel;
    return { lat, lon };
}

// ─────────────────────────────────────────────────────────────────────────────
QColor MapWidget::crimeTypeColor(const QString& crimeType) const
{
    const QString lower = crimeType.toLower();
    if (lower.contains("assault"))          return QColor("#e53935");  // red
    if (lower.contains("burglary"))         return QColor("#fb8c00");  // orange
    if (lower.contains("theft"))            return QColor("#fdd835");  // yellow
    if (lower.contains("vehicle"))          return QColor("#00bcd4");  // cyan
    if (lower.contains("drug"))             return QColor("#9c27b0");  // purple
    if (lower.contains("robbery"))          return QColor("#c62828");  // crimson
    if (lower.contains("criminal_damage")
     || lower.contains("damage"))           return QColor("#8bc34a");  // lime
    if (lower.contains("antisocial")
     || lower.contains("anti-social"))      return QColor("#78909c");  // grey
    return QColor("#eaeaea");                                           // default white
}

QColor MapWidget::riskColor(double probability) const
{
    // Lerp: 0=blue → 0.5=yellow → 1=red, alpha=160
    probability = std::clamp(probability, 0.0, 1.0);
    int r, g, b;
    if (probability < 0.5) {
        const double t = probability * 2.0;
        r = static_cast<int>(0   + t * 255);
        g = static_cast<int>(0   + t * 255);
        b = static_cast<int>(255 - t * 255);
    } else {
        const double t = (probability - 0.5) * 2.0;
        r = 255;
        g = static_cast<int>(255 - t * 255);
        b = 0;
    }
    return QColor(r, g, b, 160);
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor("#0d1117"));

    drawRiskHeatmap(p);
    drawGrid(p);
    drawKDEHotspots(p);
    drawEvents(p);
    drawScale(p);
    drawCompass(p);
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::drawGrid(QPainter& p)
{
    // Adaptive grid step: aim for ~5 grid lines across the viewport
    const double visibleDeg = width() * m_degPerPixel;
    const double rawStep    = visibleDeg / 5.0;

    // Round step to a nice number: 0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0 …
    static const double niceSteps[] = {0.001,0.002,0.005,0.01,0.02,0.05,0.1,0.2,0.5,1.0,2.0,5.0};
    double gridStep = niceSteps[0];
    for (double s : niceSteps) {
        gridStep = s;
        if (s >= rawStep) break;
    }

    p.setPen(QPen(QColor(15, 52, 96, 120), 1, Qt::DotLine));

    QFont labelFont = p.font();
    labelFont.setPointSize(8);
    p.setFont(labelFont);
    p.setPen(QColor(74, 85, 104, 200));

    const double lonMin = m_centerLon - (width()  / 2.0) * m_degPerPixel;
    const double lonMax = m_centerLon + (width()  / 2.0) * m_degPerPixel;
    const double latMin = m_centerLat - (height() / 2.0) * m_degPerPixel;
    const double latMax = m_centerLat + (height() / 2.0) * m_degPerPixel;

    // Vertical lines (constant lon)
    double lon0 = std::floor(lonMin / gridStep) * gridStep;
    for (double lon = lon0; lon <= lonMax; lon += gridStep) {
        const QPointF top = latLonToPixel(latMax, lon);
        const QPointF bot = latLonToPixel(latMin, lon);
        p.setPen(QPen(QColor(15, 52, 96, 100), 1, Qt::DotLine));
        p.drawLine(top, bot);
        p.setPen(QColor(74, 85, 104, 160));
        p.drawText(QPointF(top.x() + 3, 14), QString::number(lon, 'f', 4) + "°");
    }

    // Horizontal lines (constant lat)
    double lat0 = std::floor(latMin / gridStep) * gridStep;
    for (double lat = lat0; lat <= latMax; lat += gridStep) {
        const QPointF left  = latLonToPixel(lat, lonMin);
        const QPointF right = latLonToPixel(lat, lonMax);
        p.setPen(QPen(QColor(15, 52, 96, 100), 1, Qt::DotLine));
        p.drawLine(left, right);
        p.setPen(QColor(74, 85, 104, 160));
        p.drawText(QPointF(4, left.y() - 3), QString::number(lat, 'f', 4) + "°");
    }

    // Crosshair at center
    p.setPen(QPen(QColor("#e94560"), 1, Qt::DashLine));
    p.drawLine(width() / 2 - 20, height() / 2, width() / 2 + 20, height() / 2);
    p.drawLine(width() / 2, height() / 2 - 20, width() / 2, height() / 2 + 20);
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::drawEvents(QPainter& p)
{
    p.setRenderHint(QPainter::Antialiasing, true);

    for (const CrimeEvent& ev : m_events) {
        if (ev.latitude == 0.0 && ev.longitude == 0.0) continue;

        const QPointF pos = latLonToPixel(ev.latitude, ev.longitude);

        // Skip off-screen events
        if (pos.x() < -DOT_RADIUS || pos.x() > width()  + DOT_RADIUS) continue;
        if (pos.y() < -DOT_RADIUS || pos.y() > height() + DOT_RADIUS) continue;

        const QColor col = crimeTypeColor(ev.crimeType);

        // Outer glow
        QRadialGradient glow(pos, DOT_RADIUS * 2);
        glow.setColorAt(0.0, col.lighter(150));
        glow.setColorAt(1.0, Qt::transparent);
        p.setBrush(glow);
        p.setPen(Qt::NoPen);
        p.drawEllipse(pos, DOT_RADIUS * 2, DOT_RADIUS * 2);

        // Main dot
        p.setBrush(col);
        p.setPen(QPen(col.darker(140), 1.5));
        p.drawEllipse(pos, DOT_RADIUS, DOT_RADIUS);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::drawRiskHeatmap(QPainter& p)
{
    if (!m_riskProfile.has_value()) return;

    const GeographicProfile& prof = *m_riskProfile;

    // Render the probability surface grid
    const int nLat = static_cast<int>(prof.gridLats.size());
    const int nLon = static_cast<int>(prof.gridLons.size());
    if (nLat < 2 || nLon < 2) return;

    const double dLat = (prof.gridLats.back() - prof.gridLats.front()) / std::max(nLat - 1, 1);
    const double dLon = (prof.gridLons.back() - prof.gridLons.front()) / std::max(nLon - 1, 1);
    const double halfLat = dLat / 2.0;
    const double halfLon = dLon / 2.0;

    for (int i = 0; i < nLat; ++i) {
        for (int j = 0; j < nLon; ++j) {
            if (static_cast<int>(prof.probabilitySurface.size()) <= i) continue;
            if (static_cast<int>(prof.probabilitySurface[i].size()) <= j) continue;
            const double prob = prof.probabilitySurface[i][j];
            if (prob < 1e-6) continue;

            const double lat = prof.gridLats[i];
            const double lon = prof.gridLons[j];

            const QPointF topLeft  = latLonToPixel(lat + halfLat, lon - halfLon);
            const QPointF botRight = latLonToPixel(lat - halfLat, lon + halfLon);
            const QRectF cellRect(topLeft, botRight);
            if (!cellRect.intersects(QRectF(rect()))) continue;
            p.fillRect(cellRect, riskColor(prob));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::drawKDEHotspots(QPainter& p)
{
    if (m_kdeHotspots.isEmpty()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // Rank 1 = highest density; colour by rank
    auto hotspotColor = [](int rank, double density, double maxDensity) -> QColor {
        const double norm = (maxDensity > 0) ? density / maxDensity : 0.0;
        if (rank == 1 || norm > 0.8) return QColor(233, 69, 96, 180);   // crimson
        if (rank <= 3 || norm > 0.5) return QColor(255, 152, 0,  160);  // orange
        return QColor(255, 235, 59, 120);                                 // yellow
    };

    const double maxDensity = m_kdeHotspots.isEmpty() ? 1.0
        : m_kdeHotspots.first().peakDensity;

    for (int i = 0; i < m_kdeHotspots.size(); ++i) {
        const auto& hs = m_kdeHotspots[i];

        // Radius in pixels: approximate from bounding box half-width in degrees
        const double halfLatDeg = (hs.latMax - hs.latMin) / 2.0;
        const double halfLonDeg = (hs.lonMax - hs.lonMin) / 2.0;
        const double halfDeg    = std::max(halfLatDeg, halfLonDeg);
        const double radiusPx   = std::max(halfDeg / m_degPerPixel, 5.0);
        if (radiusPx < 2.0) continue;

        const QPointF centre = latLonToPixel(hs.centroidLat, hs.centroidLon);
        const QColor  col    = hotspotColor(hs.rank, hs.peakDensity, maxDensity);

        // Filled semi-transparent circle
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(centre, radiusPx, radiusPx);

        // Border ring
        QColor borderCol = col;
        borderCol.setAlpha(230);
        p.setPen(QPen(borderCol, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(centre, radiusPx, radiusPx);

        // Rank label inside circle
        if (radiusPx > 14) {
            p.setPen(QColor(255, 255, 255, 210));
            QFont f; f.setPixelSize(11); f.setBold(true);
            p.setFont(f);
            const QRectF labelRect(centre.x() - radiusPx,
                                   centre.y() - radiusPx,
                                   radiusPx * 2, radiusPx * 2);
            p.drawText(labelRect, Qt::AlignCenter,
                       QString("#%1").arg(hs.rank));
        }
    }
    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::drawScale(QPainter& p)
{
    // 1 degree of latitude ≈ 111.32 km
    const double kmPerDeg  = 111.32;
    const double kmPerPixel = m_degPerPixel * kmPerDeg;

    // Target scale bar: nearest nice km value for SCALE_WIDTH pixels
    double scaleKm = SCALE_WIDTH * kmPerPixel;
    // Round to nearest nice number (guard log10 for tiny scaleKm)
    if (scaleKm <= 0.0)
        scaleKm = kmPerPixel;
    const double rawExp  = std::floor(std::log10(scaleKm));
    const double factor  = std::pow(10.0, rawExp);
    scaleKm = std::round(scaleKm / factor) * factor;
    if (scaleKm <= 0.0)
        scaleKm = factor;
    const int scalePixels = std::max(1, static_cast<int>(scaleKm / kmPerPixel));

    const int x0 = 20;
    const int y0 = height() - 25;

    p.setPen(QPen(QColor("#eaeaea"), 2));
    p.drawLine(x0, y0, x0 + scalePixels, y0);
    p.drawLine(x0, y0 - 5, x0, y0 + 5);
    p.drawLine(x0 + scalePixels, y0 - 5, x0 + scalePixels, y0 + 5);

    QFont f = p.font();
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(QColor("#eaeaea"));

    const QString label = scaleKm < 1.0
        ? QString::number(scaleKm * 1000, 'f', 0) + " m"
        : QString::number(scaleKm, 'f', scaleKm < 10 ? 1 : 0) + " km";
    p.drawText(QPointF(x0 + scalePixels / 2.0 - 20, y0 - 8), label);
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::drawCompass(QPainter& p)
{
    const int cx = width()  - COMPASS_SIZE - 16;
    const int cy = COMPASS_SIZE + 16;

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    // Background circle
    p.setBrush(QColor(22, 33, 62, 200));
    p.setPen(QPen(QColor("#0f3460"), 1));
    p.drawEllipse(QPoint(cx, cy), COMPASS_SIZE, COMPASS_SIZE);

    // North arrow (red)
    QPolygon northArrow;
    northArrow << QPoint(cx, cy - COMPASS_SIZE + 6)
               << QPoint(cx - 7, cy + 4)
               << QPoint(cx + 7, cy + 4);
    p.setBrush(QColor("#e94560"));
    p.setPen(Qt::NoPen);
    p.drawPolygon(northArrow);

    // South arrow (grey)
    QPolygon southArrow;
    southArrow << QPoint(cx, cy + COMPASS_SIZE - 6)
               << QPoint(cx - 7, cy - 4)
               << QPoint(cx + 7, cy - 4);
    p.setBrush(QColor("#4a5568"));
    p.drawPolygon(southArrow);

    // "N" label
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(10);
    p.setFont(f);
    p.setPen(QColor("#eaeaea"));
    p.drawText(QRect(cx - 8, cy - COMPASS_SIZE - 4, 16, 16), Qt::AlignCenter, "N");

    p.restore();
}

// ─────────────────────────────────────────────────────────────────────────────
void MapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging  = true;
        m_dragMoved = false;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging && !m_dragMoved) {
        const auto [lat, lon] = pixelToLatLon(event->pos());
        emit locationClicked(lat, lon);
        emit regionClicked(lat, lon);
    }
    m_dragging  = false;
    m_dragMoved = false;
    setCursor(Qt::CrossCursor);
}

void MapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        const QPoint delta = event->pos() - m_lastMousePos;
        if (!delta.isNull())
            m_dragMoved = true;
        m_centerLon -= delta.x() * m_degPerPixel;
        m_centerLat += delta.y() * m_degPerPixel;
        m_lastMousePos = event->pos();
        update();
    } else {
        // Tooltip: show event info on hover
        for (const CrimeEvent& ev : m_events) {
            if (ev.latitude == 0.0 && ev.longitude == 0.0) continue;
            const QPointF pos = latLonToPixel(ev.latitude, ev.longitude);
            const QPointF diff = pos - QPointF(event->pos());
            if (std::sqrt(diff.x()*diff.x() + diff.y()*diff.y()) <= DOT_RADIUS + 2) {
                QToolTip::showText(event->globalPosition().toPoint(),
                    QString("<b>%1</b><br>%2<br>%3, %4<br>Quality: %5")
                        .arg(ev.crimeType.isEmpty() ? "Unknown" : ev.crimeType)
                        .arg(ev.timestamp.toString("yyyy-MM-dd"))
                        .arg(ev.latitude, 0, 'f', 5)
                        .arg(ev.longitude, 0, 'f', 5)
                        .arg(ev.qualityScore, 0, 'f', 2),
                    this);
                return;
            }
        }
        QToolTip::hideText();
    }
}

void MapWidget::wheelEvent(QWheelEvent* event)
{
    const double delta = event->angleDelta().y();
    if (delta > 0)
        m_degPerPixel *= ZOOM_FACTOR;       // zoom in
    else
        m_degPerPixel /= ZOOM_FACTOR;       // zoom out

    m_degPerPixel = std::clamp(m_degPerPixel, 1e-6, 1.0);
    updateZoomLabel();
    emit zoomChanged(zoomLevelInt());
    update();
    event->accept();
}

void MapWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_zoomLabel)
        m_zoomLabel->move(8, 8);
}
