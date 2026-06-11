#include "inference/EvidenceScorer.h"
#include <algorithm>
#include <cmath>

namespace {

// Interpolate LR toward neutral (1.0) based on source reliability (for display).
// reliability=0 → neutral (LR=1); reliability=1 → full LR applied.
// NOTE: the score() methods use raw LR for Bayesian updates; this helper
// is provided for external use or future weighted variants.
double reliabilityWeightedLR(double lr, double reliability)
{
    if (reliability <= 0.0) return 1.0;
    if (lr <= 0.0) return 0.0;
    return std::pow(lr, reliability);
}

} // namespace

EvidenceScorer::EvidenceScorer()
{
    buildLRTable();
}

void EvidenceScorer::buildLRTable()
{
    m_lrTable = {
        { QStringLiteral("eyewitness_identification_ideal"),
          { 4.0,    0.70, QStringLiteral("Eyewitness under ideal conditions") } },
        { QStringLiteral("eyewitness_identification_poor"),
          { 1.5,    0.40, QStringLiteral("Eyewitness under poor conditions") } },
        { QStringLiteral("fingerprint_match_10pt"),
          { 1e6,    0.99, QStringLiteral("Fingerprint 10-point match") } },
        { QStringLiteral("fingerprint_match_8pt"),
          { 1e4,    0.97, QStringLiteral("Fingerprint 8-point match") } },
        { QStringLiteral("dna_match_full_profile"),
          { 1e9,    0.999,QStringLiteral("DNA full profile match") } },
        { QStringLiteral("dna_match_partial"),
          { 1e4,    0.95, QStringLiteral("DNA partial profile match") } },
        { QStringLiteral("cctv_clear_face"),
          { 50.0,   0.85, QStringLiteral("CCTV clear facial image") } },
        { QStringLiteral("cctv_partial"),
          { 5.0,    0.60, QStringLiteral("CCTV partial image") } },
        { QStringLiteral("phone_records_at_scene"),
          { 200.0,  0.95, QStringLiteral("Phone records place suspect at scene") } },
        { QStringLiteral("alibi_strong"),
          { 0.05,   0.80, QStringLiteral("Strong alibi (exculpatory)") } },
        { QStringLiteral("alibi_weak"),
          { 0.50,   0.50, QStringLiteral("Weak alibi") } },
        { QStringLiteral("modus_operandi_match_high"),
          { 8.0,    0.75, QStringLiteral("High MO similarity") } },
        { QStringLiteral("modus_operandi_match_moderate"),
          { 3.0,    0.65, QStringLiteral("Moderate MO similarity") } },
        { QStringLiteral("geographic_profile_in_peak_zone"),
          { 4.5,    0.70, QStringLiteral("Suspect lives/works in geographic peak zone") } },
        { QStringLiteral("prior_conviction_same_type"),
          { 3.0,    0.90, QStringLiteral("Prior conviction for same crime type") } },
        { QStringLiteral("network_link_direct"),
          { 2.0,    0.70, QStringLiteral("Direct criminal network link") } },
        { QStringLiteral("vehicle_at_scene"),
          { 15.0,   0.80, QStringLiteral("Suspect vehicle identified at scene") } },
        { QStringLiteral("tool_mark_match"),
          { 100.0,  0.85, QStringLiteral("Tool-mark match") } },
        { QStringLiteral("shoe_impression_match"),
          { 200.0,  0.88, QStringLiteral("Shoe impression match") } },
        { QStringLiteral("glass_fragments_match"),
          { 50.0,   0.80, QStringLiteral("Glass fragment transfer match") } },
        { QStringLiteral("fibre_transfer_match"),
          { 30.0,   0.75, QStringLiteral("Fibre transfer match") } },
        { QStringLiteral("blood_type_match"),
          { 3.0,    0.60, QStringLiteral("Blood type match (non-exclusive)") } },
        { QStringLiteral("social_media_location"),
          { 20.0,   0.75, QStringLiteral("Social media confirms location near scene") } },
        { QStringLiteral("digital_device_at_scene"),
          { 150.0,  0.90, QStringLiteral("Digital device (phone/laptop) geolocated at scene") } },
        { QStringLiteral("informant_tip_reliable"),
          { 5.0,    0.60, QStringLiteral("Tip from reliable informant") } },
        { QStringLiteral("informant_tip_unreliable"),
          { 1.5,    0.30, QStringLiteral("Tip from unreliable informant") } },
        { QStringLiteral("no_alibi"),
          { 1.2,    0.40, QStringLiteral("No alibi provided") } },
    };
}

QStringList EvidenceScorer::availableEvidenceTypes() const
{
    return m_lrTable.keys();
}

QPair<double,double> EvidenceScorer::getLRAndReliability(const QString& type) const
{
    auto it = m_lrTable.find(type);
    if (it == m_lrTable.end()) return {1.0, 0.0};
    return {it->lr, it->reliability};
}

// ─────────────────────────────────────────────────────────────────────────────
// score(priorProbability, evidencePresence)  — UI convenience overload
// ─────────────────────────────────────────────────────────────────────────────

EvidenceScorer::Result EvidenceScorer::score(double priorProbability,
                                              const QMap<QString,bool>& evidencePresence) const
{
    const double priorOdds = (priorProbability > 0.0 && priorProbability < 1.0)
                             ? priorProbability / (1.0 - priorProbability)
                             : 1.0;
    double runningOdds = priorOdds;
    double overallLR   = 1.0;

    Result result;
    result.contributions.reserve(static_cast<std::size_t>(evidencePresence.size()));

    for (auto it = evidencePresence.constBegin(); it != evidencePresence.constEnd(); ++it) {
        const QString& type = it.key();
        bool present        = it.value();

        auto lrIt = m_lrTable.find(type);
        const double lr = (lrIt != m_lrTable.end()) ? lrIt->lr : 1.0;
        const double effectiveLR = present ? lr : (lr > 1e-12 ? 1.0 / lr : 1.0);

        runningOdds *= effectiveLR;
        overallLR   *= effectiveLR;

        Contribution c;
        c.name            = type;
        c.likelihoodRatio = effectiveLR;
        c.wasPresent      = present;
        result.contributions.push_back(c);
    }

    result.overallLikelihoodRatio  = overallLR;
    result.posteriorProbability    = runningOdds / (1.0 + runningOdds);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// score(evidence, priorProbability)
// ─────────────────────────────────────────────────────────────────────────────

QVector<EvidenceWeight> EvidenceScorer::score(const QVector<EvidenceItem>& evidence,
                                               double priorProbability) const
{
    const double clamped   = std::clamp(priorProbability, 1e-9, 1.0 - 1e-9);
    const double priorOdds = clamped / (1.0 - clamped);

    double runningOdds = priorOdds;
    QVector<EvidenceWeight> results;
    results.reserve(evidence.size());

    for (const auto& item : evidence) {
        auto it = m_lrTable.find(item.type);
        const double lr = (it != m_lrTable.end()) ? it->lr : 1.0;
        const double reliability = (it != m_lrTable.end()) ? it->reliability : 0.0;
        const QString desc = (it != m_lrTable.end())
                             ? it->description
                             : QStringLiteral("Unknown evidence type");

        // Use raw LR for Bayesian update; reliability stored for informational display only
        const double effectiveLR = item.present ? lr : (lr > 1e-12 ? 1.0 / lr : 1.0);
        runningOdds *= effectiveLR;

        const double posteriorProb = runningOdds / (1.0 + runningOdds);

        EvidenceWeight ew;
        ew.evidenceType        = item.type;
        ew.likelihoodRatio     = effectiveLR;
        ew.posteriorOdds       = runningOdds;
        ew.posteriorProbability = posteriorProb;
        ew.reliability         = reliability;
        ew.notes               = item.present
                                 ? desc
                                 : QStringLiteral("Absent: ") + desc;
        results.append(ew);
    }
    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// bayesFactor: how much more strongly the evidence supports H_A over H_B
// ─────────────────────────────────────────────────────────────────────────────

double EvidenceScorer::bayesFactor(double priorA, double priorB,
                                    const QMap<QString,bool>& evidencePresence) const
{
    if (priorA <= 0.0 || priorB <= 0.0) return 1.0;

    // Same evidence map → identical diagnostic LR for both hypotheses.
    // BF = P(E|Ha)/P(E|Hb) = overallLR_A / overallLR_B (prior-independent).
    // Use overall LR directly — the posterior-odds ratio loses precision when
    // posteriors saturate near 1.0 under strong evidence (e.g. DNA LR=1e9).
    const auto resA = score(priorA, evidencePresence);
    const auto resB = score(priorB, evidencePresence);

    const double lrA = resA.overallLikelihoodRatio;
    const double lrB = resB.overallLikelihoodRatio;

    return (lrB > 1e-12) ? lrA / lrB : 1.0;
}
