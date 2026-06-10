#pragma once
#include <QWidget>
#include "audit/ProvenanceLog.h"

class QLineEdit;
class QTableWidget;
class QPushButton;
class QLabel;

class AuditLogWidget : public QWidget {
    Q_OBJECT
public:
    explicit AuditLogWidget(ProvenanceLog& log, QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void onFilterChanged(const QString& text);
    void onClearLog();

private:
    void setupUI();
    void applyFilter(const QString& text);

    ProvenanceLog& m_log;

    QLineEdit*    m_filterEdit;
    QTableWidget* m_table;
    QPushButton*  m_clearBtn;
    QLabel*       m_countLabel;
};
