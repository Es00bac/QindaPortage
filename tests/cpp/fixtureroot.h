// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A config root for the repository fixtures in tests/data/repos: repos.conf
// needs absolute locations, so the root is built in a temporary directory.
//   gentoo — has metadata/md5-cache (exact metadata), profiles, metadata.xml
//   extra  — ebuilds only (scraped metadata)
// make.profile points at gentoo/profiles/base, which keywords
// =app-misc/newtool-0.9 for ~amd64.

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

inline bool makeFixtureRoot(QTemporaryDir &dir, const QString &unmask = QString())
{
    if (!dir.isValid()) return false;
    const QString data = QStringLiteral(TEST_DATA "/repos");
    const QString etc = dir.path() + QStringLiteral("/etc/portage");
    if (!QDir().mkpath(etc)) return false;
    QFile repos(etc + QStringLiteral("/repos.conf"));
    if (!repos.open(QIODevice::WriteOnly)) return false;
    repos.write(QStringLiteral("[DEFAULT]\nmain-repo = gentoo\n\n[gentoo]\nlocation = %1/gentoo\n\n"
                               "[extra]\nlocation = %1/extra\npriority = 50\n").arg(data).toUtf8());
    repos.close();
    if (!QFile::link(data + QStringLiteral("/gentoo/profiles/base"), etc + QStringLiteral("/make.profile"))) return false;
    if (!unmask.isEmpty()) {
        QFile f(etc + QStringLiteral("/package.unmask"));
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(unmask.toUtf8() + '\n');
    }
    return true;
}
