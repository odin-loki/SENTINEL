#pragma once
#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QSplitter>
#include <QGroupBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <memory>
#include "core/Database.h"
#include "core/CrimeEvent.h"
#include "inference/EvidenceScorer.h"
#include "models/SeriesDetector.h"

class LeadsWidget : public QWidget {
    Q_OBJECT
public:
    LeadsWidget(std::shared_ptr<Database> db, QWidget* parent = nullptr);
    void setLeads(const QVector<InvestigativeLead>& leads, const QString& forEventId = {});
    void refresh();

private slots:
    void onLeadSelected(QListWidgetItem* item);
    void onRunEvidenceScorer();
    void onExportReport();

private:
    void setupUI();
    void populateLeadDetail(const InvestigativeLead& lead);
    QString confidenceBar(double confidence) const;
    QString categoryIcon(const QString& category) const;

    std::shared_ptr<Database> m_db;
    QVector<InvestigativeLead> m_leads;
    EvidenceScorer m_evidenceScorer;
    QString m_currentEventId;

    QListWidget*           m_leadsList;
    QTextEdit*             m_detailPanel;
    QGroupBox*             m_evidenceBox;
    QDoubleSpinBox*        m_priorSpin;
    QVector<QCheckBox*>    m_evidenceChecks;
    QTextEdit*             m_evidenceOutput;
    QPushButton*           m_runEvidenceBtn;
    QPushButton*           m_exportBtn;
    QLabel*                m_countLabel;
    QTableWidget*          m_seriesTable;
};
