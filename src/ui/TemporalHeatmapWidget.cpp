#include "ui/TemporalHeatmapWidget.h"

#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

static const QStringList DAY_LABELS = {
    "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

// ─────────────────────────────────────────────────────────────────────────────
TemporalHeatmapWidget::TemporalHeatmapWidget(QWidget* parent)
    : QWidget(parent)
{
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

// ─────────────────────────────────────────────────────────────────────────────
void TemporalHeatmapWidget::setData(const std::array<std::array<int,24>,7>& counts)
{
    m_counts   = counts;
    m_maxCount = 0;
    for (const auto& row : counts)
        for (int v : row)
            if (v > m_maxCount) m_maxCount = v;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
void TemporalHeatmapWidget::setData(const QVector<CrimeEvent>& events)
{
    std::array<std::array<int,24>,7> counts{};
    for (auto& row : counts) row.fill(0);
    m_monthly.fill(0);

    for (const auto& e : events) {
        const QDateTime dt = e.occurredAt.value_or(e.timestamp);
        const int hour = std::clamp(dt.time().hour(), 0, 23);
        const int dow  = std::clamp(dt.date().dayOfWeek() - 1, 0, 6);
        const int mon  = std::clamp(dt.date().month() - 1, 0, 11);
        counts[dow][hour]++;
        m_monthly[mon]++;
    }

    setData(counts);
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<int> TemporalHeatmapWidget::hourlyData() const
{
    QVector<int> result(24, 0);
    for (int d = 0; d < 7; ++d)
        for (int h = 0; h < 24; ++h)
            result[h] += m_counts[d][h];
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<int> TemporalHeatmapWidget::dailyData() const
{
    QVector<int> result(7, 0);
    for (int d = 0; d < 7; ++d)
        for (int h = 0; h < 24; ++h)
            result[d] += m_counts[d][h];
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<int> TemporalHeatmapWidget::monthlyData() const
{
    return m_monthly;
}

// ─────────────────────────────────────────────────────────────────────────────
QColor TemporalHeatmapWidget::valueColor(double normalized) const
{
    if (normalized <= 0.0) return QColor(15, 21, 48);  // #0f1530 near black

    // Gradient: dark navy → dark purple → crimson → bright orange-red
    // Three control points:
    //   0.0 → #0f1530
    //   0.4 → #3d1e58 (purple)
    //   0.7 → #8b1a2a (dark red)
    //   1.0 → #e94560 (SENTINEL crimson)

    struct Stop { double t; int r, g, b; };
    static const Stop stops[] = {
        { 0.0, 15,  21,  48 },
        { 0.4, 61,  30,  88 },
        { 0.7, 139, 26,  42 },
        { 1.0, 233, 69,  96 },
    };

    const int nStops = static_cast<int>(std::size(stops));
    for (int i = 0; i < nStops - 1; ++i) {
        if (normalized <= stops[i+1].t) {
            const double range = stops[i+1].t - stops[i].t;
            const double f     = (range > 0) ? (normalized - stops[i].t) / range : 0.0;
            const int r = static_cast<int>(stops[i].r + f * (stops[i+1].r - stops[i].r));
            const int g = static_cast<int>(stops[i].g + f * (stops[i+1].g - stops[i].g));
            const int b = static_cast<int>(stops[i].b + f * (stops[i+1].b - stops[i].b));
            return QColor(r, g, b);
        }
    }
    return QColor(233, 69, 96);
}

// ─────────────────────────────────────────────────────────────────────────────
void TemporalHeatmapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect bounds = rect();
    const int w = bounds.width();
    const int h = bounds.height();

    // Background
    p.fillRect(bounds, QColor(13, 17, 23));  // #0d1117

    // Grid area
    const int gridLeft   = MARGIN_LEFT;
    const int gridTop    = MARGIN_TOP;
    const int gridWidth  = w - MARGIN_LEFT - MARGIN_RIGHT;
    const int gridHeight = h - MARGIN_TOP - MARGIN_BOTTOM;

    if (gridWidth <= 0 || gridHeight <= 0) return;

    const double cellW = static_cast<double>(gridWidth)  / 24.0;
    const double cellH = static_cast<double>(gridHeight) / 7.0;

    QFont labelFont = p.font();
    labelFont.setPixelSize(11);
    p.setFont(labelFont);
    p.setPen(QColor(160, 168, 184));  // SENTINEL_MUTED

    // Draw cells
    for (int day = 0; day < 7; ++day) {
        for (int hour = 0; hour < 24; ++hour) {
            const int count = m_counts[day][hour];
            const double normalized = (m_maxCount > 0)
                ? static_cast<double>(count) / m_maxCount : 0.0;

            const int cx = static_cast<int>(gridLeft + hour * cellW);
            const int cy = static_cast<int>(gridTop  + day  * cellH);
            const int cw = static_cast<int>(std::ceil(cellW)) + 1;
            const int ch = static_cast<int>(std::ceil(cellH)) + 1;

            p.fillRect(cx, cy, cw, ch, valueColor(normalized));

            // Thin separator lines
            p.setPen(QColor(13, 17, 23));
            p.drawRect(cx, cy, cw, ch);
            p.setPen(QColor(160, 168, 184));
        }

        // Day labels on left
        const QRect dayRect(0, static_cast<int>(gridTop + day * cellH),
                             MARGIN_LEFT - 4, static_cast<int>(cellH));
        p.drawText(dayRect, Qt::AlignRight | Qt::AlignVCenter,
                   DAY_LABELS[day]);
    }

    // Hour labels along bottom (every 3 hours)
    for (int hour = 0; hour < 24; hour += 3) {
        const int lx = static_cast<int>(gridLeft + hour * cellW);
        const int ly = gridTop + gridHeight + 4;
        const QRect hourRect(lx - 8, ly, 36, 20);
        p.drawText(hourRect, Qt::AlignHCenter, QString::number(hour) + "h");
    }

    // Title
    QFont titleFont = p.font();
    titleFont.setPixelSize(11);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(QColor(160, 168, 184));
    p.drawText(QRect(0, 4, w, 18),
               Qt::AlignHCenter,
               QStringLiteral("Crime Intensity — Day × Hour"));

    // Legend bar (bottom right)
    if (m_maxCount > 0) {
        const int barX = w - 120;
        const int barY = h - MARGIN_BOTTOM + 8;
        const int barW = 100;
        const int barH = 10;

        for (int i = 0; i < barW; ++i) {
            const double t = static_cast<double>(i) / barW;
            const QRect seg(barX + i, barY, 1, barH);
            p.fillRect(seg, valueColor(t));
        }
        p.setPen(QColor(160, 168, 184));
        p.drawText(QRect(barX - 4, barY, barW + 8, 20),
                   Qt::AlignLeft, "0");
        p.drawText(QRect(barX - 4, barY, barW + 8, 20),
                   Qt::AlignRight,
                   QString::number(m_maxCount));
        p.drawRect(barX, barY, barW, barH);
    }
}
