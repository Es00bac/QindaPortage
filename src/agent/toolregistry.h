// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tools the assistant may call. Handlers are asynchronous (pretend and
// source probes take seconds) and always answer with a JSON object; errors
// are answers too ({"error": ..., "didYouMean": [...]}) because a precise
// error is how a small model corrects itself.

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

namespace QindaPortage::Agent {

using ToolDone = std::function<void(const QJsonObject &result)>;
using ToolHandler = std::function<void(const QJsonObject &args, const ToolDone &done)>;

struct ToolSpec
{
    QString name;
    QString description;
    QJsonObject parameters;  // JSON schema of the arguments
    ToolHandler handler;
    bool mutatesPlan = false; // plan_* tools: shown differently in the UI
};

class ToolRegistry
{
public:
    void add(ToolSpec spec);
    const ToolSpec *find(const QString &name) const;
    QStringList names() const;
    // OpenAI "tools" array.
    QJsonArray toJson() const;

    // Schema helpers for concise declarations.
    static QJsonObject object(const QJsonObject &properties, const QStringList &required);
    static QJsonObject string(const QString &description);
    static QJsonObject stringArray(const QString &description);
    static QJsonObject integer(const QString &description);
    static QJsonObject boolean(const QString &description);

private:
    QVector<ToolSpec> m_tools;
};

} // namespace QindaPortage::Agent
