#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TemporalHeatmapWidget — Day-of-Week × Hour-of-Day crime intensity heatmap
//
// Renders a 7×24 grid where each cell's colour encodes the number of crimes
// that occurred on that (day, hour) combination.  Colour scale: dark navy
// (zero) → crimson (maximum).
// ─────────────────────────────────────────────────────────────────────────────
#include <QWidget>
#include <QVector>
#include <array>
#include "core/CrimeEvent.h"

class TemporalHeatmapWidget : public QWidget {
    Q_OBJECT
public:
    explicit TemporalHeatmapWidget(QWidget* parent = nullptr);

    // Pass a flat 7×24 count matrix (row = day 0=Mon…6=Sun, col = hour 0..23)
    void setData(const std::array<std::array<int,24>,7>& counts);

    // Build heatmap from raw crime events (uses occurredAt or timestamp)
    void setData(const QVector<CrimeEvent>& events);

    // Aggregated projections computed from the current count matrix
    QVector<int> hourlyData() const;   // 24 elements, one per hour 0..23
    QVector<int> dailyData() const;    // 7 elements, one per day 0=Mon..6=Sun
    QVector<int> monthlyData() const;  // 12 elements, one per month 0=Jan..11=Dec

    QSize sizeHint() const override { return QSize(840, 220); }
    QSize minimumSizeHint() const override { return QSize(480, 140); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override { update(); }

private:
    QColor valueColor(double normalized) const;

    std::array<std::array<int,24>,7> m_counts{};
    int m_maxCount = 0;
    QVector<int> m_monthly = QVector<int>(12, 0);

    static constexpr int MARGIN_LEFT   = 56;
    static constexpr int MARGIN_TOP    = 28;
    static constexpr int MARGIN_RIGHT  = 12;
    static constexpr int MARGIN_BOTTOM = 36;
};
