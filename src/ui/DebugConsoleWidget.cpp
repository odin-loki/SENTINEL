#include "ui/DebugConsoleWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

// ─────────────────────────────────────────────────────────────────────────────

DebugConsoleWidget::DebugConsoleWidget(QWidget* parent)
    : QWidget(parent)
    , m_textEdit(nullptr)
    , m_levelFilter(nullptr)
    , m_categoryFilter(nullptr)
    , m_clearButton(nullptr)
    , m_exportButton(nullptr)
    , m_autoScrollCheck(nullptr)
{
    setupUI();

    // Connect to live log stream
    connect(&SentinelLogger::instance(), &SentinelLogger::newEntry,
            this, &DebugConsoleWidget::appendEntry);

    // Populate with recent history
    const QVector<LogEntry> history = SentinelLogger::instance().recent();
    for (const LogEntry& e : history)
        appendEntry(e);
}

// ─────────────────────────────────────────────────────────────────────────────

void DebugConsoleWidget::setupUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Title bar ────────────────────────────────────────────────────────────
    auto* titleBar = new QHBoxLayout;
    auto* titleLbl = new QLabel("🐛  Debug Console", this);
    titleLbl->setStyleSheet("font-size: 16px; font-weight: bold; color: #e94560;");
    titleBar->addWidget(titleLbl);
    titleBar->addStretch();
    root->addLayout(titleBar);

    // ── Filter bar ───────────────────────────────────────────────────────────
    auto* filterBar = new QHBoxLayout;
    filterBar->setSpacing(8);

    auto* levelLbl = new QLabel("Min Level:", this);
    levelLbl->setStyleSheet("color: #a0a8b8;");
    filterBar->addWidget(levelLbl);

    m_levelFilter = new QComboBox(this);
    m_levelFilter->addItems({"Debug", "Info", "Warning", "Critical"});
    m_levelFilter->setCurrentIndex(0);
    m_levelFilter->setStyleSheet(R"(
        QComboBox {
            background: #1a2035; color: #eaeaea; border: 1px solid #0f3460;
            padding: 4px 8px; border-radius: 4px; min-width: 90px;
        }
        QComboBox::drop-down { border: none; }
        QComboBox QAbstractItemView { background: #16213e; color: #eaeaea; selection-background-color: #0f3460; }
    )");
    filterBar->addWidget(m_levelFilter);

    auto* catLbl = new QLabel("Category:", this);
    catLbl->setStyleSheet("color: #a0a8b8;");
    filterBar->addWidget(catLbl);

    m_categoryFilter = new QLineEdit(this);
    m_categoryFilter->setPlaceholderText("Filter by category…");
    m_categoryFilter->setStyleSheet(R"(
        QLineEdit {
            background: #1a2035; color: #eaeaea; border: 1px solid #0f3460;
            padding: 4px 8px; border-radius: 4px;
        }
        QLineEdit:focus { border-color: #e94560; }
    )");
    filterBar->addWidget(m_categoryFilter, 1);

    m_autoScrollCheck = new QCheckBox("Auto-scroll", this);
    m_autoScrollCheck->setChecked(true);
    m_autoScrollCheck->setStyleSheet("color: #a0a8b8;");
    filterBar->addWidget(m_autoScrollCheck);

    m_clearButton = new QPushButton("Clear", this);
    m_clearButton->setStyleSheet(R"(
        QPushButton {
            background: #1a2035; color: #a0a8b8; border: 1px solid #0f3460;
            padding: 4px 12px; border-radius: 4px;
        }
        QPushButton:hover { background: #0f3460; color: #eaeaea; }
        QPushButton:pressed { background: #e94560; color: #fff; }
    )");
    filterBar->addWidget(m_clearButton);

    m_exportButton = new QPushButton("Export…", this);
    m_exportButton->setStyleSheet(m_clearButton->styleSheet());
    filterBar->addWidget(m_exportButton);

    root->addLayout(filterBar);

    // ── Log text area ─────────────────────────────────────────────────────────
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setMaximumBlockCount(5000);
    m_textEdit->setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #0a0e1a;
            color: #eaeaea;
            font-family: "Consolas", "Courier New", monospace;
            font-size: 12px;
            border: 1px solid #1a2035;
            border-radius: 4px;
        }
    )");
    root->addWidget(m_textEdit, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_clearButton,    &QPushButton::clicked,  this, &DebugConsoleWidget::clear);
    connect(m_exportButton,   &QPushButton::clicked,  this, &DebugConsoleWidget::onExportClicked);
    connect(m_levelFilter,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DebugConsoleWidget::onFilterChanged);
    connect(m_categoryFilter, &QLineEdit::textChanged,
            this, &DebugConsoleWidget::onFilterChanged);
}

// ─────────────────────────────────────────────────────────────────────────────

int DebugConsoleWidget::severityOf(QtMsgType t)
{
    switch (t) {
        case QtDebugMsg:    return 0;
        case QtInfoMsg:     return 1;
        case QtWarningMsg:  return 2;
        case QtCriticalMsg: return 3;
        case QtFatalMsg:    return 4;
        default:            return 0;
    }
}

QString DebugConsoleWidget::levelTag(QtMsgType t) const
{
    switch (t) {
        case QtDebugMsg:    return QStringLiteral("DBG");
        case QtInfoMsg:     return QStringLiteral("INF");
        case QtWarningMsg:  return QStringLiteral("WRN");
        case QtCriticalMsg: return QStringLiteral("CRT");
        case QtFatalMsg:    return QStringLiteral("FTL");
        default:            return QStringLiteral("???");
    }
}

QString DebugConsoleWidget::colorForLevel(QtMsgType t) const
{
    switch (t) {
        case QtDebugMsg:    return QStringLiteral("#6b7280");
        case QtInfoMsg:     return QStringLiteral("#eaeaea");
        case QtWarningMsg:  return QStringLiteral("#f59e0b");
        case QtCriticalMsg: return QStringLiteral("#ef4444");
        case QtFatalMsg:    return QStringLiteral("#dc2626");
        default:            return QStringLiteral("#eaeaea");
    }
}

QString DebugConsoleWidget::formatEntry(const LogEntry& entry) const
{
    return QStringLiteral("[%1] [%2] [%3] %4")
        .arg(entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")))
        .arg(levelTag(entry.level))
        .arg(entry.category)
        .arg(entry.message);
}

// ─────────────────────────────────────────────────────────────────────────────

void DebugConsoleWidget::appendEntry(const LogEntry& entry)
{
    m_entries.append(entry);

    const int minSeverity  = m_levelFilter ? m_levelFilter->currentIndex() : 0;
    const QString catFilter = m_categoryFilter ? m_categoryFilter->text() : QString{};

    if (severityOf(entry.level) < minSeverity)
        return;

    if (!catFilter.isEmpty() &&
        !entry.category.contains(catFilter, Qt::CaseInsensitive))
        return;

    const QString line  = formatEntry(entry);
    const QString color = colorForLevel(entry.level);

    QTextCursor cursor(m_textEdit->document());
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat fmt;
    fmt.setForeground(QColor(color));
    cursor.insertText(line + QLatin1Char('\n'), fmt);

    if (m_autoScrollCheck && m_autoScrollCheck->isChecked())
        m_textEdit->moveCursor(QTextCursor::End);
}

// ─────────────────────────────────────────────────────────────────────────────

void DebugConsoleWidget::clear()
{
    m_entries.clear();
    m_textEdit->clear();
    SentinelLogger::instance().clear();
}

// ─────────────────────────────────────────────────────────────────────────────

void DebugConsoleWidget::onFilterChanged()
{
    rebuildDisplay();
}

void DebugConsoleWidget::rebuildDisplay()
{
    m_textEdit->clear();

    const int minSeverity  = m_levelFilter->currentIndex();
    const QString catFilter = m_categoryFilter->text();

    QTextCursor cursor(m_textEdit->document());
    cursor.movePosition(QTextCursor::End);

    for (const LogEntry& entry : std::as_const(m_entries)) {
        if (severityOf(entry.level) < minSeverity) continue;
        if (!catFilter.isEmpty() &&
            !entry.category.contains(catFilter, Qt::CaseInsensitive))
            continue;

        QTextCharFormat fmt;
        fmt.setForeground(QColor(colorForLevel(entry.level)));
        cursor.insertText(formatEntry(entry) + QLatin1Char('\n'), fmt);
    }

    if (m_autoScrollCheck && m_autoScrollCheck->isChecked())
        m_textEdit->moveCursor(QTextCursor::End);
}

// ─────────────────────────────────────────────────────────────────────────────

void DebugConsoleWidget::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Debug Log"),
        QStringLiteral("sentinel_debug_%1.log").arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        QStringLiteral("Log Files (*.log);;Text Files (*.txt);;All Files (*)"));

    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    for (const LogEntry& entry : std::as_const(m_entries))
        out << formatEntry(entry) << '\n';
}
