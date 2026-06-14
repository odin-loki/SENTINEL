#pragma once

#include <QWidget>

#include <QLabel>

#include <QMap>

#include <QPointF>

#include <QString>

#include <memory>

#include "core/Database.h"

#include "inference/CoOffendingAnalyser.h"



class QPushButton;



class CoOffendingGraphWidget : public QWidget {

    Q_OBJECT

public:

    explicit CoOffendingGraphWidget(std::shared_ptr<Database> db = {},

                                    QWidget* parent = nullptr);

    void refresh();

    void loadFromCsv(const QString& path);

    void loadFromDatabase();

    int nodeCount() const { return m_nodePositions.size(); }

    int edgeCount() const { return m_edgeCount; }



protected:

    void paintEvent(QPaintEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    void mouseReleaseEvent(QMouseEvent* event) override;

    void mouseMoveEvent(QMouseEvent* event) override;

    void wheelEvent(QWheelEvent* event) override;



private slots:

    void onLoadCsvClicked();

    void onLoadDbClicked();



private:

    void setupUI();

    void layoutNodes();

    void resetView();

    void applyPersonIncidentRecords(const QVector<PersonIncidentRecord>& records,

                                    const QString& statusLabel);

    static QVector<PersonIncidentRecord> parseCsv(const QString& path);

    static QVector<PersonIncidentRecord> recordsFromEvents(const QVector<CrimeEvent>& events);

    static int countPersonLinkedEvents(const QVector<CrimeEvent>& events);

    static QString defaultCsvPath();



    QRectF graphArea() const;

    QPointF graphCentre() const;

    QPointF toScreen(const QPointF& base) const;

    double nodeRadiusFor(const QString& personId) const;

    const NetworkNode* findNetworkNode(const QString& personId) const;

    QString findNodeAt(const QPoint& screenPos) const;

    QString nodeTooltipHtml(const QString& personId) const;



    std::shared_ptr<Database> m_db;

    CoOffendingAnalyser m_analyser;

    QMap<QString, QPointF> m_nodePositions;

    QVector<NetworkNode> m_nodes;

    int m_edgeCount = 0;



    double m_zoom = 1.0;

    QPointF m_panOffset;

    QPoint m_lastMousePos;

    bool m_dragging = false;

    bool m_dragMoved = false;



    QLabel* m_statusLabel;

    QPushButton* m_loadCsvBtn;

    QPushButton* m_loadDbBtn;

    QString m_csvPath;

};

