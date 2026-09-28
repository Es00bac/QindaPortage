// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ops/buildfiles.h"

#include <QJsonObject>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Ops {

// Everything SourceProbe learned about a piece of external software.
struct SourceInfo
{
    QString url;            // as given
    QString host;           // "github", "gitlab", "codeberg", "git", "tarball", "local"
    QString owner;
    QString repo;
    QString name;           // proposed PN
    QString description;
    QString homepage;
    QString spdxLicense;
    QString license;        // Gentoo license name(s)
    QStringList topics;
    QString language;

    QString defaultBranch;
    QString tag;            // release/tag the version comes from
    QString version;        // PMS version ("1.8.2" or "0_p20260928")
    bool snapshot = false;  // no usable tag: pinned commit
    QString commit;
    QString commitDate;     // ISO 8601
    QString tarballUrl;     // archive for SRC_URI
    QString archiveDir;     // top directory inside that archive (for S=)
    QString gitUrl;         // clone URL (git-r3 fallback)
    QString localPath;      // where the tree was analysed (clone/extract/local)

    QStringList files;      // top-level names; directories end in '/'
    QString buildSystem;    // see BuildFiles::detectBuildSystem
    QString pep517Backend;
    QVector<DeclaredDependency> dependencies;
    QMap<QString, QString> buildFiles; // file -> text (first 64 KiB)
    bool libraryOnly = false;
    bool pythonScripts = false;

    QStringList warnings;
    QString error;          // non-empty: the probe failed

    bool ok() const { return error.isEmpty(); }
    // Compact form for the assistant; build file text is included (each
    // cut to `fileChars`) only when asked.
    QJsonObject toJson(bool includeFiles = false, int fileChars = 4000) const;
};

} // namespace QindaPortage::Ops

Q_DECLARE_METATYPE(QindaPortage::Ops::SourceInfo)
