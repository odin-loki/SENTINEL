#include "nlp/CrimeClassifier.h"
#include <QStringList>
#include <QtMath>
#include <algorithm>
#include <limits>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CrimeClassifier::CrimeClassifier()
{
    buildKeywordMap();

    // Baseline severity scores per crime type
    m_severityBase = {
        { "murder",          1.0 },
        { "assault",         0.8 },
        { "robbery",         0.8 },
        { "weapons",         0.9 },
        { "burglary",        0.6 },
        { "drug_offence",    0.5 },
        { "vehicle_crime",   0.4 },
        { "criminal_damage", 0.4 },
        { "theft",           0.3 },
        { "fraud",           0.4 },
        { "public_order",    0.3 },
        { "antisocial",      0.2 },
        { "sexual_offence",  0.9 },
    };

    m_threatKeywords = {
        QStringLiteral("threat"),   QStringLiteral("kill"),      QStringLiteral("attack"),
        QStringLiteral("bomb"),     QStringLiteral("shoot"),     QStringLiteral("stab"),
        QStringLiteral("fire"),     QStringLiteral("destroy"),   QStringLiteral("retaliate"),
        QStringLiteral("hurt"),     QStringLiteral("harm"),      QStringLiteral("murder"),
        QStringLiteral("execute"),
    };

    m_negativeWords = {
        QStringLiteral("violent"),    QStringLiteral("attack"),     QStringLiteral("assault"),
        QStringLiteral("murder"),     QStringLiteral("stab"),       QStringLiteral("shoot"),
        QStringLiteral("kill"),       QStringLiteral("threaten"),   QStringLiteral("harm"),
        QStringLiteral("hurt"),       QStringLiteral("damage"),     QStringLiteral("destroy"),
        QStringLiteral("break"),      QStringLiteral("steal"),      QStringLiteral("rob"),
        QStringLiteral("threat"),     QStringLiteral("aggressive"), QStringLiteral("dangerous"),
        QStringLiteral("weapon"),     QStringLiteral("crime"),      QStringLiteral("criminal"),
        QStringLiteral("drug"),       QStringLiteral("illegal"),    QStringLiteral("forced"),
        QStringLiteral("smashed"),    QStringLiteral("broken"),     QStringLiteral("stolen"),
        QStringLiteral("mugged"),     QStringLiteral("robbed"),     QStringLiteral("stabbed"),
        QStringLiteral("shot"),       QStringLiteral("fired"),      QStringLiteral("bombed"),
        QStringLiteral("looted"),     QStringLiteral("ransacked"),  QStringLiteral("vandalised"),
        QStringLiteral("vandalized"), QStringLiteral("terror"),     QStringLiteral("suspect"),
        QStringLiteral("offender"),   QStringLiteral("perpetrator"),QStringLiteral("abused"),
        QStringLiteral("victim"),     QStringLiteral("wounded"),    QStringLiteral("injured"),
        QStringLiteral("dead"),       QStringLiteral("death"),      QStringLiteral("beaten"),
        QStringLiteral("coerced"),    QStringLiteral("trafficked"),
    };

    m_positiveWords = {
        QStringLiteral("helped"),    QStringLiteral("prevented"),  QStringLiteral("stopped"),
        QStringLiteral("caught"),    QStringLiteral("arrested"),   QStringLiteral("resolved"),
        QStringLiteral("recovered"), QStringLiteral("safe"),       QStringLiteral("clear"),
        QStringLiteral("peaceful"),  QStringLiteral("secured"),    QStringLiteral("protected"),
        QStringLiteral("rescued"),   QStringLiteral("apprehended"),QStringLiteral("convicted"),
        QStringLiteral("sentenced"),
    };
}

void CrimeClassifier::buildKeywordMap()
{
    using KW = QPair<QString, double>;

    m_keywordMap["assault"] = {
        { "attack",    3.0 }, { "assault",   3.0 }, { "hit",      2.0 },
        { "punch",     2.0 }, { "kick",      2.0 }, { "stab",     3.0 },
        { "beat",      2.0 }, { "fight",     2.0 }, { "violence", 2.0 },
        { "victim",    1.0 },
    };

    m_keywordMap["robbery"] = {
        { "robbery",   3.0 }, { "robbed",    3.0 }, { "mugged",   3.0 },
        { "took",      1.0 }, { "stole",     2.0 }, { "threatened", 2.0 },
        { "demanded",  2.0 }, { "weapon",    2.0 },
    };

    m_keywordMap["burglary"] = {
        { "burglar",   3.0 }, { "burglary",  3.0 }, { "broke",    2.0 },
        { "entry",     2.0 }, { "house",     1.0 }, { "home",     1.0 },
        { "stolen",    2.0 }, { "ransacked", 2.0 }, { "forced",   2.0 },
    };

    m_keywordMap["theft"] = {
        { "theft",       3.0 }, { "stolen",     3.0 }, { "pickpocket", 3.0 },
        { "shoplifting", 3.0 }, { "took",       1.0 }, { "missing",    1.0 },
    };

    m_keywordMap["vehicle_crime"] = {
        { "car",       1.0 }, { "vehicle",   1.0 }, { "stolen",    2.0 },
        { "vandalised",2.0 }, { "break-in",  3.0 }, { "window",    1.0 },
        { "tyre",      1.0 },
    };

    m_keywordMap["drug_offence"] = {
        { "drugs",     3.0 }, { "heroin",    3.0 }, { "cocaine",  3.0 },
        { "cannabis",  3.0 }, { "dealing",   3.0 }, { "possession", 2.0 },
    };

    m_keywordMap["criminal_damage"] = {
        { "damage",    2.0 }, { "vandalism", 3.0 }, { "graffiti", 2.0 },
        { "smashed",   2.0 }, { "destroyed", 2.0 }, { "fire",     1.0 },
    };

    m_keywordMap["public_order"] = {
        { "disorder",    2.0 }, { "disturbance", 2.0 }, { "fighting",   2.0 },
        { "drunk",       2.0 }, { "threatening", 2.0 },
    };

    m_keywordMap["weapons"] = {
        { "knife",   3.0 }, { "gun",     3.0 }, { "firearm",  3.0 },
        { "weapon",  2.0 }, { "blade",   3.0 }, { "armed",    3.0 },
    };

    m_keywordMap["fraud"] = {
        { "fraud",         3.0 }, { "scam",         3.0 }, { "fake",        2.0 },
        { "impersonation", 2.0 }, { "identity",     2.0 }, { "phishing",    3.0 },
    };

    m_keywordMap["antisocial"] = {
        { "antisocial", 3.0 }, { "nuisance",  2.0 }, { "noise",      2.0 },
        { "harassment", 2.0 }, { "loitering", 2.0 }, { "abusive",    2.0 },
    };

    m_keywordMap["murder"] = {
        { "murder",    3.0 }, { "killed",   3.0 }, { "dead",      2.0 },
        { "homicide",  3.0 }, { "manslaughter", 3.0 }, { "death",  2.0 },
        { "victim",    1.0 }, { "body",     1.0 }, { "fatal",     2.0 },
    };

    m_keywordMap["sexual_offence"] = {
        { "rape",       3.0 }, { "sexual",    3.0 }, { "indecent",  3.0 },
        { "assault",    1.0 }, { "touching",  2.0 }, { "exposure",  2.0 },
    };
}

// ---------------------------------------------------------------------------
// Tokeniser: split to lowercase words, strip punctuation
// ---------------------------------------------------------------------------
static QStringList tokenise(const QString& text)
{
    const QString lower = text.toLower();
    QStringList tokens;
    QString current;
    for (const QChar ch : lower) {
        if (ch.isLetter() || ch == QLatin1Char('-')) {
            current += ch;
        } else {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
        }
    }
    if (!current.isEmpty()) tokens.append(current);
    return tokens;
}

// ---------------------------------------------------------------------------
// classify()
// ---------------------------------------------------------------------------
QPair<QString, double> CrimeClassifier::classify(const QString& text) const
{
    const QStringList tokens = tokenise(text);
    const double textLen = static_cast<double>(std::max(tokens.size(), qsizetype{1}));

    QMap<QString, double> scores;
    for (auto it = m_keywordMap.constBegin(); it != m_keywordMap.constEnd(); ++it) {
        double score = 0.0;
        for (const auto& [kw, weight] : it.value()) {
            if (tokens.contains(kw)) score += weight;
        }
        scores[it.key()] = score / textLen;
    }

    // Find best and second-best
    QString bestType;
    double best  = 0.0;
    double second = 0.0;
    for (auto it = scores.constBegin(); it != scores.constEnd(); ++it) {
        if (it.value() > best) {
            second = best;
            best   = it.value();
            bestType = it.key();
        } else if (it.value() > second) {
            second = it.value();
        }
    }

    if (bestType.isEmpty()) {
        return { QStringLiteral("unknown"), 0.0 };
    }

    constexpr double epsilon = 1e-9;
    const double confidence = best / (best + second + epsilon);
    return { bestType, confidence };
}

// ---------------------------------------------------------------------------
// severityScore()
// ---------------------------------------------------------------------------
double CrimeClassifier::severityScore(const QString& text, const QString& crimeType) const
{
    double base = m_severityBase.value(crimeType, 0.3);

    // Boost for high-severity keywords found in text
    const QStringList tokens = tokenise(text);
    const QStringList boostWords = {
        QStringLiteral("murder"), QStringLiteral("kill"), QStringLiteral("dead"),
        QStringLiteral("death"),  QStringLiteral("gun"),  QStringLiteral("firearm"),
        QStringLiteral("bomb"),   QStringLiteral("stab"), QStringLiteral("armed"),
    };
    int boostCount = 0;
    for (const QString& bw : boostWords) {
        if (tokens.contains(bw)) ++boostCount;
    }
    const double boost = std::min(boostCount * 0.05, 0.2);
    return std::min(base + boost, 1.0);
}

// ---------------------------------------------------------------------------
// sentiment()
// ---------------------------------------------------------------------------
double CrimeClassifier::sentiment(const QString& text) const
{
    const QStringList tokens = tokenise(text);
    int negCount = 0, posCount = 0;

    for (const QString& token : tokens) {
        if (m_negativeWords.contains(token)) ++negCount;
        if (m_positiveWords.contains(token)) ++posCount;
    }

    const double total = static_cast<double>(negCount + posCount);
    if (total < 0.5) return 0.0; // no signal

    constexpr double epsilon = 1e-9;
    // positive-minus-negative, normalised — crime text is almost always negative
    return static_cast<double>(posCount - negCount) / (total + epsilon);
}

// ---------------------------------------------------------------------------
// threatSignal()
// ---------------------------------------------------------------------------
bool CrimeClassifier::threatSignal(const QString& text, double sentimentScore) const
{
    if (sentimentScore >= -0.5) return false;  // must be strictly below −0.5

    const QStringList tokens = tokenise(text);
    for (const QString& kw : m_threatKeywords) {
        if (tokens.contains(kw)) return true;
    }
    return false;
}
