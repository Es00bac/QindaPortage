// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/portageconfig.h"
#include "core/version.h"

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace QindaPortage::Core {

class PortageConfig;

struct AvailableVersion
{
    QString version;       // "1.8.2-r1"
    Version parsed;
    QString slot;          // "0/1"
    QStringList keywords;  // "amd64", "~arm64", "-*"
    QStringList iuse;      // without +/- defaults
    QString license;
    QString eapi;
    QString repo;
    bool masked = false;   // package.mask (repo profiles + user), minus user unmask
    bool exact = true;     // false: parsed from ebuild text (approximate)
};

struct AvailablePackage
{
    QString key;           // category/name
    QString category;
    QString name;
    QString description;   // of the highest version
    QString homepage;
    QVector<AvailableVersion> versions; // ascending

    QStringList repositories() const;
};

struct SearchHit
{
    QString key;
    QString description;
    QString bestVersion;   // highest visible, else highest
    int score = 0;
    bool installed = false;
};

// Every package available from the configured repositories.
//
// AGENT-NOTE: repositories with metadata/md5-cache (gentoo, sloom-private)
// are read from the cache, which is exact. Repositories without one (guru,
// qindaqt) are read by a cheap regex over the ebuild text: variables set
// through eclasses or ${…} expansions are missed, so those versions carry
// exact=false. fetchExactMetadata() asks Portage (portageq) for the truth.
class RepoIndex
{
public:
    // Blocking; ~1 s warm on qinda-top (≈20k packages). Run off the GUI thread.
    bool load(const PortageConfig &config);

    int count() const { return m_packages.size(); }
    int versionCount() const { return m_versionCount; }
    const QVector<AvailablePackage> &packages() const { return m_packages; }
    const AvailablePackage *find(const QString &key) const;
    QStringList categories() const { return m_categories; }

    // Ranked: exact name > name prefix > name substring > key substring >
    // all description words. Empty query returns nothing.
    QVector<SearchHit> search(const QString &query, int limit = 50) const;

    // Highest version that is not masked and whose KEYWORDS intersect the
    // accepted keywords ("amd64 ~amd64", "**" accepts anything).
    // Per-package package.accept_keywords from `config` (and from the
    // profile chain, read at load()) are honoured when config is given. Returns nullptr when nothing is visible.
    const AvailableVersion *bestVisibleVersion(const QString &key, const QStringList &acceptKeywords,
                                               const PortageConfig *config = nullptr) const;
    static bool keywordsAccepted(const QStringList &keywords, const QStringList &accept);

    // Cache entry (md5-cache) or regex scrape for one version.
    QMap<QString, QString> metadata(const QString &key, const QString &version) const;
    // Exact metadata through `portageq metadata / ebuild <cpv> <keys>`.
    // Blocking (~1 s). Returns key -> value for the keys asked.
    static QMap<QString, QString> fetchExactMetadata(const QString &cpv, const QStringList &keys);

    // Marks installed packages in search results.
    void setInstalledLookup(std::function<bool(const QString &key)> lookup) { m_installed = std::move(lookup); }

    // Exposed for tests.
    static QMap<QString, QString> parseCacheEntry(const QByteArray &data);
    static QMap<QString, QString> scrapeEbuild(const QString &text);

private:
    AvailablePackage &packageFor(const QString &category, const QString &name);
    void loadCacheRepo(const QString &repo, const QString &location);
    void loadEbuildRepo(const QString &repo, const QString &location);
    void addVersion(const QString &category, const QString &pn, const QString &pv, const QString &repo,
                    const QMap<QString, QString> &meta, bool exact);
    void applyMasks(const PortageConfig &config);

    QVector<AvailablePackage> m_packages;
    QHash<QString, int> m_index;
    QHash<QString, QString> m_repoLocations;
    QHash<QString, bool> m_repoHasCache;
    QStringList m_categories;
    QVector<PackageLine> m_profileKeywords; // profiles' package.accept_keywords
    int m_versionCount = 0;
    std::function<bool(const QString &)> m_installed;
};

} // namespace QindaPortage::Core
