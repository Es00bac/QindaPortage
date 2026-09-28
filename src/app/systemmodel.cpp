// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/systemmodel.h"

#include "app/treemap.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QLocale>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace QindaPortage::App {

using namespace Core;

namespace {

QString roleName(PackageGraph::Role r)
{
    switch (r) {
    case PackageGraph::WorldRoot: return QStringLiteral("world");
    case PackageGraph::SystemRoot: return QStringLiteral("system");
    case PackageGraph::Dependency: return QStringLiteral("dependency");
    case PackageGraph::Orphan: return QStringLiteral("orphan");
    }
    return {};
}

QString originName(UseOrigin::Source s)
{
    switch (s) {
    case UseOrigin::EbuildDefault: return QStringLiteral("default");
    case UseOrigin::Profile: return QStringLiteral("profile");
    case UseOrigin::MakeConf: return QStringLiteral("make.conf");
    case UseOrigin::PackageUse: return QStringLiteral("package.use");
    case UseOrigin::ProfileForce: return QStringLiteral("forced");
    case UseOrigin::ProfileMask: return QStringLiteral("masked");
    case UseOrigin::UserForce: return QStringLiteral("forced");
    case UseOrigin::Unknown: break;
    }
    return QStringLiteral("unknown");
}

} // namespace

SystemModel::SystemModel(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<SystemSnapshot *>::finished, this, [this] {
        std::unique_ptr<SystemSnapshot> fresh(m_watcher.result());
        m_loading = false;
        Q_EMIT loadingChanged();
        if (!fresh || fresh->db.packages().isEmpty()) {
            Q_EMIT loadFailed(fresh ? fresh->db.error() : QStringLiteral("load failed"));
            return;
        }
        m_snapshot = std::move(fresh);
        ++m_revision;
        Q_EMIT reloaded();
    });
    connect(&m_envWatcher, &QFutureWatcher<QMap<QString, QString>>::finished, this, [this] {
        m_env = m_envWatcher.result();
        if (m_snapshot) m_snapshot->config.setEnvironment(m_env);
        Q_EMIT environmentChanged();
    });
}

SystemModel::~SystemModel()
{
    m_watcher.waitForFinished();
    m_envWatcher.waitForFinished();
}

void SystemModel::reload()
{
    if (m_loading) return;
    m_loading = true;
    Q_EMIT loadingChanged();
    m_watcher.setFuture(QtConcurrent::run([]() -> SystemSnapshot * {
        QElapsedTimer t;
        t.start();
        auto *s = new SystemSnapshot;
        s->db.load();
        s->config.load();
        // AGENT-GUARD: the graph keeps a pointer to its VarDb. The snapshot
        // is heap-allocated once and only its owning pointer moves, so the
        // address stays valid.
        s->graph.build(s->db, s->config);
        s->loadMs = t.elapsed();
        return s;
    }));
    m_envWatcher.setFuture(QtConcurrent::run([] { return PortageConfig::loadEnvironment(); }));
}

int SystemModel::indexOf(const QString &keyOrCpv) const
{
    if (!m_snapshot) return -1;
    const int exact = m_snapshot->db.indexOfCpv(keyOrCpv);
    if (exact >= 0) return exact;
    const QVector<int> list = m_snapshot->db.byKey(keyOrCpv);
    return list.isEmpty() ? -1 : list.last();
}

QString SystemModel::formatBytes(double bytes) const
{
    return QLocale().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

QVariantMap SystemModel::stats() const
{
    if (!m_snapshot) return {};
    const auto &g = m_snapshot->graph;
    qint64 bytes = 0;
    for (const auto &p : m_snapshot->db.packages()) bytes += p.size;
    int system = 0;
    for (int i = 0; i < g.nodeCount(); ++i) system += g.isSystem(i) ? 1 : 0;
    return {{QStringLiteral("installed"), m_snapshot->db.packages().size()},
            {QStringLiteral("world"), g.worldNodes().size()},
            {QStringLiteral("system"), system},
            {QStringLiteral("orphans"), g.orphans().size()},
            {QStringLiteral("edges"), g.edges().size()},
            {QStringLiteral("unsatisfied"), g.unsatisfied().size()},
            {QStringLiteral("bytes"), double(bytes)},
            {QStringLiteral("loadMs"), m_snapshot->loadMs},
            {QStringLiteral("worldFile"), m_snapshot->config.worldAtoms().size()},
            {QStringLiteral("profileSet"), m_snapshot->config.profileSetAtoms().size()}};
}

QString SystemModel::profileName() const
{
    return m_snapshot ? m_snapshot->config.profileName() : QString();
}

QVariantList SystemModel::repositories() const
{
    if (!m_snapshot) return {};
    QHash<QString, int> counts;
    QHash<QString, double> sizes;
    for (const auto &p : m_snapshot->db.packages()) {
        counts[p.repository]++;
        sizes[p.repository] += p.size;
    }
    QVariantList out;
    for (const Repository &r : m_snapshot->config.repositories()) {
        out.append(QVariantMap{{QStringLiteral("name"), r.name}, {QStringLiteral("location"), r.location},
                               {QStringLiteral("installed"), counts.value(r.name)},
                               {QStringLiteral("bytes"), sizes.value(r.name)}});
        counts.remove(r.name);
    }
    // Packages from repositories that are no longer configured.
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        out.append(QVariantMap{{QStringLiteral("name"), it.key().isEmpty() ? QStringLiteral("(unknown)") : it.key()},
                               {QStringLiteral("location"), QString()}, {QStringLiteral("installed"), it.value()},
                               {QStringLiteral("bytes"), sizes.value(it.key())}, {QStringLiteral("gone"), true}});
    }
    return out;
}

QVariantList SystemModel::categories() const
{
    if (!m_snapshot) return {};
    QMap<QString, QPair<int, double>> cats;
    for (const auto &p : m_snapshot->db.packages()) {
        cats[p.category].first++;
        cats[p.category].second += p.size;
    }
    QVariantList out;
    for (auto it = cats.cbegin(); it != cats.cend(); ++it) {
        out.append(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("count"), it.value().first},
                               {QStringLiteral("bytes"), it.value().second}});
    }
    return out;
}

QVariantList SystemModel::environment() const
{
    QVariantList out;
    for (auto it = m_env.cbegin(); it != m_env.cend(); ++it) {
        out.append(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("value"), it.value()}});
    }
    return out;
}

QVariantList SystemModel::packageRows(const QString &filter, const QString &role, const QString &repo,
                                      const QString &sortKey, bool descending) const
{
    if (!m_snapshot) return {};
    const auto &pkgs = m_snapshot->db.packages();
    const auto &g = m_snapshot->graph;
    const QString needle = filter.trimmed().toLower();
    QVector<int> rows;
    for (int i = 0; i < pkgs.size(); ++i) {
        const InstalledPackage &p = pkgs.at(i);
        if (!repo.isEmpty() && p.repository != repo) continue;
        if (!role.isEmpty() && role != QLatin1String("all") && roleName(g.role(i)) != role) continue;
        if (!needle.isEmpty() && !p.key().toLower().contains(needle) && !p.description.toLower().contains(needle)) {
            continue;
        }
        rows.append(i);
    }
    auto value = [&](int i) -> QVariant {
        const InstalledPackage &p = pkgs.at(i);
        if (sortKey == QLatin1String("size")) return double(p.size);
        if (sortKey == QLatin1String("installed")) return double(p.buildTime);
        if (sortKey == QLatin1String("repo")) return p.repository;
        if (sortKey == QLatin1String("deps")) return g.outEdges(i).size();
        if (sortKey == QLatin1String("rdeps")) return g.inEdges(i).size();
        return p.key();
    };
    std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
        if (sortKey == QLatin1String("version")) {
            const int c = Version::compare(pkgs.at(a).version, pkgs.at(b).version);
            return descending ? c > 0 : c < 0;
        }
        const int c = QVariant::compare(value(a), value(b)) == QPartialOrdering::Less ? -1
                      : QVariant::compare(value(a), value(b)) == QPartialOrdering::Greater ? 1 : 0;
        return descending ? c > 0 : c < 0;
    });
    QVariantList out;
    out.reserve(rows.size());
    for (int i : rows) {
        const InstalledPackage &p = pkgs.at(i);
        out.append(QVariantMap{
            {QStringLiteral("key"), p.key()}, {QStringLiteral("name"), p.name},
            {QStringLiteral("category"), p.category}, {QStringLiteral("version"), p.versionText},
            {QStringLiteral("slot"), p.slot}, {QStringLiteral("repo"), p.repository},
            {QStringLiteral("size"), formatBytes(p.size)}, {QStringLiteral("bytes"), double(p.size)},
            {QStringLiteral("installed"), QDateTime::fromSecsSinceEpoch(p.buildTime).toString(QStringLiteral("yyyy-MM-dd"))},
            {QStringLiteral("role"), roleName(g.role(i))}, {QStringLiteral("deps"), g.outEdges(i).size()},
            {QStringLiteral("rdeps"), g.inEdges(i).size()}, {QStringLiteral("description"), p.description}});
    }
    return out;
}

QVariantMap SystemModel::nodeMap(int i) const
{
    const InstalledPackage &p = m_snapshot->db.packages().at(i);
    return {{QStringLiteral("key"), p.key()}, {QStringLiteral("version"), p.versionText},
            {QStringLiteral("role"), roleName(m_snapshot->graph.role(i))}};
}

QVariantMap SystemModel::packageDetail(const QString &key) const
{
    const int i = indexOf(key);
    if (i < 0) return {};
    const InstalledPackage &p = m_snapshot->db.packages().at(i);
    const auto &g = m_snapshot->graph;
    QStringList slotList;
    for (int other : m_snapshot->db.byKey(p.key())) {
        slotList.append(m_snapshot->db.packages().at(other).versionText + QLatin1Char(':')
                     + m_snapshot->db.packages().at(other).slot);
    }
    return {{QStringLiteral("key"), p.key()}, {QStringLiteral("cpv"), p.cpv()},
            {QStringLiteral("version"), p.versionText}, {QStringLiteral("slot"), p.slot},
            {QStringLiteral("repo"), p.repository}, {QStringLiteral("description"), p.description},
            {QStringLiteral("homepage"), p.homepage}, {QStringLiteral("license"), p.license},
            {QStringLiteral("eapi"), p.eapi}, {QStringLiteral("size"), formatBytes(p.size)},
            {QStringLiteral("installed"), QDateTime::fromSecsSinceEpoch(p.buildTime).toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
            {QStringLiteral("role"), roleName(g.role(i))}, {QStringLiteral("depth"), g.depth(i)},
            {QStringLiteral("deps"), g.outEdges(i).size()}, {QStringLiteral("rdeps"), g.inEdges(i).size()},
            {QStringLiteral("inherited"), p.inherited.join(QLatin1Char(' '))},
            {QStringLiteral("installedSlots"), slotList},
            {QStringLiteral("allRdeps"), g.reverseDependencies(i, 0xff).size()}};
}

QVariantList SystemModel::useFlags(const QString &key) const
{
    const int i = indexOf(key);
    if (i < 0) return {};
    const InstalledPackage &p = m_snapshot->db.packages().at(i);
    QVariantList out;
    for (const QString &flag : p.iuse) {
        const UseOrigin o = m_snapshot->config.useOrigin(p, flag);
        out.append(QVariantMap{{QStringLiteral("flag"), flag}, {QStringLiteral("enabled"), o.enabled},
                               {QStringLiteral("origin"), originName(o.source)}, {QStringLiteral("where"), o.where},
                               {QStringLiteral("locked"), o.source == UseOrigin::ProfileForce
                                                           || o.source == UseOrigin::ProfileMask}});
    }
    return out;
}

QVariantList SystemModel::dependencies(const QString &key, bool reverse) const
{
    const int i = indexOf(key);
    if (i < 0) return {};
    const auto &g = m_snapshot->graph;
    QVariantList out;
    for (int e : reverse ? g.inEdges(i) : g.outEdges(i)) {
        const auto &edge = g.edges().at(e);
        QVariantMap m = nodeMap(reverse ? edge.from : edge.to);
        m.insert(QStringLiteral("types"), PackageGraph::typesText(edge.types));
        m.insert(QStringLiteral("atom"), edge.atom);
        m.insert(QStringLiteral("conditions"), edge.conditions.join(QStringLiteral(", ")));
        m.insert(QStringLiteral("anyOf"), edge.anyOf);
        out.append(m);
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("key")).toString() < b.toMap().value(QStringLiteral("key")).toString();
    });
    return out;
}

QVariantList SystemModel::whyInstalled(const QString &key) const
{
    const int i = indexOf(key);
    if (i < 0) return {};
    QVariantList out;
    for (int n : m_snapshot->graph.whyInstalled(i)) out.append(nodeMap(n));
    return out;
}

QVariantList SystemModel::files(const QString &key, int limit) const
{
    const int i = indexOf(key);
    if (i < 0) return {};
    QVariantList out;
    for (const OwnedFile &f : VarDb::contents(m_snapshot->db.packages().at(i))) {
        if (f.type == OwnedFile::Dir) continue;
        out.append(QVariantMap{{QStringLiteral("path"), f.path},
                               {QStringLiteral("link"), f.type == OwnedFile::Symlink ? f.target : QString()}});
        if (out.size() >= limit) break;
    }
    return out;
}

QVariantMap SystemModel::neighbourhood(const QString &key, int depth, bool forward, bool reverse, int edgeTypes,
                                       int limit) const
{
    const int center = indexOf(key);
    if (center < 0) return {};
    const auto &g = m_snapshot->graph;
    const auto &pkgs = m_snapshot->db.packages();
    const quint8 types = quint8(edgeTypes);
    // Column = signed hop distance (deps positive, reverse deps negative).
    QHash<int, int> column{{center, 0}};
    auto expand = [&](bool fwd) {
        QVector<int> frontier{center};
        for (int d = 1; d <= depth && column.size() < limit; ++d) {
            QVector<int> next;
            for (int cur : frontier) {
                for (int e : fwd ? g.outEdges(cur) : g.inEdges(cur)) {
                    const auto &edge = g.edges().at(e);
                    if (!(edge.types & types)) continue;
                    const int other = fwd ? edge.to : edge.from;
                    if (column.contains(other) || column.size() >= limit) continue;
                    column.insert(other, fwd ? d : -d);
                    next.append(other);
                }
            }
            frontier = next;
        }
    };
    if (forward) expand(true);
    if (reverse) expand(false);

    QMap<int, QVector<int>> byColumn;
    for (auto it = column.cbegin(); it != column.cend(); ++it) byColumn[it.value()].append(it.key());
    // AGENT-CONTRACT: DependencyGraphView.qml draws nodes at x/y with width
    // w and height NodeHeight; keep the constants in step.
    constexpr double ColumnWidth = 250, NodeWidth = 210, RowHeight = 34;
    int tallest = 0;
    for (const auto &list : byColumn) tallest = std::max<int>(tallest, list.size());
    QVariantList nodes;
    for (auto it = byColumn.begin(); it != byColumn.end(); ++it) {
        auto &list = it.value();
        std::sort(list.begin(), list.end(), [&](int a, int b) { return pkgs.at(a).key() < pkgs.at(b).key(); });
        const double top = (tallest - list.size()) * RowHeight / 2.0;
        for (int row = 0; row < list.size(); ++row) {
            const int n = list.at(row);
            QVariantMap m = nodeMap(n);
            m.insert(QStringLiteral("id"), n);
            m.insert(QStringLiteral("label"), pkgs.at(n).name);
            m.insert(QStringLiteral("category"), pkgs.at(n).category);
            m.insert(QStringLiteral("layer"), it.key());
            m.insert(QStringLiteral("x"), (it.key() - byColumn.firstKey()) * ColumnWidth);
            m.insert(QStringLiteral("y"), top + row * RowHeight);
            m.insert(QStringLiteral("w"), NodeWidth);
            m.insert(QStringLiteral("center"), n == center);
            nodes.append(m);
        }
    }
    QVariantList edges;
    for (auto it = column.cbegin(); it != column.cend(); ++it) {
        for (int e : g.outEdges(it.key())) {
            const auto &edge = g.edges().at(e);
            if (!(edge.types & types) || !column.contains(edge.to)) continue;
            // Only edges between adjacent or same columns keep the drawing legible.
            if (std::abs(column.value(edge.to) - it.value()) > 1) continue;
            edges.append(QVariantMap{{QStringLiteral("from"), edge.from}, {QStringLiteral("to"), edge.to},
                                     {QStringLiteral("types"), PackageGraph::typesText(edge.types)}});
        }
    }
    return {{QStringLiteral("nodes"), nodes}, {QStringLiteral("edges"), edges},
            {QStringLiteral("columns"), byColumn.size()}, {QStringLiteral("rows"), tallest},
            {QStringLiteral("truncated"), column.size() >= limit}};
}

QVariantList SystemModel::treemap(const QString &category, double width, double height) const
{
    if (!m_snapshot || width <= 0 || height <= 0) return {};
    QVector<TreemapItem> items;
    if (category.isEmpty()) {
        for (const QVariant &v : categories()) {
            const QVariantMap m = v.toMap();
            items.append({m.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("name")).toString(),
                          m.value(QStringLiteral("bytes")).toDouble(), m.value(QStringLiteral("count")).toInt()});
        }
    } else {
        for (const auto &p : m_snapshot->db.packages()) {
            if (p.category == category) items.append({p.key(), p.name, double(p.size), 1});
        }
    }
    double maxValue = 1;
    for (const TreemapItem &t : items) maxValue = std::max(maxValue, t.value);
    QVariantList out;
    for (const TreemapItem &t : squarify(items, width, height)) {
        out.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("label"), t.label},
                               {QStringLiteral("weight"), std::sqrt(t.value / maxValue)},
                               {QStringLiteral("bytes"), t.value}, {QStringLiteral("size"), formatBytes(t.value)},
                               {QStringLiteral("count"), t.count}, {QStringLiteral("x"), t.x},
                               {QStringLiteral("y"), t.y}, {QStringLiteral("w"), t.w}, {QStringLiteral("h"), t.h}});
    }
    return out;
}

} // namespace QindaPortage::App
