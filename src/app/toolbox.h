// SPDX-License-Identifier: GPL-3.0-or-later
//
// Toolbox registers the assistant's tools against the app's live models
// (docs/design.md "The assistant"). Read-only tools answer from SystemModel
// and friends; plan_* tools only edit the draft plan. Every answer is compact
// JSON sized for a 16k-token context.

#pragma once

#include "agent/toolregistry.h"

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace QindaPortage::Ops {
class PretendRunner;
}

namespace QindaPortage::App {

class PlanModel;
class SystemModel;

class Toolbox final : public QObject
{
    Q_OBJECT

public:
    explicit Toolbox(SystemModel *system, QObject *parent = nullptr);

    Agent::ToolRegistry *registry() { return &m_registry; }

    // The "This machine:" block of the system prompt.
    Q_INVOKABLE QString systemProfile() const;
    // Deterministic hints for a request: words in the text that name
    // installed (or available) packages, resolved to exact atoms.
    QString requestHints(const QString &text) const;

    // Package keys whose name matches `name` exactly or closely (for
    // "didYouMean" answers).
    QStringList suggestKeys(const QString &atomOrName, int limit = 5) const;

    void registerCoreTools();
    // pretend, plan_*, build_log, web_fetch, search_bugs (toolbox_ops.cpp).
    void registerOpsTools(PlanModel *plan);

private:
    QJsonObject packageInfo(const QString &atom) const;
    QJsonObject readConfig(const QString &path) const;
    QJsonObject useFlagInfo(const QString &flag) const;
    QJsonObject reverseDeps(const QString &atom, bool transitive) const;
    QJsonObject findPackages(const QString &query) const;
    QString keyFromAtomText(const QString &text) const;

    void runPretend(const QJsonObject &args, const Agent::ToolDone &done);
    void webGet(const QUrl &url, const std::function<void(QByteArray, QString)> &done);
    QJsonObject planStep(const QJsonObject &stepJson);

    SystemModel *m_system;
    PlanModel *m_plan = nullptr;
    Ops::PretendRunner *m_pretend = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    Agent::ToolRegistry m_registry;
};

} // namespace QindaPortage::App
