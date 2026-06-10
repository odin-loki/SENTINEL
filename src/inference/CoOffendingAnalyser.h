#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CoOffendingAnalyser — co-offending network analysis
//
// Builds a weighted co-offender graph from incident–person associations, then
// computes:
//   • PageRank centrality  (iterative, damping = 0.85)
//   • Betweenness centrality (Brandes BFS algorithm, normalised)
//   • Community detection  (greedy connected-component labelling as baseline)
//
// Returns a ranked list of NetworkLead objects for the HintEngine.
//
// Design basis: Louvain community detection (Blondel et al. 2008);
//               PageRank (Page et al. 1999);
//               Brandes betweenness (Brandes 2001).
// ─────────────────────────────────────────────────────────────────────────────
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include "core/CrimeEvent.h"

struct PersonIncidentRecord {
    QString personId;
    QString incidentId;
    QString role;           // "suspect" | "witness" | "victim" | "associate"
    double  roleWeight = 1.0;  // e.g. suspect=1.0, associate=0.5
};

struct NetworkNode {
    QString personId;
    double  pageRank     = 0.0;
    double  betweenness  = 0.0;
    int     communityId  = -1;
    int     degree       = 0;
    QStringList incidentIds;
    QMap<QString, double> neighbours;   // personId → edge weight
};

class CoOffendingAnalyser {
public:
    // Build graph from person-incident associations.
    // Two people share an edge if they appear in the same incident.
    // Edge weight = sum of roleWeight products for shared incidents.
    void buildGraph(const QVector<PersonIncidentRecord>& records);

    // Run all analysis steps: PageRank + betweenness + community detection.
    // Must be called after buildGraph().
    void analyse();

    // Return top-k most central/risky network leads for a specific incident.
    // Includes direct co-offenders from the incident plus second-degree links.
    QVector<NetworkLead> findLeads(const QString& incidentId,
                                   int topK = 5) const;

    // Return all nodes (for debugging / export)
    QVector<NetworkNode> nodes() const;

    bool isBuilt() const { return m_built; }

private:
    // ── Graph algorithms ────────────────────────────────────────────────────
    void computePageRank(double damping = 0.85, int maxIter = 100,
                         double tol = 1e-6);
    void computeBetweenness();
    void detectCommunities();       // greedy modularity via union-find

    // ── Utility ─────────────────────────────────────────────────────────────
    double riskScore(const NetworkNode& node, int nShared) const;

    QMap<QString, NetworkNode> m_graph;     // personId → node
    // incidentId → set of personIds
    QMap<QString, QSet<QString>> m_incidentPersons;

    bool m_built    = false;
    bool m_analysed = false;

    // Union-Find for community detection
    mutable QMap<QString, QString> m_ufParent;
    QString ufFind(const QString& x) const;
    void    ufUnion(const QString& a, const QString& b);
};
