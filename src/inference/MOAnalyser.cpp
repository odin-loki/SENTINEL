#include "inference/MOAnalyser.h"

#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <algorithm>

MOAnalyser::MOAnalyser() {}

QMap<QString,int> MOAnalyser::tokenFreq(const QString& text) const
{
    QMap<QString,int> freq;
    const QStringList tokens = text.toLower().split(
        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString& tok : tokens)
        freq[tok]++;
    return freq;
}

void MOAnalyser::buildIDF(const QVector<MOCaseRecord>& cases)
{
    const int N = cases.size();

    // Build vocabulary and document frequencies
    QMap<QString, int> df;
    for (const auto& c : cases) {
        const QStringList tokens = c.moText.toLower().split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        QSet<QString> seen;
        for (const QString& tok : tokens) {
            if (!seen.contains(tok)) {
                df[tok]++;
                seen.insert(tok);
            }
        }
    }

    m_vocab.clear();
    int idx = 0;
    for (auto it = df.cbegin(); it != df.cend(); ++it) {
        m_vocab[it.key()] = idx++;
    }
    m_vocabSize = idx;

    m_idf.resize(m_vocabSize);
    for (auto it = df.cbegin(); it != df.cend(); ++it) {
        const int termIdx = m_vocab[it.key()];
        // Smooth IDF (sklearn default): log((N+1)/(df+1)) + 1
        // Always positive; avoids zero-IDF for terms present in all docs.
        m_idf[termIdx] = std::log((static_cast<double>(N) + 1.0) /
                                   (static_cast<double>(it.value()) + 1.0)) + 1.0;
    }
}

QVector<double> MOAnalyser::toTFIDF(const QString& text) const
{
    QVector<double> vec(m_vocabSize, 0.0);
    if (m_vocabSize == 0) return vec;

    const auto freq = tokenFreq(text);
    int totalTerms = 0;
    for (auto it = freq.cbegin(); it != freq.cend(); ++it)
        totalTerms += it.value();
    if (totalTerms == 0) return vec;

    for (auto it = freq.cbegin(); it != freq.cend(); ++it) {
        auto vocabIt = m_vocab.find(it.key());
        if (vocabIt == m_vocab.end()) continue;
        const int tIdx = vocabIt.value();
        const double tf = static_cast<double>(it.value()) /
                          static_cast<double>(totalTerms);
        vec[tIdx] = tf * m_idf[tIdx];
    }
    return vec;
}

double MOAnalyser::cosineSimilarity(const QVector<double>& a,
                                     const QVector<double>& b)
{
    double dot = 0.0, normA = 0.0, normB = 0.0;
    const int n = std::min(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        dot   += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    const double denom = std::sqrt(normA) * std::sqrt(normB);
    return denom > 1e-12 ? dot / denom : 0.0;
}

void MOAnalyser::fit(const QVector<MOCaseRecord>& cases)
{
    m_caseRecords = cases;
    buildIDF(cases);

    m_tfidfMatrix.clear();
    m_tfidfMatrix.reserve(cases.size());
    for (const auto& c : cases)
        m_tfidfMatrix.append(toTFIDF(c.moText));
}

QVector<MOMatch> MOAnalyser::findSimilar(const QString& queryMO,
                                           int topK,
                                           double minSimilarity) const
{
    const QVector<double> queryVec = toTFIDF(queryMO);
    const QStringList queryTokens = queryMO.toLower().split(
        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);

    struct Candidate {
        int idx;
        double boostedScore;
        double rawScore;
    };
    QVector<Candidate> candidates;
    candidates.reserve(m_caseRecords.size());

    for (int i = 0; i < m_caseRecords.size(); ++i) {
        const double sim = cosineSimilarity(queryVec, m_tfidfMatrix[i]);
        if (sim < minSimilarity) continue;
        const double boosted = m_caseRecords[i].resolved
                               ? std::min(sim * 1.2, 1.0)
                               : sim;
        candidates.append({i, boosted, sim});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.boostedScore > b.boostedScore;
              });

    QVector<MOMatch> results;
    const int limit = std::min(topK, static_cast<int>(candidates.size()));
    results.reserve(limit);

    for (int k = 0; k < limit; ++k) {
        const int idx = candidates[k].idx;
        const auto& rec = m_caseRecords[idx];

        // Shared features: tokens present in both query and case MO
        const QStringList caseTokens = rec.moText.toLower().split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        QSet<QString> caseSet(caseTokens.cbegin(), caseTokens.cend());
        QStringList shared;
        for (const QString& tok : queryTokens) {
            if (caseSet.contains(tok))
                shared.append(tok);
        }
        shared.removeDuplicates();

        MOMatch match;
        match.caseId          = rec.caseId;
        match.similarityScore = candidates[k].boostedScore;
        match.sharedFeatures  = shared;
        match.resolved        = rec.resolved;
        match.outcome         = rec.outcome;
        match.suspectProfile  = rec.suspectProfile;
        match.convictionYear  = rec.convictionYear;
        results.append(match);
    }
    return results;
}
