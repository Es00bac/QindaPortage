// SPDX-License-Identifier: GPL-3.0-or-later
//
// SystemModel is the QML face of src/core: it loads the installed-package
// database, Portage configuration and dependency graph on a worker thread,
// swaps the finished snapshot in on the GUI thread, and answers the views'
// questions (rows for the package table, one package's details, a graph
// neighbourhood with layout, a disk-use treemap).

#pragma once

#include "core/packagegraph.h"
#include "core/portageconfig.h"
#include "core/vardb.h"

#include <QFutureWatcher>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace QindaPortage::App {

struct SystemSnapshot
{
    Core::VarDb db;
    Core::PortageConfig config;
    Core::PackageGraph graph;
    qint64 loadMs = 0;
};

class SystemModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int revision READ revision NOTIFY reloaded)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY reloaded)
    Q_PROPERTY(QVariantList repositories READ repositories NOTIFY reloaded)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY reloaded)
    Q_PROPERTY(QVariantList environment READ environment NOTIFY environmentChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY reloaded)

public:
    explicit SystemModel(QObject *parent = nullptr);
    ~SystemModel() override;

    bool loading() const { return m_loading; }
    int revision() const { return m_revision; }
    QVariantMap stats() const;
    QVariantList repositories() const;
    QVariantList categories() const;
    QVariantList environment() const;
    QString profileName() const;

    // Direct access for the assistant's toolbox (GUI thread only).
    const SystemSnapshot *snapshot() const { return m_snapshot.get(); }
    int indexOf(const QString &keyOrCpv) const;

    // Package table rows: filter text (name/description), role ("all",
    // "world", "system", "dependency", "orphan"), repo ("" = all), sort key
    // ("name", "version", "repo", "size", "installed", "deps", "rdeps").
    Q_INVOKABLE QVariantList packageRows(const QString &filter, const QString &role, const QString &repo,
                                         const QString &sortKey, bool descending) const;
    Q_INVOKABLE QVariantMap packageDetail(const QString &key) const;
    Q_INVOKABLE QVariantList useFlags(const QString &key) const;
    Q_INVOKABLE QVariantList dependencies(const QString &key, bool reverse) const;
    Q_INVOKABLE QVariantList whyInstalled(const QString &key) const;
    Q_INVOKABLE QVariantList files(const QString &key, int limit) const;
    // Nodes {id, key, label, version, role, layer, x, y, w, highlight} and
    // edges {from, to, types} laid out in columns: reverse dependencies to
    // the left, the package in the middle, dependencies to the right.
    Q_INVOKABLE QVariantMap neighbourhood(const QString &key, int depth, bool forward, bool reverse,
                                          int edgeTypes, int limit) const;
    // Squarified treemap of installed size by category (or by package
    // inside `category`): {id, label, bytes, count, x, y, w, h}.
    Q_INVOKABLE QVariantList treemap(const QString &category, double width, double height) const;
    Q_INVOKABLE QString formatBytes(double bytes) const;

public Q_SLOTS:
    void reload();

Q_SIGNALS:
    void loadingChanged();
    void reloaded();
    void environmentChanged();
    void loadFailed(const QString &error);

private:
    QVariantMap nodeMap(int index) const;

    std::unique_ptr<SystemSnapshot> m_snapshot;
    QFutureWatcher<SystemSnapshot *> m_watcher;
    QFutureWatcher<QMap<QString, QString>> m_envWatcher;
    QMap<QString, QString> m_env;
    bool m_loading = false;
    int m_revision = 0;
};

} // namespace QindaPortage::App
