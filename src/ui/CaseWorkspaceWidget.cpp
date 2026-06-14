#include "ui/CaseWorkspaceWidget.h"
#include "ui/GeoProfileHeatmapWidget.h"
#include "models/SeriesDetector.h"
#include "inference/HintEngine.h"
#include "inference/GeographicProfiler.h"
#include "inference/LeadReportGenerator.h"
#include "core/DataExporter.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFont>
#include <QJsonValue>
#include <QJsonDocument>
#include <QSet>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QTimeZone>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QInputDialog>
#include <limits>
#include <cstdlib>

namespace {

QString dominantCrimeTypeForMembers(const QVector<SeriesEvent>& members)
{
    QMap<QString, int> typeCounts;
    for (const SeriesEvent& ev : members)
        typeCounts[ev.crimeType]++;
    QString dominant;
    int maxCount = 0;
    for (auto it = typeCounts.constBegin(); it != typeCounts.constEnd(); ++it) {
        if (it.value() > maxCount) {
            maxCount = it.value();
            dominant = it.key();
        }
    }
    return dominant;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
CaseWorkspaceWidget::CaseWorkspaceWidget(std::shared_ptr<Database> db, QWidget* parent)
    : QWidget(parent)
    , m_db(std::move(db))
    , m_caseIdEdit(nullptr)
    , m_filterBtn(nullptr)
    , m_exportBtn(nullptr)
    , m_eventsTable(nullptr)
    , m_leadsTable(nullptr)
    , m_seriesOverrideTable(nullptr)
    , m_seriesMergeBtn(nullptr)
    , m_seriesSplitBtn(nullptr)
    , m_seriesResetBtn(nullptr)
    , m_seriesCountLabel(nullptr)
    , m_geoProfileLabel(nullptr)
    , m_eventCountLabel(nullptr)
{
    setupUI();
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::setupUI()
{
    setStyleSheet(R"(
        QLineEdit {
            background-color: #1a2035;
            color: #eaeaea;
            border: 1px solid #0f3460;
            border-radius: 4px;
            padding: 6px 10px;
        }
        QLineEdit:focus { border-color: #e94560; }
        QPushButton {
            background-color: #0f3460;
            color: #eaeaea;
            border: none;
            border-radius: 4px;
            padding: 6px 16px;
        }
        QPushButton:hover { background-color: #e94560; }
        QTableWidget {
            background-color: #16213e;
            color: #eaeaea;
            gridline-color: #1a2a4a;
            border: none;
        }
        QTableWidget::item { padding: 4px 8px; }
        QTableWidget::item:selected { background-color: #1a2a4a; color: #4fc3f7; }
        QHeaderView::section {
            background-color: #0f3460;
            color: #a0a8b8;
            padding: 6px 8px;
            border: none;
            font-size: 11px;
            letter-spacing: 1px;
        }
        QGroupBox {
            border: 1px solid #1a2a4a;
            border-radius: 8px;
            margin-top: 8px;
            padding: 10px;
            background-color: #16213e;
        }
        QGroupBox::title {
            color: #a0a8b8;
            subcontrol-origin: margin;
            left: 12px;
            font-size: 11px;
            letter-spacing: 1px;
        }
        QCheckBox { color: #eaeaea; spacing: 6px; }
        QCheckBox::indicator {
            width: 14px; height: 14px; border-radius: 3px;
            border: 1px solid #0f3460; background: #1a2035;
        }
        QCheckBox::indicator:checked { background: #e94560; border-color: #e94560; }
    )");

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(12);

    auto* titleRow = new QHBoxLayout();
    auto* titleLbl = new QLabel(QStringLiteral("Case Workspace"), this);
    titleLbl->setStyleSheet(QStringLiteral("color: #eaeaea; font-size: 22px; font-weight: bold;"));
    m_eventCountLabel = new QLabel(QStringLiteral("0 events"), this);
    m_eventCountLabel->setObjectName(QStringLiteral("caseEventCountLabel"));
    m_eventCountLabel->setStyleSheet(QStringLiteral("color: #4a5568; font-size: 12px;"));
    titleRow->addWidget(titleLbl);
    titleRow->addSpacing(16);
    titleRow->addWidget(m_eventCountLabel);
    titleRow->addStretch();
    outerLayout->addLayout(titleRow);

    auto* filterRow = new QHBoxLayout();
    auto* filterLbl = new QLabel(QStringLiteral("Case ID:"), this);
    filterLbl->setStyleSheet(QStringLiteral("color: #a0a8b8;"));
    m_caseIdEdit = new QLineEdit(this);
    m_caseIdEdit->setObjectName(QStringLiteral("caseIdFilter"));
    m_caseIdEdit->setPlaceholderText(QStringLiteral("e.g. case_HX366462 or event prefix"));
    m_caseIdEdit->setMinimumWidth(280);
    m_filterBtn = new QPushButton(QStringLiteral("Filter"), this);
    m_filterBtn->setObjectName(QStringLiteral("caseFilterBtn"));
    filterRow->addWidget(filterLbl);
    filterRow->addWidget(m_caseIdEdit, 1);
    filterRow->addWidget(m_filterBtn);
    m_exportBtn = new QPushButton(QStringLiteral("Export Case Report"), this);
    m_exportBtn->setObjectName(QStringLiteral("caseExportBtn"));
    filterRow->addWidget(m_exportBtn);
    outerLayout->addLayout(filterRow);

    m_eventsTable = new QTableWidget(0, 5, this);
    m_eventsTable->setObjectName(QStringLiteral("caseEventsTable"));
    m_eventsTable->setHorizontalHeaderLabels({
        QStringLiteral("Event ID"),
        QStringLiteral("Crime Type"),
        QStringLiteral("Date"),
        QStringLiteral("Location"),
        QStringLiteral("Quality")
    });
    m_eventsTable->horizontalHeader()->setStretchLastSection(true);
    m_eventsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_eventsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_eventsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_eventsTable->verticalHeader()->setVisible(false);
    outerLayout->addWidget(m_eventsTable, 1);

    auto* summaryRow = new QHBoxLayout();

    auto* seriesBox = new QGroupBox(QStringLiteral("Series Detection"), this);
    auto* seriesLayout = new QVBoxLayout(seriesBox);
    m_seriesCountLabel = new QLabel(QStringLiteral("Series count: —"), seriesBox);
    m_seriesCountLabel->setObjectName(QStringLiteral("seriesCountLabel"));
    m_seriesCountLabel->setStyleSheet(QStringLiteral("color: #4fc3f7; font-size: 14px;"));
    seriesLayout->addWidget(m_seriesCountLabel);

    m_seriesOverrideTable = new QTableWidget(0, 5, seriesBox);
    m_seriesOverrideTable->setObjectName(QStringLiteral("seriesOverrideTable"));
    m_seriesOverrideTable->setHorizontalHeaderLabels({
        QStringLiteral("Select"),
        QStringLiteral("Series ID"),
        QStringLiteral("Event count"),
        QStringLiteral("Dominant type"),
        QStringLiteral("Actions")
    });
    m_seriesOverrideTable->horizontalHeader()->setStretchLastSection(true);
    m_seriesOverrideTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_seriesOverrideTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_seriesOverrideTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_seriesOverrideTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_seriesOverrideTable->verticalHeader()->setVisible(false);
    m_seriesOverrideTable->setMaximumHeight(160);
    seriesLayout->addWidget(m_seriesOverrideTable);

    auto* seriesBtnRow = new QHBoxLayout();
    m_seriesMergeBtn = new QPushButton(QStringLiteral("Merge Selected"), seriesBox);
    m_seriesMergeBtn->setObjectName(QStringLiteral("seriesMergeBtn"));
    m_seriesSplitBtn = new QPushButton(QStringLiteral("Split Event"), seriesBox);
    m_seriesSplitBtn->setObjectName(QStringLiteral("seriesSplitBtn"));
    m_seriesResetBtn = new QPushButton(QStringLiteral("Reset Overrides"), seriesBox);
    m_seriesResetBtn->setObjectName(QStringLiteral("seriesResetBtn"));
    seriesBtnRow->addWidget(m_seriesMergeBtn);
    seriesBtnRow->addWidget(m_seriesSplitBtn);
    seriesBtnRow->addWidget(m_seriesResetBtn);
    seriesBtnRow->addStretch();
    seriesLayout->addLayout(seriesBtnRow);
    summaryRow->addWidget(seriesBox);

    auto* geoBox = new QGroupBox(QStringLiteral("Geographic Profile"), this);
    auto* geoLayout = new QVBoxLayout(geoBox);
    m_geoProfileLabel = new QLabel(geoBox);
    m_geoProfileLabel->setObjectName(QStringLiteral("geoProfilePlaceholder"));
    m_geoProfileLabel->setWordWrap(true);
    m_geoProfileLabel->setStyleSheet(QStringLiteral("color: #a0a8b8; font-size: 12px;"));
    m_geoProfileLabel->setText(
        QStringLiteral("Geographic profile summary will appear here once ≥3 geo-tagged events "
                       "are linked to this case."));
    geoLayout->addWidget(m_geoProfileLabel);
    m_geoHeatmap = new GeoProfileHeatmapWidget(geoBox);
    m_geoHeatmap->setObjectName(QStringLiteral("geoProfileHeatmap"));
    m_geoHeatmap->hide();
    geoLayout->addWidget(m_geoHeatmap);
    summaryRow->addWidget(geoBox, 1);

    outerLayout->addLayout(summaryRow);

    auto* leadsBox = new QGroupBox(QStringLiteral("Lead History"), this);
    auto* leadsLayout = new QVBoxLayout(leadsBox);
    m_leadsTable = new QTableWidget(0, 4, leadsBox);
    m_leadsTable->setObjectName(QStringLiteral("caseLeadsTable"));
    m_leadsTable->setHorizontalHeaderLabels({
        QStringLiteral("Rank"),
        QStringLiteral("Category"),
        QStringLiteral("Headline"),
        QStringLiteral("Confidence")
    });
    m_leadsTable->horizontalHeader()->setStretchLastSection(true);
    m_leadsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_leadsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_leadsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_leadsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_leadsTable->verticalHeader()->setVisible(false);
    m_leadsTable->setMaximumHeight(180);
    leadsLayout->addWidget(m_leadsTable);
    outerLayout->addWidget(leadsBox);

    connect(m_filterBtn, &QPushButton::clicked, this, &CaseWorkspaceWidget::onFilterChanged);
    connect(m_caseIdEdit, &QLineEdit::returnPressed, this, &CaseWorkspaceWidget::onFilterChanged);
    connect(m_exportBtn, &QPushButton::clicked, this, &CaseWorkspaceWidget::onExportCaseReport);
    connect(m_seriesMergeBtn, &QPushButton::clicked, this, &CaseWorkspaceWidget::onMergeSelectedSeries);
    connect(m_seriesSplitBtn, &QPushButton::clicked, this, &CaseWorkspaceWidget::onSplitSeriesEvent);
    connect(m_seriesResetBtn, &QPushButton::clicked, this, &CaseWorkspaceWidget::onResetSeriesOverrides);
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<CrimeEvent> CaseWorkspaceWidget::loadCaseEvents(const QString& caseId) const
{
    if (caseId.trimmed().isEmpty())
        return m_db->getRecentEvents(100);

    const QString needle = caseId.trimmed();
    QVector<CrimeEvent> matched;
    QSet<QString> seen;

    const auto fromSearch = m_db->queryEvents(
        QString{}, QDate{}, QDate{}, needle, 5000);
    for (const CrimeEvent& ev : fromSearch) {
        if (!seen.contains(ev.eventId)) {
            seen.insert(ev.eventId);
            matched.append(ev);
        }
    }

    const QVector<CrimeEvent> recent = m_db->getRecentEvents(2000);
    for (const CrimeEvent& ev : recent) {
        const QString metaCase = ev.meta.value(QStringLiteral("caseId")).toString();
        if (ev.eventId.contains(needle, Qt::CaseInsensitive)
            || metaCase.contains(needle, Qt::CaseInsensitive)) {
            if (!seen.contains(ev.eventId)) {
                seen.insert(ev.eventId);
                matched.append(ev);
            }
        }
    }

    return matched;
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::populateEventsTable(const QVector<CrimeEvent>& events)
{
    m_eventsTable->setRowCount(0);
    for (const CrimeEvent& ev : events) {
        const int row = m_eventsTable->rowCount();
        m_eventsTable->insertRow(row);

        const QString dateStr = ev.occurredAt.has_value()
            ? ev.occurredAt->toString(QStringLiteral("dd MMM yyyy HH:mm"))
            : QStringLiteral("—");
        const QString loc = ev.locationRaw.value_or(ev.suburb);

        m_eventsTable->setItem(row, 0, new QTableWidgetItem(ev.eventId));
        m_eventsTable->setItem(row, 1, new QTableWidgetItem(ev.crimeType));
        m_eventsTable->setItem(row, 2, new QTableWidgetItem(dateStr));
        m_eventsTable->setItem(row, 3, new QTableWidgetItem(loc));
        m_eventsTable->setItem(row, 4,
            new QTableWidgetItem(QString::number(ev.qualityScore, 'f', 2)));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<CrimeSeries> CaseWorkspaceWidget::detectSeriesFromEvents(
    const QVector<CrimeEvent>& events) const
{
    SeriesDetector detector(0.5, 3.0, 3);
    return detector.detect(events);
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<CrimeSeries> CaseWorkspaceWidget::activeSeriesList(
    const QVector<CrimeEvent>& events) const
{
    if (!m_seriesOverrides.isEmpty())
        return m_seriesOverrides;
    return detectSeriesFromEvents(events);
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::recomputeSeriesMetadata(CrimeSeries& series) const
{
    if (series.members.isEmpty())
        return;

    std::sort(series.members.begin(), series.members.end(),
              [](const SeriesEvent& a, const SeriesEvent& b) {
                  return a.tDays < b.tDays;
              });

    double sumLat = 0.0;
    double sumLon = 0.0;
    double minT = std::numeric_limits<double>::max();
    double maxT = std::numeric_limits<double>::lowest();
    for (const SeriesEvent& ev : series.members) {
        sumLat += ev.lat;
        sumLon += ev.lon;
        minT = std::min(minT, ev.tDays);
        maxT = std::max(maxT, ev.tDays);
    }

    series.centroidLat = sumLat / series.members.size();
    series.centroidLon = sumLon / series.members.size();
    series.firstDays = minT;
    series.lastDays = maxT;
    series.dominantCrimeType = dominantCrimeTypeForMembers(series.members);
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::populateSeriesTable(const QVector<CrimeSeries>& series)
{
    m_displayedSeries = series;
    m_seriesOverrideTable->setRowCount(0);

    for (int i = 0; i < series.size(); ++i) {
        const CrimeSeries& s = series.at(i);
        const int row = m_seriesOverrideTable->rowCount();
        m_seriesOverrideTable->insertRow(row);

        auto* cb = new QCheckBox(m_seriesOverrideTable);
        cb->setObjectName(QStringLiteral("seriesSelectCheck_%1").arg(row));
        m_seriesOverrideTable->setCellWidget(row, 0, cb);

        m_seriesOverrideTable->setItem(row, 1, new QTableWidgetItem(s.seriesId));
        m_seriesOverrideTable->setItem(row, 2,
            new QTableWidgetItem(QString::number(s.members.size())));
        m_seriesOverrideTable->setItem(row, 3,
            new QTableWidgetItem(s.dominantCrimeType));

        auto* splitBtn = new QPushButton(QStringLiteral("Split…"), m_seriesOverrideTable);
        splitBtn->setObjectName(QStringLiteral("seriesRowSplitBtn_%1").arg(row));
        connect(splitBtn, &QPushButton::clicked, this, [this, row]() {
            onSplitSeriesRow(row);
        });
        m_seriesOverrideTable->setCellWidget(row, 4, splitBtn);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::updateSeriesDetection(const QVector<CrimeEvent>& events)
{
    m_lastSeriesCount = 0;
    if (events.size() < 3) {
        m_displayedSeries.clear();
        m_seriesOverrideTable->setRowCount(0);
        m_seriesCountLabel->setText(
            QStringLiteral("Series count: 0  (need ≥3 events for detection)"));
        return;
    }

    const QVector<CrimeSeries> series = activeSeriesList(events);
    m_lastSeriesCount = series.size();

    const bool overridden = !m_seriesOverrides.isEmpty();
    m_seriesCountLabel->setText(
        QStringLiteral("Series count: %1  (%2 events analysed%3)")
            .arg(series.size())
            .arg(events.size())
            .arg(overridden ? QStringLiteral(", analyst override") : QString()));

    populateSeriesTable(series);
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::applySeriesOverridesAndRefresh()
{
    updateSeriesDetection(m_lastEvents);

    const QVector<InvestigativeLead> allLeads = generateLeadsForCase(m_lastEvents);
    m_lastLeads = filterLeadsToCaseEvents(allLeads, m_lastEvents);
    populateLeadHistory(m_lastLeads);
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<int> CaseWorkspaceWidget::checkedSeriesRows() const
{
    QVector<int> rows;
    for (int row = 0; row < m_seriesOverrideTable->rowCount(); ++row) {
        const auto* cb = qobject_cast<QCheckBox*>(m_seriesOverrideTable->cellWidget(row, 0));
        if (cb && cb->isChecked())
            rows.append(row);
    }
    return rows;
}

// ─────────────────────────────────────────────────────────────────────────────
int CaseWorkspaceWidget::selectedSeriesRowForSplit() const
{
    const QVector<int> checked = checkedSeriesRows();
    if (checked.size() == 1)
        return checked.first();

    const QModelIndexList selected = m_seriesOverrideTable->selectionModel()->selectedRows();
    if (selected.size() == 1)
        return selected.first().row();

    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::onMergeSelectedSeries()
{
    if (m_lastEvents.size() < 3)
        return;

    const QVector<int> selectedRows = checkedSeriesRows();
    if (selectedRows.size() < 2) {
        QMessageBox::information(this, QStringLiteral("Merge Series"),
            QStringLiteral("Select at least two series using the checkboxes, then click Merge Selected."));
        return;
    }

    const QVector<CrimeSeries> current = activeSeriesList(m_lastEvents);
    CrimeSeries merged;
    merged.seriesId = QStringLiteral("MERGED-%1")
                          .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmss")));

    QSet<QString> seenEventIds;
    QSet<int> selectedSet(selectedRows.cbegin(), selectedRows.cend());
    QVector<CrimeSeries> remaining;
    remaining.reserve(current.size() - selectedRows.size() + 1);

    for (int i = 0; i < current.size(); ++i) {
        if (selectedSet.contains(i)) {
            for (const SeriesEvent& member : current.at(i).members) {
                if (seenEventIds.contains(member.eventId))
                    continue;
                seenEventIds.insert(member.eventId);
                merged.members.append(member);
            }
        } else {
            remaining.append(current.at(i));
        }
    }

    recomputeSeriesMetadata(merged);
    remaining.append(merged);

    m_seriesOverrides = remaining;
    applySeriesOverridesAndRefresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::onSplitSeriesRow(int row)
{
    if (row < 0 || row >= m_displayedSeries.size())
        return;

    const CrimeSeries& source = m_displayedSeries.at(row);
    if (source.members.size() < 2) {
        QMessageBox::information(this, QStringLiteral("Split Event"),
            QStringLiteral("Choose a series with at least two events to split one out."));
        return;
    }

    QStringList eventIds;
    for (const SeriesEvent& member : source.members)
        eventIds.append(member.eventId);

    const bool headless = qEnvironmentVariableIsSet("SENTINEL_HEADLESS_TEST");
    QString chosenId;
    if (headless) {
        chosenId = eventIds.first();
    } else {
        bool ok = false;
        chosenId = QInputDialog::getItem(
            this,
            QStringLiteral("Split Event"),
            QStringLiteral("Event to move into its own series:"),
            eventIds,
            0,
            false,
            &ok);
        if (!ok || chosenId.isEmpty())
            return;
    }

    QVector<CrimeSeries> updated = activeSeriesList(m_lastEvents);
    if (row >= updated.size())
        return;

    CrimeSeries& target = updated[row];
    SeriesEvent splitMember;
    bool found = false;
    for (int i = 0; i < target.members.size(); ++i) {
        if (target.members.at(i).eventId == chosenId) {
            splitMember = target.members.at(i);
            target.members.removeAt(i);
            found = true;
            break;
        }
    }
    if (!found)
        return;

    recomputeSeriesMetadata(target);

    CrimeSeries singleton;
    singleton.seriesId = QStringLiteral("SPLIT-%1").arg(splitMember.eventId);
    singleton.members.append(splitMember);
    recomputeSeriesMetadata(singleton);

    updated.append(singleton);
    m_seriesOverrides = updated;
    applySeriesOverridesAndRefresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::onSplitSeriesEvent()
{
    const int row = selectedSeriesRowForSplit();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("Split Event"),
            QStringLiteral("Select exactly one series (checkbox or row), then choose an event to split out."));
        return;
    }
    onSplitSeriesRow(row);
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::onResetSeriesOverrides()
{
    m_seriesOverrides.clear();
    applySeriesOverridesAndRefresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::updateGeoProfileSummary(const QVector<CrimeEvent>& events)
{
    QVector<QPair<double, double>> locs;
    for (const CrimeEvent& ev : events) {
        if (ev.lat.has_value() && ev.lon.has_value())
            locs.append(qMakePair(*ev.lat, *ev.lon));
    }

    if (locs.size() < 3) {
        m_geoProfileLabel->setText(
            QStringLiteral("Geographic profile summary will appear here once ≥3 geo-tagged events "
                           "are linked to this case. (%1/%2 geo-tagged)")
                .arg(locs.size())
                .arg(events.size()));
        m_geoProfileLabel->setStyleSheet(QStringLiteral("color: #a0a8b8; font-size: 12px;"));
        if (m_geoHeatmap) {
            m_geoHeatmap->clear();
            m_geoHeatmap->hide();
        }
        return;
    }

    const GeographicProfile profile = GeographicProfiler().profile(locs);
    m_geoProfileLabel->setText(
        QStringLiteral("Peak anchor: %1, %2  |  50%% search area: %3 km²  |  80%% search area: %4 km²")
            .arg(profile.peakLat, 0, 'f', 5)
            .arg(profile.peakLon, 0, 'f', 5)
            .arg(profile.searchArea50pct, 0, 'f', 2)
            .arg(profile.searchArea80pct, 0, 'f', 2));
    m_geoProfileLabel->setStyleSheet(QStringLiteral("color: #81c784; font-size: 12px;"));
    if (m_geoHeatmap) {
        m_geoHeatmap->setProfile(profile);
        m_geoHeatmap->setCrimeLocations(locs);
        m_geoHeatmap->show();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<InvestigativeLead> CaseWorkspaceWidget::generateLeadsForCase(
    const QVector<CrimeEvent>& events) const
{
    if (events.isEmpty())
        return {};

    static const QDateTime kEpoch(QDate(2024, 1, 1), QTime(0, 0, 0), QTimeZone::utc());

    SeriesDetector detector(0.5, 3.0, 3);
    const QVector<CrimeSeries> seriesList = activeSeriesList(events);

    const CrimeEvent& anchor = events.first();
    SeriesEvent probe;
    probe.eventId   = anchor.eventId;
    probe.lat       = anchor.lat.value_or(51.5);
    probe.lon       = anchor.lon.value_or(-0.1);
    probe.tDays     = kEpoch.daysTo(anchor.occurredAt.value_or(kEpoch));
    probe.crimeType = anchor.crimeType;
    probe.moText    = anchor.narrative.value_or(anchor.crimeType);

    QVector<SeriesMatch> seriesMatches;
    for (const CrimeSeries& series : seriesList) {
        if (series.members.isEmpty())
            continue;
        const double moSim = SeriesDetector::moJaccard(probe.moText, series.members.first().moText);
        SeriesMatch sm = detector.linkProbability(probe, series, moSim);
        sm.method = QStringLiteral("spatiotemporal_dbscan");
        if (sm.linkProbability >= 0.2)
            seriesMatches.append(sm);
    }

    QVector<QPair<double, double>> locs;
    for (const CrimeEvent& ev : events) {
        if (ev.lat.has_value() && ev.lon.has_value())
            locs.append(qMakePair(*ev.lat, *ev.lon));
    }

    HintEngineInput input;
    input.event = anchor;
    input.seriesMatches = seriesMatches;
    if (locs.size() >= 3)
        input.geoProfile = GeographicProfiler().profile(locs);

    double qualitySum = 0.0;
    for (const CrimeEvent& ev : events)
        qualitySum += ev.qualityScore;
    input.dataQuality = qualitySum / events.size();

    return HintEngine().generate(input);
}

// ─────────────────────────────────────────────────────────────────────────────
QVector<InvestigativeLead> CaseWorkspaceWidget::filterLeadsToCaseEvents(
    const QVector<InvestigativeLead>& leads,
    const QVector<CrimeEvent>& events) const
{
    QSet<QString> caseIds;
    for (const CrimeEvent& ev : events)
        caseIds.insert(ev.eventId);

    QVector<InvestigativeLead> filtered;
    filtered.reserve(leads.size());
    for (const InvestigativeLead& lead : leads) {
        if (!lead.relatedEventIds.isEmpty()) {
            bool relates = false;
            for (const QString& id : lead.relatedEventIds) {
                if (caseIds.contains(id)) {
                    relates = true;
                    break;
                }
            }
            if (!relates)
                continue;
        } else {
            const QString eventId =
                lead.supportingData.value(QStringLiteral("eventId")).toString();
            if (!eventId.isEmpty() && !caseIds.contains(eventId))
                continue;
        }
        filtered.append(lead);
    }
    return filtered;
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::populateLeadHistory(const QVector<InvestigativeLead>& leads)
{
    m_leadsTable->setRowCount(0);
    for (const InvestigativeLead& lead : leads) {
        const int row = m_leadsTable->rowCount();
        m_leadsTable->insertRow(row);
        m_leadsTable->setItem(row, 0, new QTableWidgetItem(QString::number(lead.rank)));
        m_leadsTable->setItem(row, 1, new QTableWidgetItem(lead.category));
        m_leadsTable->setItem(row, 2, new QTableWidgetItem(lead.headline));
        m_leadsTable->setItem(row, 3,
            new QTableWidgetItem(QStringLiteral("%1%")
                .arg(static_cast<int>(lead.confidence * 100.0))));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool CaseWorkspaceWidget::exportCaseReport(const QString& filePath) const
{
    if (filePath.isEmpty() || m_lastEvents.isEmpty())
        return false;

    const QString caseId = m_caseIdEdit->text().trimmed();
    const QString effectiveCaseId =
        caseId.isEmpty() ? QStringLiteral("recent_events") : caseId;

    const LeadReport report = LeadReportGenerator::generate(effectiveCaseId, m_lastLeads);

    QString md = report.markdownText;
    md += QStringLiteral("\n\n---\n\n## Case Events (Provenance)\n\n");
    md += QStringLiteral("**Events in scope:** %1\n\n").arg(m_lastEvents.size());
    md += QStringLiteral("| Event ID | Crime Type | Source | Quality |\n");
    md += QStringLiteral("|----------|------------|--------|----------|\n");
    for (const CrimeEvent& ev : m_lastEvents) {
        md += QStringLiteral("| %1 | %2 | %3 | %4 |\n")
                  .arg(ev.eventId, ev.crimeType, ev.source)
                  .arg(QString::number(ev.qualityScore, 'f', 2));
    }

    const QVector<CrimeSeries> series = activeSeriesList(m_lastEvents);
    if (!series.isEmpty()) {
        md += QStringLiteral("\n### Detected Series\n\n");
        if (!m_seriesOverrides.isEmpty())
            md += QStringLiteral("*Analyst overrides applied.*\n\n");
        md += QStringLiteral("| Series ID | Events | Dominant type |\n");
        md += QStringLiteral("|-----------|--------|---------------|\n");
        for (const CrimeSeries& s : series) {
            md += QStringLiteral("| %1 | %2 | %3 |\n")
                      .arg(s.seriesId)
                      .arg(s.members.size())
                      .arg(s.dominantCrimeType);
        }
    }

    md += QStringLiteral("\n### Event Provenance (JSON)\n\n```json\n");
    md += QString::fromUtf8(
        QJsonDocument(DataExporter::eventsToJson(m_lastEvents)).toJson(QJsonDocument::Indented));
    md += QStringLiteral("\n```\n");

    return DataExporter::saveText(md, filePath);
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::onExportCaseReport()
{
    if (m_lastEvents.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export Case Report"),
            QStringLiteral("No case events loaded. Filter a case first."));
        return;
    }

    const QString defaultName =
        m_caseIdEdit->text().trimmed().isEmpty()
            ? QStringLiteral("case_report.md")
            : QStringLiteral("%1_report.md").arg(m_caseIdEdit->text().trimmed());

    const bool headless = qEnvironmentVariableIsSet("SENTINEL_HEADLESS_TEST");
    QString path;
    if (headless) {
        path = QDir::temp().filePath(defaultName);
    } else {
        path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Export Case Report"),
            defaultName,
            QStringLiteral("Markdown (*.md);;All Files (*)"));
        if (path.isEmpty())
            return;
    }

    if (!exportCaseReport(path)) {
        if (!headless) {
            QMessageBox::warning(this, QStringLiteral("Export Case Report"),
                QStringLiteral("Failed to write report: %1").arg(DataExporter::lastError()));
        }
        return;
    }

    if (!headless) {
        QMessageBox::information(this, QStringLiteral("Export Case Report"),
            QStringLiteral("Report saved to:\n%1").arg(path));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::onFilterChanged()
{
    m_seriesOverrides.clear();
    refresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void CaseWorkspaceWidget::refresh()
{
    try {
        m_seriesOverrides.clear();

        const QVector<CrimeEvent> events = loadCaseEvents(m_caseIdEdit->text());
        m_lastEvents = events;

        populateEventsTable(events);
        updateSeriesDetection(events);
        updateGeoProfileSummary(events);

        const QVector<InvestigativeLead> allLeads = generateLeadsForCase(events);
        m_lastLeads = filterLeadsToCaseEvents(allLeads, events);
        populateLeadHistory(m_lastLeads);

        m_eventCountLabel->setText(
            QStringLiteral("%1 event%2")
                .arg(events.size())
                .arg(events.size() == 1 ? QString() : QStringLiteral("s")));
    } catch (...) {
        m_lastEvents.clear();
        m_lastLeads.clear();
        m_lastSeriesCount = 0;
        m_seriesOverrides.clear();
        m_displayedSeries.clear();
        m_eventsTable->setRowCount(0);
        m_leadsTable->setRowCount(0);
        m_seriesOverrideTable->setRowCount(0);
        m_seriesCountLabel->setText(QStringLiteral("Series count: —"));
        m_eventCountLabel->setText(QStringLiteral("0 events"));
    }
}
