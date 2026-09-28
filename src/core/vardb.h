// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/version.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

// One installed package, read from /var/db/pkg/<category>/<pf>/.
struct InstalledPackage
{
    QString category;
    QString name;
    QString versionText;  // "3.24.5-r1"
    Version version;
    QString slot;         // "0" or "3/3.24" (slot/sub-slot)
    QString repository;
    QString eapi;
    QString description;
    QString homepage;
    QString license;
    QString keywords;
    QSet<QString> use;          // enabled flags (USE file: effective, incl. arch/expand)
    QStringList iuse;           // declared flags without +/- defaults, file order
    QSet<QString> iuseDefaultOn;// flags declared "+flag"
    QString depend;
    QString rdepend;
    QString bdepend;
    QString pdepend;
    QString idepend;
    QStringList inherited;
    qint64 size = 0;            // bytes on disk
    qint64 buildTime = 0;       // unix time of the merge
    QString path;               // the vardb directory

    QString key() const { return category + QLatin1Char('/') + name; }
    QString cpv() const { return key() + QLatin1Char('-') + versionText; }
    QString mainSlot() const { return slot.section(QLatin1Char('/'), 0, 0); }
    QString subSlot() const { return slot.section(QLatin1Char('/'), 1, 1); }
    // Flags in IUSE that are enabled, in IUSE order.
    QStringList enabledIuse() const;
};

// A file owned by a package (CONTENTS).
struct OwnedFile
{
    enum Type { Dir, File, Symlink, Other };
    Type type = File;
    QString path;
    QString target; // symlink target
};

class VarDb
{
public:
    // Reads every package under `root` (default /var/db/pkg). Skips
    // entries that are being merged (-MERGING-) and unreadable ones.
    bool load(const QString &root = QStringLiteral("/var/db/pkg"));

    const QVector<InstalledPackage> &packages() const { return m_packages; }
    // Indexes of installed packages with this "category/name" key.
    QVector<int> byKey(const QString &key) const { return m_byKey.value(key); }
    int indexOfCpv(const QString &cpv) const { return m_byCpv.value(cpv, -1); }
    QString root() const { return m_root; }
    QString error() const { return m_error; }

    // Reads a package's CONTENTS file (not cached).
    static QVector<OwnedFile> contents(const InstalledPackage &pkg);
    // Which installed package owns `path` (scans CONTENTS; slow, ~1 s).
    QString ownerOf(const QString &path) const;

    // Parses a single vardb directory (exposed for tests).
    static bool readPackage(const QString &dir, const QString &category, const QString &pf,
                            InstalledPackage *out);

private:
    QVector<InstalledPackage> m_packages;
    QHash<QString, QVector<int>> m_byKey;
    QHash<QString, int> m_byCpv;
    QString m_root;
    QString m_error;
};

} // namespace QindaPortage::Core
