#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// GeoProfileHeatmapWidget — Rossmo CGT probability surface mini-map
//
// Renders a bounded lat/lon view of GeographicProfile.probabilitySurface with
// crime-location dots overlaid.  Intended for the Case Workspace geo summary.
// ─────────────────────────────────────────────────────────────────────────────
#include <QWidget>
#include <QVector>
#include <QPair>
#include <optional>
#include "core/CrimeEvent.h"

class GeoProfileHeatmapWidget : public QWidget {
    Q_OBJECT
public:
    explicit GeoProfileHeatmapWidget(QWidget* parent = nullptr);

    void setProfile(const GeographicProfile& profile);
    void setCrimeLocations(QVector<QPair<double, double>> locations);
    void clear();

    bool hasProfile() const { return m_hasProfile; }

    QSize minimumSizeHint() const override { return QSize(200, 180); }
    QSize sizeHint() const override { return QSize(320, 180); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override { update(); }

private:
    QColor riskColor(double probability) const;
    QPointF latLonToPixel(double lat, double lon) const;
    void recomputeBounds();

    std::optional<GeographicProfile> m_profile;
    QVector<QPair<double, double>>   m_crimeLocations;
    bool m_hasProfile = false;

    double m_latMin = 0.0;
    double m_latMax = 0.0;
    double m_lonMin = 0.0;
    double m_lonMax = 0.0;

    static constexpr int MARGIN     = 8;
    static constexpr int DOT_RADIUS = 4;
};
