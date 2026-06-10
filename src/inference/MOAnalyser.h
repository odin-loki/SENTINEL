#pragma once
#include <QVector>
#include <QMap>
#include <QString>
#include "core/CrimeEvent.h"

// MOCaseRecord and MOMatch are defined in core/CrimeEvent.h

class MOAnalyser {
public:
    MOAnalyser();

    // Build TF-IDF index from case records
    void fit(const QVector<MOCaseRecord>& cases);

    // Find top-k similar cases to query MO string
    QVector<MOMatch> findSimilar(const QString& queryMO,
                                  int topK = 10,
                                  double minSimilarity = 0.3) const;

    bool isFitted() const { return !m_caseRecords.isEmpty(); }
    int caseCount() const { return m_caseRecords.size(); }

private:
    QVector<MOCaseRecord> m_caseRecords;

    QMap<QString, int> m_vocab;           // token → index
    QVector<QVector<double>> m_tfidfMatrix; // [caseIdx][termIdx]
    QVector<double> m_idf;                // inverse document frequency per term
    int m_vocabSize = 0;

    QVector<double> toTFIDF(const QString& text) const;
    QMap<QString, int> tokenFreq(const QString& text) const;
    static double cosineSimilarity(const QVector<double>& a,
                                    const QVector<double>& b);

    void buildIDF(const QVector<MOCaseRecord>& cases);
};
