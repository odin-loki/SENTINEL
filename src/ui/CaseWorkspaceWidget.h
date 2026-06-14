#pragma once

#include <QWidget>

#include <QLineEdit>

#include <QTableWidget>

#include <QLabel>

#include <QPushButton>

#include <QVector>

#include <memory>

#include "core/Database.h"

#include "core/CrimeEvent.h"

#include "models/SeriesDetector.h"



class GeoProfileHeatmapWidget;



class CaseWorkspaceWidget : public QWidget {

    Q_OBJECT

public:

    explicit CaseWorkspaceWidget(std::shared_ptr<Database> db, QWidget* parent = nullptr);

    void refresh();



    bool exportCaseReport(const QString& filePath) const;

    int lastSeriesCount() const { return m_lastSeriesCount; }



private slots:

    void onFilterChanged();

    void onExportCaseReport();

    void onMergeSelectedSeries();

    void onSplitSeriesEvent();

    void onResetSeriesOverrides();

    void onSplitSeriesRow(int row);



private:

    void setupUI();

    QVector<CrimeEvent> loadCaseEvents(const QString& caseId) const;

    void populateEventsTable(const QVector<CrimeEvent>& events);

    QVector<CrimeSeries> detectSeriesFromEvents(const QVector<CrimeEvent>& events) const;

    QVector<CrimeSeries> activeSeriesList(const QVector<CrimeEvent>& events) const;

    void recomputeSeriesMetadata(CrimeSeries& series) const;

    void updateSeriesDetection(const QVector<CrimeEvent>& events);

    void populateSeriesTable(const QVector<CrimeSeries>& series);

    void applySeriesOverridesAndRefresh();

    void updateGeoProfileSummary(const QVector<CrimeEvent>& events);

    QVector<InvestigativeLead> generateLeadsForCase(const QVector<CrimeEvent>& events) const;

    QVector<InvestigativeLead> filterLeadsToCaseEvents(

        const QVector<InvestigativeLead>& leads,

        const QVector<CrimeEvent>& events) const;

    void populateLeadHistory(const QVector<InvestigativeLead>& leads);

    QVector<int> checkedSeriesRows() const;

    int selectedSeriesRowForSplit() const;



    std::shared_ptr<Database> m_db;



    QLineEdit*    m_caseIdEdit;

    QPushButton*  m_filterBtn;

    QPushButton*  m_exportBtn;

    QTableWidget* m_eventsTable;

    QTableWidget* m_leadsTable;

    QTableWidget* m_seriesOverrideTable;

    QPushButton*  m_seriesMergeBtn;

    QPushButton*  m_seriesSplitBtn;

    QPushButton*  m_seriesResetBtn;

    QLabel*       m_seriesCountLabel;

    QLabel*       m_geoProfileLabel;

    GeoProfileHeatmapWidget* m_geoHeatmap = nullptr;

    QLabel*       m_eventCountLabel;



    int m_lastSeriesCount = 0;

    QVector<CrimeEvent> m_lastEvents;

    QVector<InvestigativeLead> m_lastLeads;

    QVector<CrimeSeries> m_seriesOverrides;

    QVector<CrimeSeries> m_displayedSeries;

};

