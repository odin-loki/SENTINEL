#pragma once
#include <QWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QLineEdit>
#include <QComboBox>
#include <QDateEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QLabel>
#include <memory>
#include "core/Database.h"
#include "core/CrimeEvent.h"

class EventsTableWidget : public QWidget {
    Q_OBJECT
public:
    EventsTableWidget(std::shared_ptr<Database> db, QWidget* parent = nullptr);
    void refresh();

signals:
    void eventSelected(const CrimeEvent& ev);

private slots:
    void onFilterChanged();
    void onRowSelected(const QModelIndex& index);
    void onExportCsv();

private:
    void setupUI();
    void loadEvents();
    void populateDetail(const CrimeEvent& ev);
    QString qualityBadge(double score) const;

    std::shared_ptr<Database> m_db;
    QVector<CrimeEvent> m_events;

    QLineEdit*          m_searchEdit;
    QComboBox*          m_crimeTypeFilter;
    QDateEdit*          m_fromDate;
    QDateEdit*          m_toDate;
    QPushButton*        m_filterBtn;
    QPushButton*        m_exportBtn;
    QTableView*         m_table;
    QStandardItemModel* m_model;
    QTextEdit*          m_detailPanel;
    QLabel*             m_countLabel;
};
