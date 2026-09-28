// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/atom.h"
#include "core/vardb.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

class PortageConfig;

// The installed system as a graph: nodes are VarDb package indexes, edges
// are the dependencies in effect with the installed USE flags.
class PackageGraph
{
public:
    enum EdgeType : quint8 { Build = 1, Run = 2, Post = 4, Install = 8 };

    struct Edge
    {
        int from = -1;
        int to = -1;
        quint8 types = 0;     // EdgeType bits
        QString atom;         // the atom that selected `to` (first seen)
        QStringList conditions; // USE conditions controlling it ("!x" negated)
        bool anyOf = false;   // chosen from an || ( ) group
    };

    struct Unsatisfied
    {
        int from = -1;
        quint8 types = 0;
        QString requirement;  // atom text or "|| ( a b )"
    };

    enum Role { Orphan, WorldRoot, SystemRoot, Dependency };

    void build(const VarDb &db, const PortageConfig &config);

    int nodeCount() const { return m_db ? m_db->packages().size() : 0; }
    const QVector<Edge> &edges() const { return m_edges; }
    // Edge indexes.
    const QVector<int> &outEdges(int node) const { return m_out.at(node); }
    const QVector<int> &inEdges(int node) const { return m_in.at(node); }
    const QVector<Unsatisfied> &unsatisfied() const { return m_unsatisfied; }

    Role role(int node) const { return m_role.at(node); }
    bool isWorld(int node) const { return m_world.contains(node); }
    bool isSystem(int node) const { return m_system.contains(node); }
    // BFS distance from the nearest world/system root; -1 for orphans.
    int depth(int node) const { return m_depth.at(node); }
    QVector<int> orphans() const;
    QVector<int> worldNodes() const { return QVector<int>(m_world.begin(), m_world.end()); }

    // Shortest chain root -> ... -> node that explains why `node` is
    // installed (empty for orphans; a single element for roots).
    QVector<int> whyInstalled(int node) const;
    // All packages reachable from node through edges of `types` (excluding it).
    QSet<int> dependencies(int node, quint8 types, int maxDepth = -1) const;
    QSet<int> reverseDependencies(int node, quint8 types, int maxDepth = -1) const;
    // Neighbourhood for drawing: nodes within `depth` hops in the chosen
    // direction(s), capped at `limit` nodes (closest first).
    QVector<int> neighbourhood(int node, int depth, bool forward, bool reverse, quint8 types,
                               int limit) const;

    // Best installed provider of an atom (highest version matching), -1 if none.
    int resolve(const Atom &atom) const;

    static QString typesText(quint8 types);

private:
    void addEdge(int from, int to, quint8 type, const Atom &atom, const QStringList &conditions, bool anyOf);
    void addDeps(int from, const QString &spec, quint8 type);
    bool satisfiedByInstalled(const QVector<Atom> &alternative) const;

    const VarDb *m_db = nullptr;
    QVector<Edge> m_edges;
    QHash<quint64, int> m_edgeIndex;
    QVector<QVector<int>> m_out;
    QVector<QVector<int>> m_in;
    QVector<Unsatisfied> m_unsatisfied;
    QSet<int> m_world;
    QSet<int> m_system;
    QVector<Role> m_role;
    QVector<int> m_depth;
    QVector<int> m_parent; // BFS parent for whyInstalled
};

} // namespace QindaPortage::Core
