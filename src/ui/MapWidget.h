#pragma once
#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QLabel>
#include <optional>
#include "core/CrimeEvent.h"
#include "models/KDEHotspot.h"

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget* parent = nullptr);

    void setEvents(const QVector<CrimeEvent>& events);
    void setRiskSurface(const GeographicProfile& profile);
    void setKDEHotspots(const QVector<HotspotRegion>& hotspots);
    // setHotspots is an alias for setKDEHotspots
    void setHotspots(const QVector<HotspotRegion>& hotspots);
    void clearKDEHotspots();
    // clearOverlays clears events, risk surface, and KDE hotspots
    void clearOverlays();
    void setCenter(double lat, double lon);
    void setZoom(double degreesPerPixel);
    void clearRiskSurface();

    int zoomLevelInt() const;

signals:
    void locationClicked(double lat, double lon);
    void regionClicked(double lat, double lon);
    void zoomChanged(int zoomLevel);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void drawGrid(QPainter& p);
    void drawEvents(QPainter& p);
    void drawRiskHeatmap(QPainter& p);
    void drawKDEHotspots(QPainter& p);
    void drawScale(QPainter& p);
    void drawCompass(QPainter& p);
    void updateZoomLabel();

    QPointF latLonToPixel(double lat, double lon) const;
    QPair<double, double> pixelToLatLon(const QPoint& px) const;
    QColor crimeTypeColor(const QString& crimeType) const;
    QColor riskColor(double probability) const;

    double m_centerLat   = 51.5074;
    double m_centerLon   = -0.1278;
    double m_degPerPixel = 0.0001;

    QVector<CrimeEvent>              m_events;
    std::optional<GeographicProfile> m_riskProfile;
    QVector<HotspotRegion>           m_kdeHotspots;
    QPoint m_lastMousePos;
    bool   m_dragging   = false;
    bool   m_dragMoved  = false;

    QLabel* m_zoomLabel = nullptr;
};
