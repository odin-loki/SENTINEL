#pragma once
#include <QString>
#include <regex>
#include <vector>
#include <utility>
#include <optional>
#include "core/CrimeEvent.h"

class MOExtractor {
public:
    MOExtractor();

    MOFeatures extract(const QString& text) const;

    // Produce canonical MO string for similarity analysis
    // e.g. "forced_entry residential night cash jewellery solo"
    QString canonicalMOString(const MOFeatures& mo) const;

private:
    std::vector<std::pair<std::string, std::regex>> m_entryPatterns;
    std::vector<std::pair<std::string, std::regex>> m_targetPatterns;
    std::vector<std::pair<std::string, std::regex>> m_timePatterns;
    std::vector<std::pair<std::string, std::regex>> m_weaponPatterns;
    std::vector<std::pair<std::string, std::regex>> m_victimPatterns;
    std::vector<std::pair<std::string, std::regex>> m_precautionPatterns;
    std::regex m_itemsPattern;
    std::regex m_soloPattern;
    std::regex m_groupPattern;

    std::optional<QString> matchFirst(
        const std::string& text,
        const std::vector<std::pair<std::string, std::regex>>& patterns) const;
};
