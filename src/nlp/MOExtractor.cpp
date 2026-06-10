#include "nlp/MOExtractor.h"
#include <QStringList>
#include <regex>
#include <sstream>
#include <algorithm>

static constexpr auto ICASE = std::regex::icase | std::regex::ECMAScript;

MOExtractor::MOExtractor()
    : m_itemsPattern(R"(\b(cash|jewellery|jewelry|phone|laptop|wallet|keys|handbag|watch|electronics|tools|purse|bag|card|bicycle|bike|tablet|passport|documents|clothing|jewels|necklace|ring|camera)\b)", ICASE)
    , m_soloPattern(R"(\b(alone|solo|lone|single|by himself|by herself)\b)", ICASE)
    , m_groupPattern(R"(\b(group|gang|multiple|several|two|three|four|pair)\b)", ICASE)
{
    // Entry method patterns
    m_entryPatterns = {
        { "forced_entry", std::regex(R"(\b(smashed|broke|forced|pried|kicked|jimmied|broken)\b)", ICASE) },
        { "unlocked",     std::regex(R"(\b(unlocked|open|unsecured|left open)\b)",               ICASE) },
        { "deception",    std::regex(R"(\b(pretended|posed as|impersonated|tricked|deceived)\b)", ICASE) },
        { "tailgating",   std::regex(R"(\b(tailgated|followed|piggybacked)\b)",                  ICASE) },
    };

    // Target type patterns
    m_targetPatterns = {
        { "residential", std::regex(R"(\b(house|home|flat|apartment|unit|residence|dwelling|property)\b)", ICASE) },
        { "commercial",  std::regex(R"(\b(shop|store|office|warehouse|factory|business|premises)\b)",      ICASE) },
        { "vehicle",     std::regex(R"(\b(car|vehicle|truck|van|motorcycle|bike|auto)\b)",                 ICASE) },
        { "person",      std::regex(R"(\b(person|pedestrian|victim|woman|man|child|individual)\b)",        ICASE) },
    };

    // Time-of-day patterns
    m_timePatterns = {
        { "early_morning", std::regex(R"(\b(1am|2am|3am|4am|5am|early morning|overnight|before dawn)\b)", ICASE) },
        { "morning",       std::regex(R"(\b(6am|7am|8am|9am|10am|11am|morning)\b)",                       ICASE) },
        { "afternoon",     std::regex(R"(\b(12pm|1pm|2pm|3pm|4pm|5pm|afternoon|midday)\b)",               ICASE) },
        { "evening",       std::regex(R"(\b(6pm|7pm|8pm|9pm|evening|dusk|sunset)\b)",                     ICASE) },
        { "night",         std::regex(R"(\b(10pm|11pm|midnight|night|dark|late night)\b)",                 ICASE) },
    };

    // Weapon patterns
    m_weaponPatterns = {
        { "firearm", std::regex(R"(\b(gun|pistol|rifle|firearm|shotgun|revolver|handgun)\b)",  ICASE) },
        { "knife",   std::regex(R"(\b(knife|blade|stabbed|slashed|cut|machete|sword)\b)",      ICASE) },
        { "blunt",   std::regex(R"(\b(bat|hammer|club|pipe|rod|crowbar|wrench)\b)",            ICASE) },
        { "other",   std::regex(R"(\b(acid|explosive|taser|spray|chemical|fire)\b)",           ICASE) },
    };

    // Victim profile patterns
    m_victimPatterns = {
        { "elderly",    std::regex(R"(\b(elderly|old|senior|pensioner|retired|aged)\b)",    ICASE) },
        { "child",      std::regex(R"(\b(child|children|minor|juvenile|teenager|youth)\b)", ICASE) },
        { "female",     std::regex(R"(\b(woman|female|girl|lady|her|she)\b)",               ICASE) },
        { "male",       std::regex(R"(\b(man|male|boy|gentleman|him|he)\b)",                ICASE) },
        { "business",   std::regex(R"(\b(shop|business|owner|manager|staff|employee)\b)",  ICASE) },
        { "vulnerable", std::regex(R"(\b(vulnerable|disabled|homeless|isolated)\b)",       ICASE) },
    };
}

// ---------------------------------------------------------------------------
// Internal: return the label of the first pattern that matches
// ---------------------------------------------------------------------------
std::optional<QString> MOExtractor::matchFirst(
    const std::string& text,
    const std::vector<std::pair<std::string, std::regex>>& patterns) const
{
    for (const auto& [label, re] : patterns) {
        if (std::regex_search(text, re)) {
            return QString::fromStdString(label);
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Main extraction
// ---------------------------------------------------------------------------
MOFeatures MOExtractor::extract(const QString& text) const
{
    // Truncate to 4096 chars to keep regex searches O(1) for very long inputs.
    const std::string stdText = text.left(4096).toStdString();
    MOFeatures mo;

    mo.entryMethod   = matchFirst(stdText, m_entryPatterns);
    mo.targetType    = matchFirst(stdText, m_targetPatterns);
    mo.timeOfDay     = matchFirst(stdText, m_timePatterns);
    mo.weaponType    = matchFirst(stdText, m_weaponPatterns);
    mo.victimProfile = matchFirst(stdText, m_victimPatterns);

    // Items taken: collect all unique matches
    {
        auto it  = std::sregex_iterator(stdText.begin(), stdText.end(), m_itemsPattern);
        auto end = std::sregex_iterator{};
        std::vector<QString> items;
        for (; it != end; ++it) {
            QString item = QString::fromStdString((*it)[1].str()).toLower();
            // Normalise british/american spelling
            if (item == QStringLiteral("jewelry")) item = QStringLiteral("jewellery");
            if (std::find(items.begin(), items.end(), item) == items.end()) {
                items.push_back(item);
            }
        }
        mo.itemsTaken = std::move(items);
    }

    // Solo or group
    const bool isSolo  = std::regex_search(stdText, m_soloPattern);
    const bool isGroup = std::regex_search(stdText, m_groupPattern);
    if (isSolo && !isGroup)
        mo.soloOrGroup = QStringLiteral("solo");
    else if (isGroup && !isSolo)
        mo.soloOrGroup = QStringLiteral("group");
    else if (isSolo && isGroup)
        mo.soloOrGroup = QStringLiteral("group"); // group takes precedence when ambiguous

    return mo;
}

// ---------------------------------------------------------------------------
// Canonical MO string
// ---------------------------------------------------------------------------
QString MOExtractor::canonicalMOString(const MOFeatures& mo) const
{
    QStringList parts;

    if (mo.entryMethod)  parts << *mo.entryMethod;
    if (mo.targetType)   parts << *mo.targetType;
    if (mo.timeOfDay)    parts << *mo.timeOfDay;
    if (mo.weaponType)   parts << *mo.weaponType;
    for (const auto& item : mo.itemsTaken) parts << item;
    if (mo.soloOrGroup)  parts << *mo.soloOrGroup;
    if (mo.victimProfile) parts << *mo.victimProfile;

    return parts.join(QLatin1Char(' '));
}
