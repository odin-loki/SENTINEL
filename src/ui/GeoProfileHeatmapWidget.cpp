#include "ui/GeoProfileHeatmapWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
GeoProfileHeatmapWidget::GeoProfileHeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QStringLiteral("background-color: #0d1117;"));
}

// ─────────────────────────────────────────────────────────────────────────────
void GeoProfileHeatmapWidget::setProfile(const GeographicProfile& profile)
{
    m_profile    = profile;
    m_hasProfile = !profile.gridLats.empty() && !profile.gridLons.empty()
                && profile.probabilitySurface.size() >= 2;
    recomputeBounds();
    update();
}

void GeoProfileHeatmapWidget::setCrimeLocations(QVector<QPair<double, double>> locations)
{
    m_crimeLocations = std::move(locations);
    recomputeBounds();
    update();
}

void GeoProfileHeatmapWidget::clear()
{
    m_profile.reset();
    m_crimeLocations.clear();
    m_hasProfile = false;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
void GeoProfileHeatmapWidget::recomputeBounds()
{
    bool haveBounds = false;
    double latMin = 0.0, latMax = 0.0, lonMin = 0.0, lonMax = 0.0;

    auto extend = [&](double lat, double lon) {
        if (!haveBounds) {
            latMin = latMax = lat;
            lonMin = lonMax = lon;
            haveBounds = true;
        } else {
            latMin = std::min(latMin, lat);
            latMax = std::max(latMax, lat);
            lonMin = std::min(lonMin, lon);
            lonMax = std::max(lonMax, lon);
        }
    };

    if (m_profile.has_value()) {
        for (double lat : m_profile->gridLats)
            for (double lon : m_profile->gridLons)
                extend(lat, lon);
    }

    for (const auto& loc : m_crimeLocations)
        extend(loc.first, loc.second);

    if (!haveBounds) {
        m_latMin = m_latMax = m_lonMin = m_lonMax = 0.0;
        return;
    }

    const double latSpan = std::max(latMax - latMin, 1e-6);
    const double lonSpan = std::max(lonMax - lonMin, 1e-6);
    const double padLat  = latSpan * 0.05;
    const double padLon  = lonSpan * 0.05;

    m_latMin = latMin - padLat;
    m_latMax = latMax + padLat;
    m_lonMin = lonMin - padLon;
    m_lonMax = lonMax + padLon;
}

// ─────────────────────────────────────────────────────────────────────────────
QPointF GeoProfileHeatmapWidget::latLonToPixel(double lat, double lon) const
{
    const double latSpan = std::max(m_latMax - m_latMin, 1e-9);
    const double lonSpan = std::max(m_lonMax - m_lonMin, 1e-9);

    const double drawW = std::max(width()  - 2.0 * MARGIN, 1.0);
    const double drawH = std::max(height() - 2.0 * MARGIN, 1.0);

    const double x = MARGIN + (lon - m_lonMin) / lonSpan * drawW;
    const double y = MARGIN + (m_latMax - lat) / latSpan * drawH;
    return { x, y };
}

QColor GeoProfileHeatmapWidget::riskColor(double probability) const
{
    // Lerp: 0=blue → 0.5=yellow → 1=red, alpha=160 (matches MapWidget)
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
void GeoProfileHeatmapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(QStringLiteral("#0d1117")));

    if (!m_hasProfile || !m_profile.has_value())
        return;

    const GeographicProfile& prof = *m_profile;

    const int nLat = static_cast<int>(prof.gridLats.size());
    const int nLon = static_cast<int>(prof.gridLons.size());
    if (nLat < 2 || nLon < 2)
        return;

    const double dLat = (prof.gridLats.back() - prof.gridLats.front())
                      / std::max(nLat - 1, 1);
    const double dLon = (prof.gridLons.back() - prof.gridLons.front())
                      / std::max(nLon - 1, 1);
    const double halfLat = dLat / 2.0;
    const double halfLon = dLon / 2.0;

    for (int i = 0; i < nLat; ++i) {
        for (int j = 0; j < nLon; ++j) {
            if (static_cast<int>(prof.probabilitySurface.size()) <= i)
                continue;
            if (static_cast<int>(prof.probabilitySurface[i].size()) <= j)
                continue;

            const double prob = prof.probabilitySurface[i][j];
            if (prob < 1e-6)
                continue;

            const double lat = prof.gridLats[i];
            const double lon = prof.gridLons[j];

            const QPointF topLeft  = latLonToPixel(lat + halfLat, lon - halfLon);
            const QPointF botRight = latLonToPixel(lat - halfLat, lon + halfLon);
            const QRectF cellRect(topLeft, botRight);
            if (!cellRect.intersects(QRectF(rect())))
                continue;
            p.fillRect(cellRect, riskColor(prob));
        }
    }

    // Peak anchor marker
    const QPointF peak = latLonToPixel(prof.peakLat, prof.peakLon);
    p.setPen(QPen(QColor(QStringLiteral("#e94560")), 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(peak, 6, 6);
    p.drawLine(QPointF(peak.x() - 8, peak.y()), QPointF(peak.x() + 8, peak.y()));
    p.drawLine(QPointF(peak.x(), peak.y() - 8), QPointF(peak.x(), peak.y() + 8));

    // Crime location dots
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& loc : m_crimeLocations) {
        const QPointF pos = latLonToPixel(loc.first, loc.second);
        if (pos.x() < -DOT_RADIUS || pos.x() > width()  + DOT_RADIUS)
            continue;
        if (pos.y() < -DOT_RADIUS || pos.y() > height() + DOT_RADIUS)
            continue;

        p.setBrush(QColor(QStringLiteral("#eaeaea")));
        p.setPen(QPen(QColor(QStringLiteral("#4a5568")), 1.0));
        p.drawEllipse(pos, DOT_RADIUS, DOT_RADIUS);
    }
}
