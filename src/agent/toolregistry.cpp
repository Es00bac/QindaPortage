// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/toolregistry.h"

namespace QindaPortage::Agent {

void ToolRegistry::add(ToolSpec spec)
{
    for (ToolSpec &existing : m_tools) {
        if (existing.name == spec.name) {
            existing = std::move(spec);
            return;
        }
    }
    m_tools.append(std::move(spec));
}

const ToolSpec *ToolRegistry::find(const QString &name) const
{
    for (const ToolSpec &t : m_tools) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

QStringList ToolRegistry::names() const
{
    QStringList out;
    for (const ToolSpec &t : m_tools) out.append(t.name);
    return out;
}

QJsonArray ToolRegistry::toJson() const
{
    QJsonArray out;
    for (const ToolSpec &t : m_tools) {
        out.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), t.name},
                                                     {QStringLiteral("description"), t.description},
                                                     {QStringLiteral("parameters"), t.parameters}}}});
    }
    return out;
}

QJsonObject ToolRegistry::object(const QJsonObject &properties, const QStringList &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), QJsonArray::fromStringList(required)}};
}

QJsonObject ToolRegistry::string(const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("description"), description}};
}

QJsonObject ToolRegistry::stringArray(const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                       {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                       {QStringLiteral("description"), description}};
}

QJsonObject ToolRegistry::integer(const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                       {QStringLiteral("description"), description}};
}

QJsonObject ToolRegistry::boolean(const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                       {QStringLiteral("description"), description}};
}

} // namespace QindaPortage::Agent
