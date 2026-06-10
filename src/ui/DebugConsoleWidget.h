#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include "core/SentinelLogger.h"

class DebugConsoleWidget : public QWidget {
    Q_OBJECT
public:
    explicit DebugConsoleWidget(QWidget* parent = nullptr);

public slots:
    void appendEntry(const LogEntry& entry);
    void clear();

private slots:
    void onFilterChanged();
    void onExportClicked();

private:
    void setupUI();
    QString formatEntry(const LogEntry& entry) const;
    QString levelTag(QtMsgType t) const;
    QString colorForLevel(QtMsgType t) const;
    static int severityOf(QtMsgType t);
    void rebuildDisplay();

    QPlainTextEdit* m_textEdit;
    QComboBox*      m_levelFilter;    // Debug/Info/Warning/Critical
    QLineEdit*      m_categoryFilter; // filter by category name
    QPushButton*    m_clearButton;
    QPushButton*    m_exportButton;
    QCheckBox*      m_autoScrollCheck;

    QVector<LogEntry> m_entries;     // cached for re-filtering
};
