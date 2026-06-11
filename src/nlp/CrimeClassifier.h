#pragma once
#include <QString>
#include <QMap>
#include <QVector>
#include <QPair>
#include "core/CrimeEvent.h"

class CrimeClassifier {
public:
    CrimeClassifier();

    // Returns (crimeType, confidence) pair
    QPair<QString, double> classify(const QString& text) const;

    // Classify a batch of texts; results are in the same order as input
    QVector<QPair<QString, double>> batchClassify(const QVector<QString>& texts) const;

    // Number of distinct crime-type categories in the keyword corpus
    int corpusSize() const;

    // Severity score 0.0–1.0 based on crime type + high-severity keywords
    double severityScore(const QString& text, const QString& crimeType) const;

    // Sentiment: negative words − positive words, normalised to [-1, 1]
    double sentiment(const QString& text) const;

    // Threat signal: sentimentScore < -0.5 AND at least one threat keyword present
    bool threatSignal(const QString& text, double sentimentScore) const;

private:
    // crime type → list of (keyword, weight) pairs
    QMap<QString, QVector<QPair<QString, double>>> m_keywordMap;

    QVector<QString> m_threatKeywords;
    QVector<QString> m_negativeWords;
    QVector<QString> m_positiveWords;

    // Baseline severity per crime type
    QMap<QString, double> m_severityBase;

    void buildKeywordMap();
};
