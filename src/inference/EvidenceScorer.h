#pragma once
#include <QVector>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QPair>
#include <vector>
#include "core/CrimeEvent.h"

struct EvidenceItem {
    QString type;
    bool present;
};

class EvidenceScorer {
public:
    // ── Result type for UI evidence-scorer panel ─────────────────────────────
    struct Contribution {
        QString name;
        double  likelihoodRatio = 1.0;
        bool    wasPresent      = false;
    };
    struct Result {
        double posteriorProbability   = 0.5;
        double overallLikelihoodRatio = 1.0;
        std::vector<Contribution> contributions;
    };

    EvidenceScorer();

    // Original API: list of typed evidence items
    QVector<EvidenceWeight> score(const QVector<EvidenceItem>& evidence,
                                   double priorProbability = 0.05) const;

    // UI convenience: named evidence map (present/absent) → Result
    Result score(double priorProbability,
                 const QMap<QString, bool>& evidencePresence) const;

    QStringList availableEvidenceTypes() const;

    QPair<double,double> getLRAndReliability(const QString& type) const;

    // Compute Bayes Factor: how much more likely is hypothesis A vs B
    // given the same set of evidence.
    // Returns BF > 1 if evidence favours A, < 1 if it favours B.
    double bayesFactor(double priorA, double priorB,
                       const QMap<QString, bool>& evidencePresence) const;

    // Return the number of evidence types in the LR table
    int evidenceTypeCount() const { return m_lrTable.size(); }

private:
    struct LREntry {
        double lr;
        double reliability;
        QString description;
    };
    QMap<QString, LREntry> m_lrTable;
    void buildLRTable();
};
