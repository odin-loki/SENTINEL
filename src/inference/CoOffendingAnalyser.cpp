#include "inference/CoOffendingAnalyser.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <stack>
#include <QDebug>
#include <QStack>

// ─── Union-Find ───────────────────────────────────────────────────────────────

QString CoOffendingAnalyser::ufFind(const QString& x) const
{
    if (!m_ufParent.contains(x)) {
        m_ufParent[x] = x;
        return x;
    }
    if (m_ufParent[x] != x)
        m_ufParent[x] = ufFind(m_ufParent[x]);   // path compression
    return m_ufParent[x];
}

void CoOffendingAnalyser::ufUnion(const QString& a, const QString& b)
{
    const QString ra = ufFind(a);
    const QString rb = ufFind(b);
    if (ra != rb) m_ufParent[ra] = rb;
}

// ─── Build graph ─────────────────────────────────────────────────────────────

void CoOffendingAnalyser::buildGraph(const QVector<PersonIncidentRecord>& records)
{
    m_graph.clear();
    m_incidentPersons.clear();
    m_built    = false;
    m_analysed = false;

    // Index: incident → persons
    for (const auto& rec : records) {
        m_incidentPersons[rec.incidentId].insert(rec.personId);

        auto& node = m_graph[rec.personId];
        node.personId = rec.personId;
        if (!node.incidentIds.contains(rec.incidentId))
            node.incidentIds.append(rec.incidentId);
    }

    // Build edges: for each incident, connect all persons in it
    for (const auto& [incId, persons] : m_incidentPersons.asKeyValueRange()) {
        const QList<QString> pList(persons.cbegin(), persons.cend());
        for (int i = 0; i < pList.size(); ++i) {
            for (int j = i + 1; j < pList.size(); ++j) {
                const QString& pA = pList[i];
                const QString& pB = pList[j];

                // Find role weights for this incident
                double wA = 1.0, wB = 1.0;
                for (const auto& rec : records) {
                    if (rec.incidentId == incId && rec.personId == pA) wA = rec.roleWeight;
                    if (rec.incidentId == incId && rec.personId == pB) wB = rec.roleWeight;
                }
                const double edgeW = wA * wB;

                m_graph[pA].neighbours[pB] += edgeW;
                m_graph[pB].neighbours[pA] += edgeW;
            }
        }
    }

    // Compute degree
    for (auto it = m_graph.begin(); it != m_graph.end(); ++it)
        it.value().degree = static_cast<int>(it.value().neighbours.size());

    m_built = true;
}

// ─── PageRank ─────────────────────────────────────────────────────────────────

void CoOffendingAnalyser::computePageRank(double damping, int maxIter, double tol)
{
    const int n = m_graph.size();
    if (n == 0) return;

    // Initial uniform rank
    const double init = 1.0 / n;
    QMap<QString, double> rank;
    for (const auto& pid : m_graph.keys()) rank[pid] = init;

    // Precompute out-weights (sum of edge weights)
    QMap<QString, double> outWeight;
    for (const auto& [pid, node] : m_graph.asKeyValueRange()) {
        double w = 0.0;
        for (const double ew : node.neighbours) w += ew;
        outWeight[pid] = (w > 0.0) ? w : 1.0;  // avoid division by zero
    }

    for (int iter = 0; iter < maxIter; ++iter) {
        QMap<QString, double> newRank;
        for (const auto& pid : m_graph.keys()) newRank[pid] = (1.0 - damping) / n;

        for (const auto& [pid, node] : m_graph.asKeyValueRange()) {
            for (const auto& [nid, ew] : node.neighbours.asKeyValueRange()) {
                const double contrib = damping * rank[pid] * (ew / outWeight[pid]);
                newRank[nid] += contrib;
            }
        }

        // Check convergence
        double delta = 0.0;
        for (const auto& pid : rank.keys())
            delta += std::abs(newRank[pid] - rank[pid]);

        rank = newRank;
        if (delta < tol) break;
    }

    for (auto it = m_graph.begin(); it != m_graph.end(); ++it)
        it.value().pageRank = rank.value(it.key(), 0.0);
}

// ─── Betweenness centrality (Brandes 2001) ───────────────────────────────────

void CoOffendingAnalyser::computeBetweenness()
{
    const QList<QString> nodes = m_graph.keys();
    const int n = nodes.size();
    if (n < 3) return;

    QMap<QString, double> betw;
    for (const auto& pid : nodes) betw[pid] = 0.0;

    // BFS from each source (unweighted — treats graph as unweighted for speed)
    for (const QString& src : nodes) {
        QMap<QString, QStringList> pred;   // predecessors on shortest paths
        QMap<QString, int>         dist;
        QMap<QString, double>      sigma;  // number of shortest paths
        QStack<QString>            stk;

        for (const auto& v : nodes) { dist[v] = -1; sigma[v] = 0.0; }
        dist[src]  = 0;
        sigma[src] = 1.0;

        std::queue<QString> q;
        q.push(src);

        while (!q.empty()) {
            const QString v = q.front(); q.pop();
            stk.push(v);

            for (const auto& w : m_graph[v].neighbours.keys()) {
                if (dist[w] < 0) {
                    dist[w] = dist[v] + 1;
                    q.push(w);
                }
                if (dist[w] == dist[v] + 1) {
                    sigma[w] += sigma[v];
                    pred[w].append(v);
                }
            }
        }

        QMap<QString, double> delta;
        for (const auto& v : nodes) delta[v] = 0.0;

        while (!stk.isEmpty()) {
            const QString w = stk.pop();
            for (const QString& v : pred[w]) {
                delta[v] += (sigma[v] / sigma[w]) * (1.0 + delta[w]);
            }
            if (w != src) betw[w] += delta[w];
        }
    }

    // Normalise: divide by (n-1)(n-2) for directed-equivalent
    const double norm = (n > 2) ? 1.0 / ((n - 1.0) * (n - 2.0)) : 1.0;
    for (auto it = m_graph.begin(); it != m_graph.end(); ++it)
        it.value().betweenness = betw.value(it.key(), 0.0) * norm;
}

// ─── Community detection (greedy connected components via Union-Find) ─────────

void CoOffendingAnalyser::detectCommunities()
{
    m_ufParent.clear();
    for (const auto& pid : m_graph.keys()) m_ufParent[pid] = pid;

    for (const auto& [pid, node] : m_graph.asKeyValueRange()) {
        for (const auto& nid : node.neighbours.keys()) {
            ufUnion(pid, nid);
        }
    }

    // Assign community IDs as integer labels from roots
    QMap<QString, int> rootToId;
    int nextId = 0;
    for (auto it = m_graph.begin(); it != m_graph.end(); ++it) {
        const QString root = ufFind(it.key());
        if (!rootToId.contains(root)) rootToId[root] = nextId++;
        it.value().communityId = rootToId[root];
    }
}

// ─── Analyse ──────────────────────────────────────────────────────────────────

void CoOffendingAnalyser::analyse()
{
    if (!m_built) return;
    computePageRank();
    computeBetweenness();
    detectCommunities();
    m_analysed = true;
}

// ─── Risk scoring ─────────────────────────────────────────────────────────────

double CoOffendingAnalyser::riskScore(const NetworkNode& node, int nShared) const
{
    // Composite: 40% shared incidents, 30% PageRank, 20% betweenness, 10% degree
    const int n = m_graph.size();
    const double normDegree  = (n > 1) ? static_cast<double>(node.degree) / (n - 1) : 0.0;
    const double sharedScore = std::min(nShared / 5.0, 1.0);   // saturates at 5 shared

    return std::min(
        0.40 * sharedScore +
        0.30 * node.pageRank * n +   // un-normalise pageRank relative to uniform
        0.20 * node.betweenness +
        0.10 * normDegree,
        1.0);
}

// ─── Find leads ──────────────────────────────────────────────────────────────

QVector<NetworkLead> CoOffendingAnalyser::findLeads(
    const QString& incidentId, int topK) const
{
    if (!m_analysed) return {};

    // Direct participants in this incident
    const QSet<QString> directPersons =
        m_incidentPersons.value(incidentId, {});

    struct Candidate {
        QString personId;
        int     sharedIncidents = 0;
        QString connectionType;
        double  risk = 0.0;
    };

    QVector<Candidate> candidates;

    // Direct co-offenders
    for (const QString& pid : directPersons) {
        if (!m_graph.contains(pid)) continue;
        const auto& node = m_graph[pid];

        int sharedCount = 0;
        for (const auto& iid : node.incidentIds) {
            const auto& persons = m_incidentPersons.value(iid);
            if (persons.size() > 1 && persons.contains(pid))
                ++sharedCount;
        }

        candidates.append({pid, sharedCount, QStringLiteral("direct_participant"),
                           riskScore(node, sharedCount)});
    }

    // Second-degree: neighbours of direct participants NOT already in incident
    for (const QString& pid : directPersons) {
        if (!m_graph.contains(pid)) continue;
        for (const auto& nid : m_graph[pid].neighbours.keys()) {
            if (directPersons.contains(nid)) continue;
            if (!m_graph.contains(nid)) continue;

            // Count shared incidents with any direct participant
            int shared = 0;
            for (const auto& iid : m_graph[nid].incidentIds) {
                if (m_incidentPersons.contains(iid)) {
                    for (const auto& dp : directPersons) {
                        if (m_incidentPersons[iid].contains(dp)) { ++shared; break; }
                    }
                }
            }

            // Avoid duplicates
            bool already = false;
            for (const auto& c : candidates) { if (c.personId == nid) { already = true; break; } }
            if (!already)
                candidates.append({nid, shared, QStringLiteral("second_degree"),
                                   riskScore(m_graph[nid], shared)});
        }
    }

    // Sort by risk descending, take top-K
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.risk > b.risk; });

    if (candidates.size() > topK) candidates.resize(topK);

    QVector<NetworkLead> leads;
    for (const auto& c : candidates) {
        const auto& node = m_graph[c.personId];
        NetworkLead nl;
        nl.personId       = c.personId;
        nl.connectionType = c.connectionType;
        nl.sharedIncidents = c.sharedIncidents;
        nl.centralityScore = node.pageRank;
        nl.communityId    = node.communityId;
        nl.riskScore      = c.risk;
        nl.reasoning      = QStringLiteral(
            "PageRank=%1 Betweenness=%2 Degree=%3 Community=%4")
            .arg(node.pageRank,    0, 'f', 4)
            .arg(node.betweenness, 0, 'f', 4)
            .arg(node.degree)
            .arg(node.communityId);
        leads.append(nl);
    }
    return leads;
}

// ─── All nodes ────────────────────────────────────────────────────────────────

QVector<NetworkNode> CoOffendingAnalyser::nodes() const
{
    QVector<NetworkNode> result;
    result.reserve(m_graph.size());
    for (const auto& node : m_graph) result.append(node);
    return result;
}
