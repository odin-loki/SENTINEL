#include "ui/AuditLogWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFrame>

// ─────────────────────────────────────────────────────────────────────────────
AuditLogWidget::AuditLogWidget(ProvenanceLog& log, QWidget* parent)
    : QWidget(parent)
    , m_log(log)
    , m_filterEdit(nullptr)
    , m_table(nullptr)
    , m_clearBtn(nullptr)
    , m_countLabel(nullptr)
{
    setupUI();
    refresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void AuditLogWidget::setupUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ── Page title ───────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel("Audit Log", this);
    title->setStyleSheet("color: #eaeaea; font-size: 22px; font-weight: bold;");
    titleRow->addWidget(title);
    titleRow->addStretch();

    m_countLabel = new QLabel("0 entries", this);
    m_countLabel->setStyleSheet("color: #4a5568; font-size: 13px;");
    titleRow->addWidget(m_countLabel);
    root->addLayout(titleRow);

    // ── Filter / toolbar bar ─────────────────────────────────────────────────
    auto* filterFrame = new QFrame(this);
    filterFrame->setStyleSheet(
        "QFrame { background-color: #16213e; border-radius: 8px; padding: 4px; }");
    auto* filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setContentsMargins(10, 6, 10, 6);
    filterLayout->setSpacing(8);

    auto* searchIcon = new QLabel("🔍", this);
    searchIcon->setStyleSheet("color: #a0a8b8; font-size: 14px;");
    filterLayout->addWidget(searchIcon);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter by Event ID or Stage…");
    m_filterEdit->setStyleSheet(R"(
        QLineEdit {
            background: #1a2035;
            border: 1px solid #0f3460;
            border-radius: 4px;
            color: #eaeaea;
            padding: 4px 8px;
            font-size: 13px;
        }
        QLineEdit:focus { border-color: #e94560; }
    )");
    filterLayout->addWidget(m_filterEdit, 1);

    m_clearBtn = new QPushButton("Clear Log", this);
    m_clearBtn->setStyleSheet(R"(
        QPushButton {
            background: #0f3460;
            border: none;
            border-radius: 4px;
            color: #eaeaea;
            padding: 5px 14px;
            font-size: 13px;
        }
        QPushButton:hover { background: #e94560; }
        QPushButton:pressed { background: #c73652; }
    )");
    filterLayout->addWidget(m_clearBtn);

    root->addWidget(filterFrame);

    // ── Table ────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {"Timestamp", "Event ID", "Stage", "Action", "Detail", "Hash"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultSectionSize(160);
    m_table->horizontalHeader()->resizeSection(0, 180); // Timestamp
    m_table->horizontalHeader()->resizeSection(1, 130); // Event ID
    m_table->horizontalHeader()->resizeSection(2,  90); // Stage
    m_table->horizontalHeader()->resizeSection(3, 140); // Action
    m_table->horizontalHeader()->resizeSection(4, 220); // Detail
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(true);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet(R"(
        QTableWidget {
            background-color: #16213e;
            gridline-color: #1a2a4a;
            border: 1px solid #0f3460;
            border-radius: 6px;
            color: #eaeaea;
            selection-background-color: #1a2a4a;
            selection-color: #e94560;
        }
        QTableWidget::item { padding: 4px 8px; }
        QTableWidget::item:alternate { background-color: #1a2035; }
        QHeaderView::section {
            background-color: #0f3460;
            color: #a0a8b8;
            border: none;
            border-right: 1px solid #1a2a4a;
            padding: 6px 8px;
            font-weight: bold;
        }
        QScrollBar:vertical {
            background: #16213e;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: #0f3460;
            border-radius: 5px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover { background: #e94560; }
    )");
    root->addWidget(m_table, 1);

    // ── Connections ──────────────────────────────────────────────────────────
    connect(m_filterEdit, &QLineEdit::textChanged, this, &AuditLogWidget::onFilterChanged);
    connect(m_clearBtn,   &QPushButton::clicked,   this, &AuditLogWidget::onClearLog);
}

// ─────────────────────────────────────────────────────────────────────────────
void AuditLogWidget::refresh()
{
    const QString filter = m_filterEdit ? m_filterEdit->text() : QString{};
    const QVector<ProvenanceEntry> entries = m_log.recent(500);

    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    for (const ProvenanceEntry& e : entries) {
        if (!filter.isEmpty()) {
            const bool match = e.eventId.contains(filter, Qt::CaseInsensitive)
                            || e.stage.contains(filter, Qt::CaseInsensitive);
            if (!match) continue;
        }

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* tsItem     = new QTableWidgetItem(e.timestamp.toString("yyyy-MM-dd HH:mm:ss"));
        auto* idItem     = new QTableWidgetItem(e.eventId);
        auto* stageItem  = new QTableWidgetItem(e.stage);
        auto* actionItem = new QTableWidgetItem(e.action);
        auto* detailItem = new QTableWidgetItem(e.detail);
        auto* hashItem   = new QTableWidgetItem(e.dataHash);

        // Colour-code stage column
        static const QHash<QString, QString> stageColours = {
            {"ingest",    "#4a9eff"},
            {"nlp",       "#a78bfa"},
            {"model",     "#34d399"},
            {"inference", "#fbbf24"},
            {"output",    "#e94560"},
        };
        const QString stageColour = stageColours.value(e.stage.toLower(), "#a0a8b8");
        stageItem->setForeground(QColor(stageColour));

        m_table->setItem(row, 0, tsItem);
        m_table->setItem(row, 1, idItem);
        m_table->setItem(row, 2, stageItem);
        m_table->setItem(row, 3, actionItem);
        m_table->setItem(row, 4, detailItem);
        m_table->setItem(row, 5, hashItem);
    }

    m_table->setSortingEnabled(true);

    const int shown = m_table->rowCount();
    const int total = entries.size();
    if (filter.isEmpty()) {
        m_countLabel->setText(QString("%1 entr%2").arg(total).arg(total == 1 ? "y" : "ies"));
    } else {
        m_countLabel->setText(QString("%1 / %2 entries").arg(shown).arg(total));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void AuditLogWidget::onFilterChanged(const QString& text)
{
    Q_UNUSED(text)
    refresh();
}

// ─────────────────────────────────────────────────────────────────────────────
void AuditLogWidget::onClearLog()
{
    m_log.clear();
    refresh();
}
