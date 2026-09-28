// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

namespace QindaPortage::Core {

class PortageConfig;
class RepoIndex;

// What a USE flag means: per-package (metadata.xml, use.local.desc),
// global (use.desc) and USE_EXPAND (profiles/desc/<var>.desc).
class UseDescriptions
{
public:
    void load(const PortageConfig &config);

    // Local description for `key` first, then global, then USE_EXPAND.
    // Empty when nothing describes the flag.
    QString describe(const QString &flag, const QString &key = QString()) const;
    // "local", "global", "expand" or "" for describe()'s answer.
    QString sourceOf(const QString &flag, const QString &key = QString()) const;

    QString global(const QString &flag) const { return m_global.value(flag); }
    // Flags that USE_EXPAND variables contribute, e.g. "video_cards".
    QStringList expandPrefixes() const { return m_expandPrefixes; }

    // Packages whose IUSE (any version) declares `flag`.
    static QStringList packagesUsing(const RepoIndex &index, const QString &flag, int limit = 500);

    // Parsers (exposed for tests).
    static QHash<QString, QString> parseDescFile(const QString &path, const QString &prefix = QString());
    static QHash<QString, QString> parseMetadataXml(const QString &path);

private:
    QString local(const QString &flag, const QString &key) const;

    QHash<QString, QString> m_global;
    QHash<QString, QString> m_expand;
    QHash<QString, QString> m_localDesc;  // "cat/pn:flag" from use.local.desc
    QStringList m_expandPrefixes;
    QStringList m_repoLocations;
    mutable QHash<QString, QHash<QString, QString>> m_metadataCache; // key -> flag -> text
};

} // namespace QindaPortage::Core
