#pragma once
#include <QVector>
#include <QString>
#include <QMap>
#include <QPair>

struct BiasReport {
    QString metric;        // "disparate_impact", "equal_opportunity", etc.
    QString groupA;
    QString groupB;
    double  valueA  = 0.0;
    double  valueB  = 0.0;
    double  ratio   = 0.0;    // valueA / valueB
    bool    flagged = false;  // ratio outside [0.80, 1.25]
    QString notes;
};

struct GroupStats {
    QString groupId;
    int     nEvents    = 0;
    int     nFlagged   = 0;
    double  flagRate   = 0.0;  // nFlagged / nEvents
    double  meanPred   = 0.0;
    double  actualRate = 0.0;
    int     nTP        = 0;    // true positives (yTrue=1 and yPred >= threshold)
    int     nActualPos = 0;    // total actual positives (yTrue=1)
};

class BiasAuditor {
public:
    // Disparate impact: ratio of positive prediction rates between groups.
    // Values outside [0.80, 1.25] (4/5 rule) are flagged.
    // groups: parallel vector mapping each event to a group label (e.g., LGA)
    // yPred: predicted probability (threshold at 0.5 for positive)
    static QVector<BiasReport> disparateImpact(
        const QVector<QString>& groups,
        const QVector<double>& yPred,
        double threshold = 0.5);

    // Equal opportunity: compare true-positive rates across groups
    static QVector<BiasReport> equalOpportunity(
        const QVector<QString>& groups,
        const QVector<double>& yTrue,
        const QVector<double>& yPred,
        double threshold = 0.5);

    // Compute per-group statistics
    static QMap<QString, GroupStats> groupStats(
        const QVector<QString>& groups,
        const QVector<double>& yTrue,
        const QVector<double>& yPred);

    // Feedback loop detection: flag groups where prediction rate
    // trends up while actual rate is flat or declining.
    // trendData[groupId] = list of (pred_rate, actual_rate) over time windows
    static QVector<QString> feedbackLoopCheck(
        const QMap<QString, QVector<QPair<double,double>>>& trendData,
        double sensitivity = 0.01);

    // Ratio of max to min selection rate across groups (max/min).
    // Returns 0.0 if any group is empty.
    double maxDisparateImpact(const QVector<GroupStats>& groups) const;

    // Max absolute difference in TPR (TP / (TP + FN)) across groups.
    // Returns 0.0 if any group has no actual positives.
    double equalizedOddsDiff(const QVector<GroupStats>& groups) const;

    // Format all reports as a text summary
    static QString formatReports(const QVector<BiasReport>& reports);

private:
    static constexpr double DI_LOW  = 0.80;
    static constexpr double DI_HIGH = 1.25;

    static double trendSlope(const QVector<double>& values);
};
