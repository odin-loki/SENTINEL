#include "ui/EventsTableWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QFile>
#include <QMessageBox>
#include <QFrame>
#include <QFont>
#include <QSortFilterProxyModel>
#include <QDateTime>

// ─────────────────────────────────────────────────────────────────────────────
EventsTableWidget::EventsTableWidget(std::shared_ptr<Database> db, QWidget* parent)
    : QWidget(parent)
    , m_db(std::move(db))
    , m_searchEdit(nullptr)
    , m_crimeTypeFilter(nullptr)
    , m_fromDate(nullptr)
    , m_toDate(nullptr)
    , m_filterBtn(nullptr)
    , m_exportBtn(nullptr)
    , m_table(nullptr)
    , m_model(nullptr)
    , m_detailPanel(nullptr)
    , m_countLabel(nullptr)
{
    setupUI();
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::setupUI()
{
    setStyleSheet(R"(
        QTableView {
            background-color: #16213e;
            gridline-color: #1a2a4a;
            color: #eaeaea;
            border: none;
            selection-background-color: #1a2a4a;
            selection-color: #e94560;
        }
        QHeaderView::section {
            background-color: #0f3460;
            color: #a0a8b8;
            padding: 6px 10px;
            border: none;
            font-size: 11px;
            letter-spacing: 1px;
        }
        QLineEdit, QComboBox, QDateEdit {
            background-color: #1a2035;
            color: #eaeaea;
            border: 1px solid #0f3460;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QLineEdit:focus, QComboBox:focus { border-color: #e94560; }
        QPushButton {
            background-color: #0f3460;
            color: #eaeaea;
            border: none;
            border-radius: 4px;
            padding: 6px 16px;
        }
        QPushButton:hover { background-color: #e94560; }
        QTextEdit {
            background-color: #16213e;
            color: #eaeaea;
            border: none;
            border-top: 1px solid #1a2a4a;
        }
    )");

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(12);

    // ── Page title ───────────────────────────────────────────────────────────
    auto* titleLbl = new QLabel("Crime Events", this);
    titleLbl->setStyleSheet("color: #eaeaea; font-size: 22px; font-weight: bold;");
    outerLayout->addWidget(titleLbl);

    // ── Filter bar ───────────────────────────────────────────────────────────
    auto* filterFrame = new QFrame(this);
    filterFrame->setFrameShape(QFrame::NoFrame);
    filterFrame->setStyleSheet("background-color: #16213e; border-radius: 8px; padding: 4px;");

    auto* filterRow = new QHBoxLayout(filterFrame);
    filterRow->setContentsMargins(12, 8, 12, 8);
    filterRow->setSpacing(10);

    auto* searchLbl = new QLabel("Search:", filterFrame);
    searchLbl->setStyleSheet("color: #a0a8b8;");
    m_searchEdit = new QLineEdit(filterFrame);
    m_searchEdit->setPlaceholderText("Keyword, location, ID…");
    m_searchEdit->setMinimumWidth(180);

    auto* typeLbl = new QLabel("Type:", filterFrame);
    typeLbl->setStyleSheet("color: #a0a8b8;");
    m_crimeTypeFilter = new QComboBox(filterFrame);
    m_crimeTypeFilter->addItem("All types", "");
    for (const QString& t : {
         "assault","burglary","theft","vehicle_crime",
         "drugs","robbery","criminal_damage","antisocial_behaviour",
         "public_order","other_crime"}) {
        m_crimeTypeFilter->addItem(t, t);
    }
    m_crimeTypeFilter->setMinimumWidth(150);

    auto* fromLbl = new QLabel("From:", filterFrame);
    fromLbl->setStyleSheet("color: #a0a8b8;");
    m_fromDate = new QDateEdit(QDate::currentDate().addYears(-1), filterFrame);
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setDisplayFormat("yyyy-MM-dd");

    auto* toLbl = new QLabel("To:", filterFrame);
    toLbl->setStyleSheet("color: #a0a8b8;");
    m_toDate = new QDateEdit(QDate::currentDate(), filterFrame);
    m_toDate->setCalendarPopup(true);
    m_toDate->setDisplayFormat("yyyy-MM-dd");

    m_filterBtn = new QPushButton("Filter", filterFrame);
    m_exportBtn = new QPushButton("Export CSV", filterFrame);
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #1a4a2a; color: #81c784; }"
        "QPushButton:hover { background-color: #2e7d32; }");

    filterRow->addWidget(searchLbl);
    filterRow->addWidget(m_searchEdit);
    filterRow->addWidget(typeLbl);
    filterRow->addWidget(m_crimeTypeFilter);
    filterRow->addWidget(fromLbl);
    filterRow->addWidget(m_fromDate);
    filterRow->addWidget(toLbl);
    filterRow->addWidget(m_toDate);
    filterRow->addWidget(m_filterBtn);
    filterRow->addWidget(m_exportBtn);
    filterRow->addStretch();

    m_countLabel = new QLabel("0 events", this);
    m_countLabel->setStyleSheet("color: #4a5568; font-size: 11px; padding-left: 8px;");
    filterRow->addWidget(m_countLabel);

    outerLayout->addWidget(filterFrame);

    // ── Splitter: table top, detail bottom ───────────────────────────────────
    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(4);
    splitter->setStyleSheet("QSplitter::handle { background: #0f3460; }");

    // Table
    m_model = new QStandardItemModel(0, 7, this);
    m_model->setHorizontalHeaderLabels(
        {"ID", "Date", "Crime Type", "Location", "Suburb", "Outcome", "Quality"});

    m_table = new QTableView(this);
    m_table->setModel(m_model);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setStyleSheet("QTableView { alternate-background-color: #1a2035; }");
    m_table->setSortingEnabled(true);
    splitter->addWidget(m_table);

    // Detail panel
    m_detailPanel = new QTextEdit(this);
    m_detailPanel->setReadOnly(true);
    m_detailPanel->setMinimumHeight(160);
    m_detailPanel->setMaximumHeight(280);
    m_detailPanel->setPlaceholderText("Select a row to view event details…");
    splitter->addWidget(m_detailPanel);

    splitter->setSizes({400, 200});
    outerLayout->addWidget(splitter, 1);

    // Connections
    connect(m_filterBtn,  &QPushButton::clicked, this, &EventsTableWidget::onFilterChanged);
    connect(m_exportBtn,  &QPushButton::clicked, this, &EventsTableWidget::onExportCsv);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &EventsTableWidget::onFilterChanged);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &EventsTableWidget::onRowSelected);
}

// ─────────────────────────────────────────────────────────────────────────────
QString EventsTableWidget::qualityBadge(double score) const
{
    if (score >= 0.7) return "🟢 High";
    if (score >= 0.4) return "🟡 Medium";
    return "🔴 Low";
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::loadEvents()
{
    m_model->removeRows(0, m_model->rowCount());

    for (int row = 0; row < m_events.size(); ++row) {
        const CrimeEvent& ev = m_events[row];

        auto* idItem      = new QStandardItem(ev.id);
        auto* dateItem    = new QStandardItem(ev.timestamp.toString("yyyy-MM-dd HH:mm"));
        auto* typeItem    = new QStandardItem(ev.crimeType.isEmpty() ? "Unknown" : ev.crimeType);
        auto* locItem     = new QStandardItem(ev.locationDescription);
        auto* suburbItem  = new QStandardItem(ev.suburb.isEmpty() ? "—" : ev.suburb);
        auto* outcomeItem = new QStandardItem(ev.outcome.isEmpty() ? "—" : ev.outcome);
        auto* qualItem    = new QStandardItem(qualityBadge(ev.qualityScore));

        idItem->setData(row, Qt::UserRole);   // store index for retrieval

        m_model->appendRow({idItem, dateItem, typeItem, locItem,
                            suburbItem, outcomeItem, qualItem});
    }

    m_countLabel->setText(QString("%1 event%2").arg(m_events.size()).arg(m_events.size() == 1 ? "" : "s"));
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::refresh()
{
    onFilterChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::onFilterChanged()
{
    const QString search   = m_searchEdit->text().trimmed();
    const QString crimeType = m_crimeTypeFilter->currentData().toString();
    const QDate   from      = m_fromDate->date();
    const QDate   to        = m_toDate->date();

    try {
        QDateTime fromDt = from.isValid()
            ? QDateTime(from, QTime(0,0,0), Qt::LocalTime) : QDateTime{};
        QDateTime toDt = to.isValid()
            ? QDateTime(to, QTime(23,59,59), Qt::LocalTime) : QDateTime{};
        m_events = m_db->queryEvents(crimeType, fromDt, toDt);
        // Client-side search filter if a keyword is entered
        if (!search.isEmpty()) {
            m_events.erase(
                std::remove_if(m_events.begin(), m_events.end(),
                    [&search](const CrimeEvent& e) {
                        return !e.eventId.contains(search, Qt::CaseInsensitive)
                            && !e.crimeType.contains(search, Qt::CaseInsensitive)
                            && !e.locationRaw.value_or("").contains(search, Qt::CaseInsensitive)
                            && !e.suburb.contains(search, Qt::CaseInsensitive)
                            && !e.narrative.value_or("").contains(search, Qt::CaseInsensitive);
                    }),
                m_events.end());
        }
    } catch (...) {
        m_events.clear();
    }
    loadEvents();
    m_detailPanel->clear();
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::onRowSelected(const QModelIndex& index)
{
    if (!index.isValid()) return;

    const QModelIndex idIdx = m_model->index(index.row(), 0);
    const int evIdx = m_model->data(idIdx, Qt::UserRole).toInt();

    if (evIdx < 0 || evIdx >= m_events.size()) return;

    const CrimeEvent& ev = m_events[evIdx];
    populateDetail(ev);
    emit eventSelected(ev);
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::populateDetail(const CrimeEvent& ev)
{
    QString html;
    html.reserve(2048);

    html += "<html><body style='font-family:\"Segoe UI\",Arial;color:#eaeaea;"
            "background:#16213e;padding:12px;'>";

    // Header
    html += QString("<h3 style='color:#e94560;margin:0 0 8px 0;'>%1</h3>").arg(ev.crimeType.toUpper());

    // Key-value table
    auto row = [&](const QString& label, const QString& value, const QString& color = "#eaeaea") {
        html += QString("<tr><td style='color:#a0a8b8;width:160px;padding:3px 8px 3px 0;'>"
                        "%1</td><td style='color:%2;'>%3</td></tr>")
                    .arg(label, color, value);
    };

    html += "<table style='border-collapse:collapse;width:100%;'>";
    row("Event ID",   ev.id);
    row("Date/Time",  ev.timestamp.toString("dddd dd MMM yyyy, HH:mm:ss"));
    row("Crime Type", ev.crimeType.isEmpty() ? "Unknown" : ev.crimeType, "#4fc3f7");
    row("Location",   ev.locationDescription);
    row("Suburb",     ev.suburb.isEmpty() ? "—" : ev.suburb);
    row("Outcome",    ev.outcome.isEmpty() ? "Under investigation" : ev.outcome);
    row("Latitude",   QString::number(ev.latitude,  'f', 6));
    row("Longitude",  QString::number(ev.longitude, 'f', 6));
    row("Quality",    QString("%1  %2").arg(qualityBadge(ev.qualityScore))
                                       .arg(ev.qualityScore, 0, 'f', 3));
    row("Source",     ev.source);
    if (ev.narrative.has_value()) {
        row("Narrative", ev.narrative.value().left(300) + (ev.narrative.value().length() > 300 ? "…" : ""));
    }
    html += "</table>";

    html += "</body></html>";
    m_detailPanel->setHtml(html);
}

// ─────────────────────────────────────────────────────────────────────────────
void EventsTableWidget::onExportCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Events to CSV", "events_export.csv",
        "CSV Files (*.csv);;All Files (*)");

    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error",
            "Could not open file for writing:\n" + path);
        return;
    }

    QTextStream out(&file);
    out << "ID,Date,CrimeType,Location,Suburb,Outcome,Latitude,Longitude,QualityScore\n";

    for (const CrimeEvent& ev : m_events) {
        auto esc = [](const QString& s) {
            QString copy = s;
            copy.replace('"', "\"\"");
            return '"' + copy + '"';
        };
        out << esc(ev.id) << ','
            << ev.timestamp.toString("yyyy-MM-dd HH:mm:ss") << ','
            << esc(ev.crimeType) << ','
            << esc(ev.locationDescription) << ','
            << esc(ev.suburb) << ','
            << esc(ev.outcome) << ','
            << ev.latitude  << ','
            << ev.longitude << ','
            << ev.qualityScore << '\n';
    }

    QMessageBox::information(this, "Export Complete",
        QString("Exported <b>%1</b> events to:<br>%2").arg(m_events.size()).arg(path));
}
