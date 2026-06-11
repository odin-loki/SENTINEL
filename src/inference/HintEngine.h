#pragma once
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <optional>
#include "core/CrimeEvent.h"
// MOMatch, NetworkLead, MOCaseRecord defined in core/CrimeEvent.h

struct HintEngineInput {
    CrimeEvent event;
    QVector<SeriesMatch> seriesMatches;
    QVector<MOMatch> moMatches;
    std::optional<GeographicProfile> geoProfile;
    QVector<NetworkLead> networkLeads;
    QVector<EvidenceWeight> evidenceWeights;
    std::optional<AnomalySignal> anomalySignal;
    double dataQuality = 1.0;
};

class HintEngine {
public:
    static constexpr int kMaxLeads = 50;

    // Generate ranked leads from all inference inputs
    QVector<InvestigativeLead> generate(const HintEngineInput& input) const;

private:
    QVector<InvestigativeLead> seriesLeads(
        const QVector<SeriesMatch>& matches, int& rank) const;
    QVector<InvestigativeLead> moLeads(
        const QVector<MOMatch>& matches, int& rank) const;
    QVector<InvestigativeLead> geoLeads(
        const GeographicProfile& profile, int& rank) const;
    QVector<InvestigativeLead> anomalyLeads(
        const AnomalySignal& signal, int& rank) const;
    QVector<InvestigativeLead> networkLeadsFromInput(
        const QVector<NetworkLead>& leads, int& rank) const;

    void detectContradictions(QVector<InvestigativeLead>& leads) const;

    double rankScore(const InvestigativeLead& lead,
                     double dataQuality) const;
    void rerankLeads(QVector<InvestigativeLead>& leads,
                     double dataQuality) const;

    static QString provenanceStr(const QStringList& chain);
};
