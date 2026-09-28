// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/usedescriptions.h"

#include "core/portageconfig.h"
#include "core/repoindex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

namespace QindaPortage::Core {

QHash<QString, QString> UseDescriptions::parseDescFile(const QString &path, const QString &prefix)
{
    // "flag - description" per line ("cat/pn:flag - description" in
    // use.local.desc); '#' comments.
    QHash<QString, QString> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int sep = line.indexOf(QStringLiteral(" - "));
        if (sep <= 0) continue;
        out.insert(prefix + line.left(sep).trimmed(), line.mid(sep + 3).trimmed());
    }
    return out;
}

QHash<QString, QString> UseDescriptions::parseMetadataXml(const QString &path)
{
    QHash<QString, QString> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    QXmlStreamReader xml(&f);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("flag")) continue;
        const QString name = xml.attributes().value(QLatin1String("name")).toString();
        // Inline <pkg>/<b> markup is flattened into text.
        const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        if (!name.isEmpty() && !out.contains(name)) out.insert(name, text);
    }
    return out;
}

void UseDescriptions::load(const PortageConfig &config)
{
    m_global.clear();
    m_expand.clear();
    m_localDesc.clear();
    m_expandPrefixes.clear();
    m_repoLocations.clear();
    m_metadataCache.clear();
    for (const Repository &r : config.repositories()) {
        m_repoLocations.append(r.location);
        const QString profiles = r.location + QStringLiteral("/profiles");
        const auto global = parseDescFile(profiles + QStringLiteral("/use.desc"));
        for (auto it = global.cbegin(); it != global.cend(); ++it) {
            if (!m_global.contains(it.key())) m_global.insert(it.key(), it.value());
        }
        const auto local = parseDescFile(profiles + QStringLiteral("/use.local.desc"));
        for (auto it = local.cbegin(); it != local.cend(); ++it) {
            if (!m_localDesc.contains(it.key())) m_localDesc.insert(it.key(), it.value());
        }
        QDir desc(profiles + QStringLiteral("/desc"));
        for (const QString &file : desc.entryList({QStringLiteral("*.desc")}, QDir::Files, QDir::Name)) {
            const QString prefix = file.chopped(5) + QLatin1Char('_');
            if (!m_expandPrefixes.contains(prefix.chopped(1))) m_expandPrefixes.append(prefix.chopped(1));
            const auto expand = parseDescFile(desc.filePath(file), prefix);
            for (auto it = expand.cbegin(); it != expand.cend(); ++it) {
                if (!m_expand.contains(it.key())) m_expand.insert(it.key(), it.value());
            }
        }
    }
}

QString UseDescriptions::local(const QString &flag, const QString &key) const
{
    if (key.isEmpty()) return {};
    auto it = m_metadataCache.constFind(key);
    if (it == m_metadataCache.cend()) {
        // AGENT-NOTE: metadata.xml is the modern source of local flag text
        // (use.local.desc is generated from it for ::gentoo only); read
        // lazily, first repository that has the package wins.
        QHash<QString, QString> flags;
        for (const QString &location : m_repoLocations) {
            const QString path = location + QLatin1Char('/') + key + QStringLiteral("/metadata.xml");
            if (QFileInfo::exists(path)) {
                flags = parseMetadataXml(path);
                break;
            }
        }
        it = m_metadataCache.insert(key, flags);
    }
    const QString fromXml = it->value(flag);
    if (!fromXml.isEmpty()) return fromXml;
    return m_localDesc.value(key + QLatin1Char(':') + flag);
}

QString UseDescriptions::describe(const QString &flag, const QString &key) const
{
    const QString l = local(flag, key);
    if (!l.isEmpty()) return l;
    const QString g = m_global.value(flag);
    if (!g.isEmpty()) return g;
    return m_expand.value(flag);
}

QString UseDescriptions::sourceOf(const QString &flag, const QString &key) const
{
    if (!local(flag, key).isEmpty()) return QStringLiteral("local");
    if (m_global.contains(flag)) return QStringLiteral("global");
    if (m_expand.contains(flag)) return QStringLiteral("expand");
    return {};
}

QStringList UseDescriptions::packagesUsing(const RepoIndex &index, const QString &flag, int limit)
{
    QStringList out;
    for (const AvailablePackage &p : index.packages()) {
        for (const AvailableVersion &v : p.versions) {
            if (v.iuse.contains(flag)) {
                out.append(p.key);
                break;
            }
        }
        if (out.size() >= limit) break;
    }
    out.sort();
    return out;
}

} // namespace QindaPortage::Core
