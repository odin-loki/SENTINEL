#include "inference/HintEngine.h"
#include "core/SentinelLogger.h"

#include <algorithm>
#include <cmath>
#include <QJsonArray>

// ─── Provenance helper ────────────────────────────────────────────────────────

QString HintEngine::provenanceStr(const QStringList& chain)
{
    return chain.join(QStringLiteral(" → "));
}

// ─── Series leads ─────────────────────────────────────────────────────────────

QVector<InvestigativeLead> HintEngine::seriesLeads(
    const QVector<SeriesMatch>& matches, int& rank) const
{
    QVector<InvestigativeLead> leads;
    for (const auto& m : matches) {
        if (m.linkProbability < 0.2) continue;

        InvestigativeLead lead;
        lead.rank     = rank++;
        lead.category = QStringLiteral("series_linkage");
        lead.headline = QStringLiteral("Linked to series %1 (%2 members, p=%3)")
                            .arg(m.seriesId)
                            .arg(m.memberCount)
                            .arg(m.linkProbability, 0, 'f', 3);
        lead.detail   = QStringLiteral(
                            "Spatial distance: %1 m | Temporal distance: %2 days | "
                            "MO similarity: %3 | Composite: %4 | Method: %5")
                            .arg(m.spatialDistanceM,     0, 'f', 1)
                            .arg(m.temporalDistanceDays, 0, 'f', 1)
                            .arg(m.moSimilarity,         0, 'f', 2)
                            .arg(m.compositeScore,       0, 'f', 3)
                            .arg(m.method);
        lead.confidence       = m.linkProbability;
        lead.confidenceMethod = QStringLiteral("near_repeat_composite_kernel");

        QJsonObject data;
        data[QStringLiteral("seriesId")]           = m.seriesId;
        data[QStringLiteral("memberCount")]         = m.memberCount;
        data[QStringLiteral("spatialDistanceM")]    = m.spatialDistanceM;
        data[QStringLiteral("temporalDistanceDays")]= m.temporalDistanceDays;
        data[QStringLiteral("moSimilarity")]        = m.moSimilarity;
        data[QStringLiteral("compositeScore")]      = m.compositeScore;
        lead.supportingData = data;

        lead.provenance = {
            QStringLiteral("SeriesDetector.link_probability"),
            QStringLiteral("near_repeat_composite_kernel")
        };
        lead.generatedAt = QDateTime::currentDateTimeUtc();
        leads.append(lead);
    }
    return leads;
}

// ─── MO leads ─────────────────────────────────────────────────────────────────

QVector<InvestigativeLead> HintEngine::moLeads(
    const QVector<MOMatch>& matches, int& rank) const
{
    QVector<InvestigativeLead> leads;
    int count = 0;
    for (const auto& m : matches) {
        if (count >= 3) break;
        if (m.similarityScore < 0.3) continue;

        const double conf = std::min(m.similarityScore, 0.95);

        InvestigativeLead lead;
        lead.rank     = rank++;
        lead.category = QStringLiteral("mo_similarity");
        lead.headline = m.resolved
            ? QStringLiteral("Resolved case %1 shares MO (similarity %2)").arg(m.caseId).arg(conf, 0, 'f', 2)
            : QStringLiteral("Open case %1 shares MO (similarity %2)").arg(m.caseId).arg(conf, 0, 'f', 2);

        QString detail = QStringLiteral("Shared features: %1")
                             .arg(m.sharedFeatures.join(QStringLiteral(", ")));
        if (!m.suspectProfile.isEmpty())
            detail += QStringLiteral(" | Suspect profile: %1").arg(m.suspectProfile);
        if (m.resolved && !m.outcome.isEmpty())
            detail += QStringLiteral(" | Outcome: %1").arg(m.outcome);
        lead.detail = detail;

        lead.confidence       = conf;
        lead.confidenceMethod = QStringLiteral("tfidf_cosine_similarity");

        QJsonObject data;
        data[QStringLiteral("caseId")]          = m.caseId;
        data[QStringLiteral("similarityScore")] = m.similarityScore;
        data[QStringLiteral("resolved")]        = m.resolved;
        if (m.convictionYear > 0)
            data[QStringLiteral("convictionYear")] = m.convictionYear;
        QJsonArray sharedArr;
        for (const auto& f : m.sharedFeatures) sharedArr.append(f);
        data[QStringLiteral("sharedFeatures")] = sharedArr;
        lead.supportingData = data;

        lead.provenance = {
            QStringLiteral("MOAnalyser.findSimilar"),
            QStringLiteral("tfidf_cosine_similarity")
        };
        lead.generatedAt = QDateTime::currentDateTimeUtc();
        leads.append(lead);
        ++count;
    }
    return leads;
}

// ─── Geographic leads ─────────────────────────────────────────────────────────

QVector<InvestigativeLead> HintEngine::geoLeads(
    const GeographicProfile& profile, int& rank) const
{
    QVector<InvestigativeLead> leads;
    if (profile.peakProbability <= 0.001) return leads;

    InvestigativeLead lead;
    lead.rank     = rank++;
    lead.category = QStringLiteral("geographic_profile");
    lead.headline = QStringLiteral("Peak anchor at (%1, %2) \xe2\x80\x94 p=%3")
                        .arg(profile.peakLat,         0, 'f', 5)
                        .arg(profile.peakLon,         0, 'f', 5)
                        .arg(profile.peakProbability, 0, 'f', 4);
    lead.detail   = QStringLiteral("50%% search area: %1 km\xb2 | 80%% search area: %2 km\xb2 | Method: %3")
                        .arg(profile.searchArea50pct, 0, 'f', 2)
                        .arg(profile.searchArea80pct, 0, 'f', 2)
                        .arg(profile.method);
    lead.confidence       = std::min(profile.peakProbability * 100.0, 0.85);
    lead.confidenceMethod = QStringLiteral("rossmo_cgt");

    QJsonObject data;
    data[QStringLiteral("peakLat")]         = profile.peakLat;
    data[QStringLiteral("peakLon")]         = profile.peakLon;
    data[QStringLiteral("peakProbability")] = profile.peakProbability;
    data[QStringLiteral("searchArea50pct")] = profile.searchArea50pct;
    data[QStringLiteral("searchArea80pct")] = profile.searchArea80pct;
    lead.supportingData = data;

    lead.provenance = {
        QStringLiteral("GeographicProfiler.profile"),
        QStringLiteral("rossmo_cgt")
    };
    lead.generatedAt = QDateTime::currentDateTimeUtc();
    leads.append(lead);
    return leads;
}

// ─── Anomaly leads ────────────────────────────────────────────────────────────

QVector<InvestigativeLead> HintEngine::anomalyLeads(
    const AnomalySignal& signal, int& rank) const
{
    QVector<InvestigativeLead> leads;
    if (!signal.isAnomaly) return leads;

    InvestigativeLead lead;
    lead.rank     = rank++;
    lead.category = QStringLiteral("statistical_anomaly");
    lead.headline = QStringLiteral("Event %1 flagged as statistical anomaly (score %2)")
                        .arg(signal.eventId)
                        .arg(signal.combinedScore, 0, 'f', 3);
    const QStringList sigList(signal.signalReasons.cbegin(), signal.signalReasons.cend());
    lead.detail   = QStringLiteral("Isolation: %1 | LOF: %2 | Z-temporal: %3 | Z-spatial: %4 | Signals: %5")
                        .arg(signal.isolationScore,  0, 'f', 3)
                        .arg(signal.lofScore,        0, 'f', 3)
                        .arg(signal.zScoreTemporal,  0, 'f', 2)
                        .arg(signal.zScoreSpatial,   0, 'f', 2)
                        .arg(sigList.join(QStringLiteral(", ")));
    lead.confidence       = signal.combinedScore;
    lead.confidenceMethod = QStringLiteral("multi_method_anomaly_score");

    QJsonObject data;
    data[QStringLiteral("eventId")]        = signal.eventId;
    data[QStringLiteral("isolationScore")] = signal.isolationScore;
    data[QStringLiteral("lofScore")]       = signal.lofScore;
    data[QStringLiteral("zTemporal")]      = signal.zScoreTemporal;
    data[QStringLiteral("zSpatial")]       = signal.zScoreSpatial;
    data[QStringLiteral("combinedScore")]  = signal.combinedScore;
    QJsonArray sigArr;
    for (const auto& s : signal.signalReasons) sigArr.append(s);
    data[QStringLiteral("signals")]        = sigArr;
    lead.supportingData = data;

    lead.provenance = {
        QStringLiteral("AnomalyDetector.detectAnomalies"),
        QStringLiteral("multi_method_anomaly_score")
    };
    lead.generatedAt = QDateTime::currentDateTimeUtc();
    leads.append(lead);
    return leads;
}

// ─── Network leads ────────────────────────────────────────────────────────────

QVector<InvestigativeLead> HintEngine::networkLeadsFromInput(
    const QVector<NetworkLead>& leads, int& rank) const
{
    QVector<InvestigativeLead> result;
    for (const auto& nl : leads) {
        InvestigativeLead lead;
        lead.rank     = rank++;
        lead.category = QStringLiteral("network_association");
        lead.headline = QStringLiteral("Person %1 linked via %2 (%3 shared incidents)")
                            .arg(nl.personId)
                            .arg(nl.connectionType)
                            .arg(nl.sharedIncidents);
        lead.detail   = QStringLiteral("Centrality: %1 | Community: %2 | Risk: %3 | %4")
                            .arg(nl.centralityScore, 0, 'f', 3)
                            .arg(nl.communityId)
                            .arg(nl.riskScore,       0, 'f', 3)
                            .arg(nl.reasoning);
        lead.confidence       = std::min(nl.riskScore, 0.95);
        lead.confidenceMethod = QStringLiteral("network_analysis_centrality");

        QJsonObject data;
        data[QStringLiteral("personId")]        = nl.personId;
        data[QStringLiteral("connectionType")]  = nl.connectionType;
        data[QStringLiteral("sharedIncidents")] = nl.sharedIncidents;
        data[QStringLiteral("centralityScore")] = nl.centralityScore;
        data[QStringLiteral("communityId")]     = nl.communityId;
        data[QStringLiteral("riskScore")]       = nl.riskScore;
        lead.supportingData = data;

        lead.provenance = {
            QStringLiteral("NetworkAnalyser"),
            QStringLiteral("network_analysis_centrality")
        };
        lead.generatedAt = QDateTime::currentDateTimeUtc();
        result.append(lead);
    }
    return result;
}

// ─── Contradiction detection ──────────────────────────────────────────────────

void HintEngine::detectContradictions(QVector<InvestigativeLead>& leads) const
{
    // Pass 1: detect solo-vs-group contradictions from detail text
    for (int i = 0; i < leads.size(); ++i) {
        const bool iSolo  = leads[i].detail.contains(QStringLiteral("solo"),  Qt::CaseInsensitive);
        const bool iGroup = leads[i].detail.contains(QStringLiteral("group"), Qt::CaseInsensitive);

        for (int j = i + 1; j < leads.size(); ++j) {
            const bool jSolo  = leads[j].detail.contains(QStringLiteral("solo"),  Qt::CaseInsensitive);
            const bool jGroup = leads[j].detail.contains(QStringLiteral("group"), Qt::CaseInsensitive);

            if ((iSolo && jGroup) || (iGroup && jSolo)) {
                const QString msg = QStringLiteral("Contradicts lead rank %1 (solo vs group)")
                                        .arg(leads[j].rank);
                leads[i].contradictions.push_back(msg);
                const QString msg2 = QStringLiteral("Contradicts lead rank %1 (solo vs group)")
                                         .arg(leads[i].rank);
                leads[j].contradictions.push_back(msg2);
            }
        }
    }

    // Pass 2: detect same-category leads with a very large confidence divergence
    // (> 0.5 delta). Two leads in the same investigative zone that strongly
    // support contradictory conclusions should be flagged for analyst review.
    {
        QMap<QString, QVector<int>> catIdx;
        for (int i = 0; i < leads.size(); ++i)
            catIdx[leads[i].category].append(i);

        for (auto it = catIdx.constBegin(); it != catIdx.constEnd(); ++it) {
            const auto& idxs = it.value();
            for (int a = 0; a < static_cast<int>(idxs.size()); ++a) {
                for (int b = a + 1; b < static_cast<int>(idxs.size()); ++b) {
                    const int i = idxs[a], j = idxs[b];
                    if (std::abs(leads[i].confidence - leads[j].confidence) > 0.5) {
                        leads[i].contradictions.push_back(
                            QStringLiteral("Confidence conflict with lead rank %1 "
                                           "(same category '%2', delta > 0.5)")
                                .arg(leads[j].rank).arg(it.key()));
                        leads[j].contradictions.push_back(
                            QStringLiteral("Confidence conflict with lead rank %1 "
                                           "(same category '%2', delta > 0.5)")
                                .arg(leads[i].rank).arg(it.key()));
                    }
                }
            }
        }
    }
}

// ─── Ranking ──────────────────────────────────────────────────────────────────

double HintEngine::rankScore(const InvestigativeLead& lead,
                              double dataQuality) const
{
    const double qualityBonus = (dataQuality > 0.7) ? 1.0 : 0.5;
    const int nContra = static_cast<int>(lead.contradictions.size());
    const double novelty = 1.0 / std::max(nContra, 1);
    const double penalty = (nContra > 0) ? 0.1 * nContra : 0.0;

    return 0.7 * lead.confidence +
           0.2 * novelty +
           0.1 * qualityBonus -
           penalty;
}

void HintEngine::rerankLeads(QVector<InvestigativeLead>& leads,
                              double dataQuality) const
{
    std::sort(leads.begin(), leads.end(),
              [&](const InvestigativeLead& a, const InvestigativeLead& b) {
                  return rankScore(a, dataQuality) > rankScore(b, dataQuality);
              });

    for (int i = 0; i < leads.size(); ++i)
        leads[i].rank = i + 1;
}

// ─── Public entry point ───────────────────────────────────────────────────────

QVector<InvestigativeLead> HintEngine::generate(const HintEngineInput& input) const
{
    QVector<InvestigativeLead> all;
    int rank = 1;

    all += seriesLeads(input.seriesMatches, rank);
    all += moLeads(input.moMatches, rank);

    if (input.geoProfile.has_value())
        all += geoLeads(*input.geoProfile, rank);

    if (input.anomalySignal.has_value())
        all += anomalyLeads(*input.anomalySignal, rank);

    all += networkLeadsFromInput(input.networkLeads, rank);

    // Deduplicate: remove leads that share an identical headline
    {
        QSet<QString> seen;
        QVector<InvestigativeLead> deduped;
        deduped.reserve(all.size());
        for (const auto& lead : all) {
            if (!seen.contains(lead.headline)) {
                seen.insert(lead.headline);
                deduped.append(lead);
            }
        }
        all = std::move(deduped);
    }

    detectContradictions(all);
    rerankLeads(all, input.dataQuality);

    if (all.size() > kMaxLeads)
        all.resize(kMaxLeads);

    qCInfo(lcInference) << "Hint engine generated" << all.size()
                        << "leads for event" << input.event.eventId;
    return all;
}
