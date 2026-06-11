#include "benchmark/BiasAuditor.h"

#include <algorithm>
#include <cmath>
#include <QStringList>

namespace {

// Collect unique group IDs in stable insertion order
QVector<QString> uniqueGroups(const QVector<QString>& groups)
{
    QVector<QString> result;
    for (const auto& g : groups) {
        if (!result.contains(g))
            result.append(g);
    }
    return result;
}

// Generate all pairs (i, j) with i < j from a list
QVector<QPair<int,int>> allPairs(int n)
{
    QVector<QPair<int,int>> pairs;
    for (int i = 0; i < n; ++i)
        for (int j = i+1; j < n; ++j)
            pairs.append({i, j});
    return pairs;
}

} // anonymous namespace

// ─── trendSlope (least-squares) ─────────────────────────────────────────────

double BiasAuditor::trendSlope(const QVector<double>& values)
{
    const int n = values.size();
    if (n < 2) return 0.0;

    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
    for (int i = 0; i < n; ++i) {
        sumX  += i;
        sumY  += values[i];
        sumXY += i * values[i];
        sumX2 += i * i;
    }
    const double denom = n * sumX2 - sumX * sumX;
    if (std::abs(denom) < 1e-12) return 0.0;
    return (n * sumXY - sumX * sumY) / denom;
}

// ─── groupStats ──────────────────────────────────────────────────────────────

QMap<QString, GroupStats> BiasAuditor::groupStats(
    const QVector<QString>& groups,
    const QVector<double>& yTrue,
    const QVector<double>& yPred)
{
    QMap<QString, GroupStats> stats;
    const int n = groups.size();

    for (int i = 0; i < n; ++i) {
        const auto& g = groups[i];
        auto& s = stats[g];
        s.groupId = g;
        s.nEvents++;
        const bool predPos = (yPred[i] >= 0.5);
        const bool actPos  = (yTrue[i] >= 0.5);
        if (predPos) s.nFlagged++;
        s.meanPred   += yPred[i];
        s.actualRate += yTrue[i];
        if (actPos) {
            s.nActualPos++;
            if (predPos) s.nTP++; else s.nFN++;
        } else {
            if (predPos) s.nFP++; else s.nTN++;
        }
    }

    for (auto& s : stats) {
        if (s.nEvents > 0) {
            s.flagRate   = static_cast<double>(s.nFlagged) / s.nEvents;
            s.meanPred   /= s.nEvents;
            s.actualRate /= s.nEvents;
        }
        const int negTotal = s.nFP + s.nTN;
        const int posTotal = s.nTP + s.nFN;
        s.falsePositiveRate = (negTotal > 0) ? static_cast<double>(s.nFP) / negTotal : 0.0;
        s.falseNegativeRate = (posTotal > 0) ? static_cast<double>(s.nFN) / posTotal : 0.0;
        s.truePositiveRate  = (posTotal > 0) ? static_cast<double>(s.nTP) / posTotal : 0.0;
    }
    return stats;
}

// ─── disparateImpact ─────────────────────────────────────────────────────────

QVector<BiasReport> BiasAuditor::disparateImpact(
    const QVector<QString>& groups,
    const QVector<double>& yPred,
    double threshold)
{
    QVector<BiasReport> reports;
    const int n = groups.size();
    if (n == 0) return reports;

    // Build a dummy yTrue of zeros (not used for disparate impact)
    QVector<double> yZero(n, 0.0);
    const auto stats = groupStats(groups, yZero, yPred);
    const QVector<QString> gList = stats.keys().toVector();

    for (const auto& [i, j] : allPairs(gList.size())) {
        const auto& sa = stats[gList[i]];
        const auto& sb = stats[gList[j]];

        // Re-compute flag rate using the requested threshold (groupStats uses 0.5 internally)
        // Recompute from raw data for the correct threshold
        double countA = 0, posA = 0, countB = 0, posB = 0;
        for (int k = 0; k < n; ++k) {
            if (groups[k] == sa.groupId) {
                countA++;
                if (yPred[k] >= threshold) posA++;
            } else if (groups[k] == sb.groupId) {
                countB++;
                if (yPred[k] >= threshold) posB++;
            }
        }
        const double rateA = (countA > 0) ? posA / countA : 0.0;
        const double rateB = (countB > 0) ? posB / countB : 0.0;

        BiasReport rep;
        rep.metric = QStringLiteral("disparate_impact");
        rep.groupA = sa.groupId;
        rep.groupB = sb.groupId;
        rep.valueA = rateA;
        rep.valueB = rateB;

        const double hiRate = std::max(rateA, rateB);
        const double loRate = std::min(rateA, rateB);

        if (hiRate < 1e-9) {
            rep.ratio   = 1.0;
            rep.flagged = false;
            rep.notes   = QStringLiteral("Both groups have zero positive rate");
        } else {
            rep.ratio   = loRate / hiRate;
            rep.flagged = (rep.ratio < DI_LOW);
        }
        reports.append(rep);
    }
    return reports;
}

// ─── equalOpportunity ────────────────────────────────────────────────────────

QVector<BiasReport> BiasAuditor::equalOpportunity(
    const QVector<QString>& groups,
    const QVector<double>& yTrue,
    const QVector<double>& yPred,
    double threshold)
{
    QVector<BiasReport> reports;
    const int n = groups.size();
    if (n == 0) return reports;

    // Collect per-group TP and actual positives
    QMap<QString, double> tp, actualPos;
    const auto gList = uniqueGroups(groups);
    for (const auto& g : gList) {
        tp[g]        = 0.0;
        actualPos[g] = 0.0;
    }
    for (int i = 0; i < n; ++i) {
        if (yTrue[i] >= 0.5) {
            actualPos[groups[i]] += 1.0;
            if (yPred[i] >= threshold) tp[groups[i]] += 1.0;
        }
    }

    // TPR per group
    QMap<QString, double> tpr;
    for (const auto& g : gList)
        tpr[g] = (actualPos[g] > 0) ? tp[g] / actualPos[g] : 0.0;

    for (const auto& [i, j] : allPairs(gList.size())) {
        const auto& ga = gList[i];
        const auto& gb = gList[j];

        BiasReport rep;
        rep.metric = QStringLiteral("equal_opportunity");
        rep.groupA = ga;
        rep.groupB = gb;
        rep.valueA = tpr[ga];
        rep.valueB = tpr[gb];

        if (tpr[gb] < 1e-9) {
            if (tpr[ga] < 1e-9) {
                rep.ratio   = 1.0;
                rep.flagged = false;
                rep.notes   = QStringLiteral("Both groups have zero TPR");
            } else {
                rep.ratio   = std::numeric_limits<double>::infinity();
                rep.flagged = true;
                rep.notes   = QStringLiteral("Group B has zero TPR");
            }
        } else {
            rep.ratio   = tpr[ga] / tpr[gb];
            rep.flagged = (rep.ratio < DI_LOW || rep.ratio > DI_HIGH);
        }
        reports.append(rep);
    }
    return reports;
}

// ─── feedbackLoopCheck ───────────────────────────────────────────────────────

QVector<QString> BiasAuditor::feedbackLoopCheck(
    const QMap<QString, QVector<QPair<double,double>>>& trendData,
    double sensitivity)
{
    QVector<QString> flagged;

    for (auto it = trendData.constBegin(); it != trendData.constEnd(); ++it) {
        const auto& series = it.value();
        if (series.size() < 2) continue;

        QVector<double> predRates, actualRates;
        predRates.reserve(series.size());
        actualRates.reserve(series.size());
        for (const auto& [pred, actual] : series) {
            predRates.append(pred);
            actualRates.append(actual);
        }

        const double predSlope   = trendSlope(predRates);
        const double actualSlope = trendSlope(actualRates);

        // Flag if pred is going up AND actual is flat or declining
        if (predSlope > sensitivity && actualSlope <= sensitivity)
            flagged.append(it.key());
    }
    return flagged;
}

// ─── maxDisparateImpact ──────────────────────────────────────────────────────

double BiasAuditor::maxDisparateImpact(const QVector<GroupStats>& groups) const
{
    if (groups.isEmpty()) return 0.0;

    double maxRate = -1.0, minRate = 2.0;
    for (const auto& g : groups) {
        if (g.nEvents == 0) return 0.0;
        const double rate = static_cast<double>(g.nFlagged) / g.nEvents;
        maxRate = std::max(maxRate, rate);
        minRate = std::min(minRate, rate);
    }

    if (minRate < 1e-12) return 0.0;
    return maxRate / minRate;
}

// ─── equalizedOddsDiff ───────────────────────────────────────────────────────

double BiasAuditor::equalizedOddsDiff(const QVector<GroupStats>& groups) const
{
    if (groups.isEmpty()) return 0.0;

    double maxTPR = -1.0, minTPR = 2.0;
    for (const auto& g : groups) {
        if (g.nActualPos == 0) return 0.0;
        const double tpr = static_cast<double>(g.nTP) / g.nActualPos;
        maxTPR = std::max(maxTPR, tpr);
        minTPR = std::min(minTPR, tpr);
    }

    return maxTPR - minTPR;
}

// ─── formatReports ───────────────────────────────────────────────────────────

QString BiasAuditor::formatReports(const QVector<BiasReport>& reports)
{
    if (reports.isEmpty())
        return QStringLiteral("No bias reports.\n");

    QStringList lines;
    lines << QStringLiteral("Bias Audit Report (%1 comparison(s))").arg(reports.size());
    lines << QString(50, u'-');

    for (const auto& r : reports) {
        lines << QStringLiteral("[%1] %2 vs %3")
                   .arg(r.flagged ? QStringLiteral("FLAGGED") : QStringLiteral("OK"))
                   .arg(r.groupA, r.groupB);
        lines << QStringLiteral("  Metric: %1  Ratio: %2 (%3=%4 / %5=%6)")
                   .arg(r.metric)
                   .arg(r.ratio,  0, 'f', 4)
                   .arg(r.groupA).arg(r.valueA, 0, 'f', 4)
                   .arg(r.groupB).arg(r.valueB, 0, 'f', 4);
        if (!r.notes.isEmpty())
            lines << QStringLiteral("  Notes: ") + r.notes;
    }
    return lines.join(u'\n') + u'\n';
}
