// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/packagegraph.h"

#include "core/depspec.h"
#include "core/portageconfig.h"

#include <QQueue>

#include <algorithm>

namespace QindaPortage::Core {

namespace {

QStringList conditionsFor(const DepNode &root, const Atom &atom)
{
    for (const auto &pair : DepSpec::atomsWithConditions(root)) {
        if (pair.first.text() == atom.text()) return pair.second;
    }
    return {};
}

QString requirementText(const DepRequirement &req)
{
    if (!req.isAnyOf()) {
        QStringList atoms;
        for (const Atom &a : req.alternatives.value(0)) atoms.append(a.text());
        return atoms.join(QLatin1Char(' '));
    }
    QStringList alts;
    for (const auto &alt : req.alternatives) {
        QStringList atoms;
        for (const Atom &a : alt) atoms.append(a.text());
        alts.append(atoms.size() == 1 ? atoms.first() : QStringLiteral("( %1 )").arg(atoms.join(QLatin1Char(' '))));
    }
    return QStringLiteral("|| ( %1 )").arg(alts.join(QLatin1Char(' ')));
}

} // namespace

int PackageGraph::resolve(const Atom &atom) const
{
    if (!m_db || !atom.isValid() || atom.blocker() != Atom::NoBlock) return -1;
    int best = -1;
    const auto &pkgs = m_db->packages();
    for (int idx : m_db->byKey(atom.key())) {
        const InstalledPackage &p = pkgs.at(idx);
        // Sub-slot is ignored here: a stale sub-slot is a pending rebuild, not a
        // missing dependency.
        if (!atom.matches(p.category, p.name, p.version, p.mainSlot(), p.repository)) continue;
        if (best < 0 || Version::compare(p.version, pkgs.at(best).version) > 0) best = idx;
    }
    return best;
}

bool PackageGraph::satisfiedByInstalled(const QVector<Atom> &alternative) const
{
    for (const Atom &a : alternative) {
        if (a.blocker() != Atom::NoBlock) continue;
        if (resolve(a) < 0) return false;
    }
    return true;
}

void PackageGraph::addEdge(int from, int to, quint8 type, const Atom &atom, const QStringList &conditions,
                           bool anyOf)
{
    if (from == to) return;
    const quint64 key = (quint64(quint32(from)) << 32) | quint32(to);
    if (auto it = m_edgeIndex.constFind(key); it != m_edgeIndex.cend()) {
        m_edges[*it].types |= type;
        return;
    }
    Edge e;
    e.from = from;
    e.to = to;
    e.types = type;
    e.atom = atom.text();
    e.conditions = conditions;
    e.anyOf = anyOf;
    const int index = m_edges.size();
    m_edges.append(e);
    m_edgeIndex.insert(key, index);
    m_out[from].append(index);
    m_in[to].append(index);
}

void PackageGraph::addDeps(int from, const QString &spec, quint8 type)
{
    if (spec.isEmpty()) return;
    const InstalledPackage &pkg = m_db->packages().at(from);
    const DepNode root = DepSpec::parse(spec);
    for (const DepRequirement &req : DepSpec::evaluate(root, pkg.use)) {
        // AGENT-NOTE: like Portage, an || ( ) group is satisfied by the first
        // alternative that is fully installed; that alternative becomes the
        // edge. None installed -> unsatisfied (normal for BDEPEND after
        // --with-bdeps=n cleaning).
        const QVector<Atom> *chosen = nullptr;
        for (const auto &alt : req.alternatives) {
            if (satisfiedByInstalled(alt)) { chosen = &alt; break; }
        }
        if (!chosen) {
            bool onlyBlockers = true;
            for (const auto &alt : req.alternatives) {
                for (const Atom &a : alt) onlyBlockers &= a.blocker() != Atom::NoBlock;
            }
            if (!onlyBlockers) m_unsatisfied.append({from, type, requirementText(req)});
            continue;
        }
        for (const Atom &a : *chosen) {
            if (a.blocker() != Atom::NoBlock) continue;
            const int to = resolve(a);
            if (to >= 0) addEdge(from, to, type, a, conditionsFor(root, a), req.isAnyOf());
        }
    }
}

void PackageGraph::build(const VarDb &db, const PortageConfig &config)
{
    m_db = &db;
    const int n = db.packages().size();
    m_edges.clear();
    m_edgeIndex.clear();
    m_unsatisfied.clear();
    m_out = QVector<QVector<int>>(n);
    m_in = QVector<QVector<int>>(n);
    m_world.clear();
    m_system.clear();

    for (int i = 0; i < n; ++i) {
        const InstalledPackage &p = db.packages().at(i);
        addDeps(i, p.depend, Build);
        addDeps(i, p.bdepend, Build);
        addDeps(i, p.rdepend, Run);
        addDeps(i, p.pdepend, Post);
        addDeps(i, p.idepend, Install);
    }

    auto markRoots = [&](const QStringList &atoms, QSet<int> *into) {
        for (const QString &text : atoms) {
            const Atom a = Atom::parse(text);
            if (!a.isValid()) continue;
            // A world entry keeps every matching installed slot.
            for (int idx : db.byKey(a.key())) {
                const InstalledPackage &p = db.packages().at(idx);
                if (a.matches(p.category, p.name, p.version, p.slot, p.repository)) into->insert(idx);
            }
        }
    };
    markRoots(config.selectedAtoms(), &m_world);
    markRoots(config.systemAtoms(), &m_system);

    // BFS from all roots over every edge type (depclean keeps build deps
    // with --with-bdeps=y, which is Portage's default for depclean).
    m_depth = QVector<int>(n, -1);
    m_parent = QVector<int>(n, -1);
    m_role = QVector<Role>(n, Orphan);
    QQueue<int> queue;
    for (int r : m_system) { m_depth[r] = 0; m_role[r] = SystemRoot; queue.enqueue(r); }
    for (int r : m_world) {
        if (m_depth[r] < 0) queue.enqueue(r);
        m_depth[r] = 0;
        m_role[r] = WorldRoot;
    }
    while (!queue.isEmpty()) {
        const int cur = queue.dequeue();
        for (int e : m_out.at(cur)) {
            const int next = m_edges.at(e).to;
            if (m_depth[next] >= 0) continue;
            m_depth[next] = m_depth[cur] + 1;
            m_parent[next] = cur;
            m_role[next] = Dependency;
            queue.enqueue(next);
        }
    }
}

QVector<int> PackageGraph::orphans() const
{
    QVector<int> out;
    for (int i = 0; i < m_role.size(); ++i) {
        if (m_role.at(i) == Orphan) out.append(i);
    }
    return out;
}

QVector<int> PackageGraph::whyInstalled(int node) const
{
    QVector<int> chain;
    if (node < 0 || node >= m_depth.size() || m_depth.at(node) < 0) return chain;
    for (int cur = node; cur >= 0; cur = m_parent.at(cur)) chain.prepend(cur);
    return chain;
}

static QSet<int> walk(const PackageGraph &g, int node, quint8 types, int maxDepth, bool forward)
{
    QSet<int> seen;
    QQueue<QPair<int, int>> queue;
    queue.enqueue({node, 0});
    while (!queue.isEmpty()) {
        const auto [cur, d] = queue.dequeue();
        if (maxDepth >= 0 && d >= maxDepth) continue;
        const auto &list = forward ? g.outEdges(cur) : g.inEdges(cur);
        for (int e : list) {
            const auto &edge = g.edges().at(e);
            if (!(edge.types & types)) continue;
            const int next = forward ? edge.to : edge.from;
            if (next == node || seen.contains(next)) continue;
            seen.insert(next);
            queue.enqueue({next, d + 1});
        }
    }
    return seen;
}

QSet<int> PackageGraph::dependencies(int node, quint8 types, int maxDepth) const
{
    return walk(*this, node, types, maxDepth, true);
}

QSet<int> PackageGraph::reverseDependencies(int node, quint8 types, int maxDepth) const
{
    return walk(*this, node, types, maxDepth, false);
}

QVector<int> PackageGraph::neighbourhood(int node, int depth, bool forward, bool reverse, quint8 types,
                                         int limit) const
{
    QVector<int> out{node};
    QSet<int> seen{node};
    QVector<int> frontier{node};
    for (int d = 0; d < depth && out.size() < limit; ++d) {
        QVector<int> next;
        for (int cur : frontier) {
            auto visit = [&](int other) {
                if (seen.contains(other) || out.size() >= limit) return;
                seen.insert(other);
                out.append(other);
                next.append(other);
            };
            if (forward) {
                for (int e : m_out.at(cur)) if (m_edges.at(e).types & types) visit(m_edges.at(e).to);
            }
            if (reverse) {
                for (int e : m_in.at(cur)) if (m_edges.at(e).types & types) visit(m_edges.at(e).from);
            }
        }
        frontier = next;
    }
    return out;
}

QString PackageGraph::typesText(quint8 types)
{
    QStringList parts;
    if (types & Build) parts.append(QStringLiteral("build"));
    if (types & Run) parts.append(QStringLiteral("run"));
    if (types & Post) parts.append(QStringLiteral("post"));
    if (types & Install) parts.append(QStringLiteral("install"));
    return parts.join(QLatin1Char('+'));
}

} // namespace QindaPortage::Core
