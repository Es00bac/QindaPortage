// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/atom.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

struct InstalledPackage;

// One "atom tokens..." line of a package.* file (or profile file), with
// where it came from so the UI can say *why* a flag is set.
struct PackageLine
{
    Atom atom;
    QStringList tokens;
    QString file;
    int line = 0;
    QString comment;  // the "# ..." block directly above, if any
};

struct Repository
{
    QString name;
    QString location;
    int priority = 0;
    bool hasMd5Cache = false;
};

// Where a flag's effective state comes from, most specific first.
struct UseOrigin
{
    enum Source { Unknown, EbuildDefault, Profile, MakeConf, PackageUse, ProfileForce, ProfileMask, UserForce };
    Source source = Unknown;
    QString where; // "package.use/30-qindaqt:12", "make.conf", profile dir…
    bool enabled = false;
};

class PortageConfig
{
public:
    // Reads /etc/portage (or `configRoot` + "/etc/portage"), the profile
    // chain, repos.conf and the world file. `envValues` is the result of
    // `portageq envvar -v` (see loadEnvironment) and may be empty in tests.
    bool load(const QString &configRoot = QStringLiteral("/"));
    // Runs `portageq envvar -v` for the resolved values of the variables
    // the UI and assistant show. Blocking (~0.5 s); call off the GUI thread.
    static QMap<QString, QString> loadEnvironment();
    void setEnvironment(const QMap<QString, QString> &env) { m_env = env; }

    QString value(const QString &var) const { return m_env.value(var); }
    const QMap<QString, QString> &environment() const { return m_env; }

    const QVector<Repository> &repositories() const { return m_repos; }
    QString repositoryLocation(const QString &name) const;
    QStringList profileChain() const { return m_profiles; }
    QString profileName() const { return m_profileName; }

    QStringList worldAtoms() const { return m_world; }
    // @world as Portage sees it: @selected (world file), the sets named in
    // world_sets, and @profile (bare atoms in the profiles' packages files).
    QStringList selectedAtoms() const { return m_world + m_setAtoms + m_profileSet; }
    QStringList profileSetAtoms() const { return m_profileSet; }
    QStringList worldSets() const { return m_worldSets; }
    QStringList systemAtoms() const { return m_system; }

    // Parsed /etc/portage/package.<kind> entries, e.g. kind "use",
    // "accept_keywords", "mask", "unmask", "license", "env".
    QVector<PackageLine> packageLines(const QString &kind) const { return m_package.value(kind); }
    QStringList packageFiles(const QString &kind) const;

    // Explains the effective state of `flag` on `pkg`.
    UseOrigin useOrigin(const InstalledPackage &pkg, const QString &flag) const;
    // User package.use lines that match pkg (all versions/slots).
    QVector<PackageLine> userUseLinesFor(const InstalledPackage &pkg) const;

    QString configDir() const { return m_etc; }

    // Reads "atom tokens" lines from a file or every file of a directory
    // (lexical order, skipping backups), keeping preceding comments.
    static QVector<PackageLine> readPackageFile(const QString &path);

private:
    void loadRepos();
    void loadProfiles();
    void addProfile(const QString &dir);
    QStringList readLines(const QString &path) const;

    QString m_root;
    QString m_etc;
    QString m_profileName;
    QMap<QString, QString> m_env;
    QVector<Repository> m_repos;
    QStringList m_profiles;  // outermost parent first
    QStringList m_world;
    QStringList m_worldSets;
    QStringList m_system;
    QStringList m_profileSet;
    QStringList m_setAtoms;
    QHash<QString, QVector<PackageLine>> m_package;
    // Profile-level flag controls, later entries override earlier ones.
    QStringList m_useMask;
    QStringList m_useForce;
    QVector<PackageLine> m_profilePackageUse;
    QVector<PackageLine> m_profilePackageUseMask;
    QVector<PackageLine> m_profilePackageUseForce;
    QStringList m_profileUseDefaults; // USE from make.defaults, in order
    QStringList m_makeConfUse;        // USE= from make.conf tokens
};

} // namespace QindaPortage::Core
