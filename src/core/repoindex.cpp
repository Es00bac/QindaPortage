// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/repoindex.h"

#include "core/atom.h"
#include "core/portageconfig.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <thread>
#include <vector>

namespace QindaPortage::Core {

namespace {

QStringList words(const QString &text)
{
    return text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QStringList stripIuseDefaults(const QString &iuse)
{
    QStringList out;
    for (QString f : iuse.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (f.startsWith(QLatin1Char('+')) || f.startsWith(QLatin1Char('-'))) f.remove(0, 1);
        if (!f.isEmpty() && !f.contains(QLatin1Char('$')) && !out.contains(f)) out.append(f);
    }
    return out;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

QStringList AvailablePackage::repositories() const
{
    QStringList out;
    for (const AvailableVersion &v : versions) {
        if (!out.contains(v.repo)) out.append(v.repo);
    }
    return out;
}

QMap<QString, QString> RepoIndex::parseCacheEntry(const QByteArray &data)
{
    QMap<QString, QString> out;
    for (const QByteArray &line : data.split('\n')) {
        const int eq = line.indexOf('=');
        if (eq <= 0) continue;
        out.insert(QString::fromLatin1(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)));
    }
    return out;
}

QMap<QString, QString> RepoIndex::scrapeEbuild(const QString &text)
{
    // AGENT-NOTE: approximate by design (see the class comment). Only the
    // first plain assignment of each variable is read; quotes may span lines.
    static const QRegularExpression re(QStringLiteral(
        "^[ \\t]*(DESCRIPTION|HOMEPAGE|SLOT|KEYWORDS|IUSE|LICENSE|EAPI)=(?:\"([^\"]*)\"|'([^']*)'|(\\S*))"),
        QRegularExpression::MultilineOption);
    QMap<QString, QString> out;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString name = m.captured(1);
        if (out.contains(name)) continue;
        QString value = m.captured(2);
        if (value.isEmpty()) value = m.captured(3);
        if (value.isEmpty()) value = m.captured(4);
        out.insert(name, value.simplified());
    }
    return out;
}

AvailablePackage &RepoIndex::packageFor(const QString &category, const QString &name)
{
    const QString key = category + QLatin1Char('/') + name;
    auto it = m_index.constFind(key);
    if (it != m_index.cend()) return m_packages[*it];
    AvailablePackage p;
    p.key = key;
    p.category = category;
    p.name = name;
    m_index.insert(key, m_packages.size());
    m_packages.append(std::move(p));
    return m_packages.last();
}

void RepoIndex::addVersion(const QString &category, const QString &pn, const QString &pv, const QString &repo,
                           const QMap<QString, QString> &meta, bool exact)
{
    AvailableVersion v;
    v.version = pv;
    v.parsed = Version::parse(pv);
    if (!v.parsed.isValid()) return;
    v.slot = meta.value(QStringLiteral("SLOT"), QStringLiteral("0"));
    if (v.slot.isEmpty()) v.slot = QStringLiteral("0");
    v.keywords = words(meta.value(QStringLiteral("KEYWORDS")));
    v.iuse = stripIuseDefaults(meta.value(QStringLiteral("IUSE")));
    v.license = meta.value(QStringLiteral("LICENSE"));
    v.eapi = meta.value(QStringLiteral("EAPI"));
    v.repo = repo;
    v.exact = exact;
    AvailablePackage &p = packageFor(category, pn);
    const bool highest = p.versions.isEmpty() || Version::compare(v.parsed, p.versions.last().parsed) >= 0;
    if (highest || p.description.isEmpty()) {
        p.description = meta.value(QStringLiteral("DESCRIPTION"));
        p.homepage = meta.value(QStringLiteral("HOMEPAGE")).section(QLatin1Char(' '), 0, 0);
    }
    p.versions.append(std::move(v));
    ++m_versionCount;
}

void RepoIndex::loadCacheRepo(const QString &repo, const QString &location)
{
    // AGENT-NOTE: ::gentoo's cache is ~33k small files; reading them is
    // I/O-latency bound when the page cache is cold (7 s single-threaded on
    // qinda-top), so categories are read on several threads and merged here
    // in category order.
    struct Entry { QString category, pn, pv; QMap<QString, QString> meta; };
    QDir cache(location + QStringLiteral("/metadata/md5-cache"));
    const QStringList categories = cache.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const int threads = std::clamp<int>(int(std::thread::hardware_concurrency()), 1, 8);
    std::vector<QVector<Entry>> perCategory(categories.size());
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            for (int c = t; c < categories.size(); c += threads) {
                const QString catPath = location + QStringLiteral("/metadata/md5-cache/") + categories.at(c);
                for (const QString &pf : QDir(catPath).entryList(QDir::Files, QDir::Name)) {
                    QString pn, pv;
                    if (!Version::splitNameVersion(pf, &pn, &pv)) continue;
                    perCategory[c].append({categories.at(c), pn, pv,
                                           parseCacheEntry(readAll(catPath + QLatin1Char('/') + pf))});
                }
            }
        });
    }
    for (auto &w : workers) w.join();
    for (const auto &list : perCategory) {
        for (const Entry &e : list) addVersion(e.category, e.pn, e.pv, repo, e.meta, true);
    }
}

void RepoIndex::loadEbuildRepo(const QString &repo, const QString &location)
{
    QDir root(location);
    for (const QString &category : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!category.contains(QLatin1Char('-')) && category != QLatin1String("virtual")) continue;
        QDir catDir(root.filePath(category));
        for (const QString &pn : catDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QDir pkgDir(catDir.filePath(pn));
            for (const QString &file : pkgDir.entryList({QStringLiteral("*.ebuild")}, QDir::Files, QDir::Name)) {
                const QString pf = file.chopped(7);
                QString name, pv;
                if (!Version::splitNameVersion(pf, &name, &pv) || name != pn) continue;
                const QString text = QString::fromUtf8(readAll(pkgDir.filePath(file)));
                addVersion(category, pn, pv, repo, scrapeEbuild(text), false);
            }
        }
    }
}

void RepoIndex::applyMasks(const PortageConfig &config)
{
    QVector<PackageLine> masks;
    for (const Repository &r : config.repositories()) {
        masks += PortageConfig::readPackageFile(r.location + QStringLiteral("/profiles/package.mask"));
    }
    masks += config.packageLines(QStringLiteral("mask"));
    const QVector<PackageLine> unmasks = config.packageLines(QStringLiteral("unmask"));
    auto matchesAny = [](const QVector<PackageLine> &lines, const AvailablePackage &p, const AvailableVersion &v) {
        for (const PackageLine &l : lines) {
            if (l.atom.isValid() && l.atom.key() == p.key
                && l.atom.matches(p.category, p.name, v.parsed, v.slot, v.repo)) {
                return true;
            }
        }
        return false;
    };
    QHash<QString, QVector<PackageLine>> byKey;
    for (const PackageLine &l : masks) {
        if (l.atom.isValid()) byKey[l.atom.key()].append(l);
    }
    QHash<QString, QVector<PackageLine>> unmaskByKey;
    for (const PackageLine &l : unmasks) {
        if (l.atom.isValid()) unmaskByKey[l.atom.key()].append(l);
    }
    for (AvailablePackage &p : m_packages) {
        const auto it = byKey.constFind(p.key);
        if (it == byKey.cend()) continue;
        for (AvailableVersion &v : p.versions) {
            v.masked = matchesAny(*it, p, v) && !matchesAny(unmaskByKey.value(p.key), p, v);
        }
    }
}

bool RepoIndex::load(const PortageConfig &config)
{
    m_packages.clear();
    m_index.clear();
    m_repoLocations.clear();
    m_repoHasCache.clear();
    m_versionCount = 0;
    for (const Repository &r : config.repositories()) {
        m_repoLocations.insert(r.name, r.location);
        m_repoHasCache.insert(r.name, r.hasMd5Cache);
        if (r.hasMd5Cache) loadCacheRepo(r.name, r.location);
        else loadEbuildRepo(r.name, r.location);
    }
    QSet<QString> cats;
    for (AvailablePackage &p : m_packages) {
        std::sort(p.versions.begin(), p.versions.end(), [](const auto &a, const auto &b) {
            return Version::compare(a.parsed, b.parsed) < 0;
        });
        cats.insert(p.category);
    }
    applyMasks(config);
    m_profileKeywords.clear();
    for (const QString &dir : config.profileChain()) {
        m_profileKeywords += PortageConfig::readPackageFile(dir + QStringLiteral("/package.accept_keywords"));
    }
    m_categories = QStringList(cats.begin(), cats.end());
    m_categories.sort();
    return !m_packages.isEmpty();
}

const AvailablePackage *RepoIndex::find(const QString &key) const
{
    const auto it = m_index.constFind(key);
    return it == m_index.cend() ? nullptr : &m_packages.at(*it);
}

bool RepoIndex::keywordsAccepted(const QStringList &keywords, const QStringList &accept)
{
    for (const QString &a : accept) {
        if (a == QLatin1String("**")) return true;
        if (a == QLatin1String("*") ) {
            for (const QString &k : keywords) if (!k.startsWith(QLatin1Char('~')) && !k.startsWith(QLatin1Char('-'))) return true;
        }
        if (a == QLatin1String("~*")) {
            for (const QString &k : keywords) if (k.startsWith(QLatin1Char('~'))) return true;
        }
        if (keywords.contains(a)) return true;
    }
    return false;
}

const AvailableVersion *RepoIndex::bestVisibleVersion(const QString &key, const QStringList &acceptKeywords,
                                                      const PortageConfig *config) const
{
    const AvailablePackage *p = find(key);
    if (!p) return nullptr;
    QString arch = config ? config->value(QStringLiteral("ARCH")) : QString();
    if (arch.isEmpty()) arch = QStringLiteral("amd64");
    for (int i = p->versions.size() - 1; i >= 0; --i) {
        const AvailableVersion &v = p->versions.at(i);
        if (v.masked) continue;
        QStringList accept = acceptKeywords;
        if (config) {
            for (const PackageLine &l : m_profileKeywords + config->packageLines(QStringLiteral("accept_keywords"))) {
                if (!l.atom.isValid() || l.atom.key() != key) continue;
                if (!l.atom.matches(p->category, p->name, v.parsed, v.slot, v.repo)) continue;
                accept += l.tokens.isEmpty() ? QStringList{QLatin1Char('~') + arch} : l.tokens;
            }
        }
        if (keywordsAccepted(v.keywords, accept)) return &v;
    }
    return nullptr;
}

QVector<SearchHit> RepoIndex::search(const QString &query, int limit) const
{
    QVector<SearchHit> hits;
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return hits;
    const QStringList qWords = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const AvailablePackage &p : m_packages) {
        const QString name = p.name.toLower();
        const QString key = p.key.toLower();
        int score = 0;
        if (name == q || key == q) score = 1000;
        else if (name.startsWith(q)) score = 800 - std::min<int>(100, name.size() - q.size());
        else if (name.contains(q)) score = 600 - std::min<int>(100, name.size() - q.size());
        else if (key.contains(q)) score = 500;
        else {
            const QString desc = p.description.toLower();
            bool all = true;
            for (const QString &w : qWords) {
                if (!desc.contains(w) && !name.contains(w)) { all = false; break; }
            }
            if (all) score = 200;
        }
        if (score == 0) continue;
        SearchHit h;
        h.key = p.key;
        h.description = p.description;
        h.bestVersion = p.versions.isEmpty() ? QString() : p.versions.last().version;
        h.score = score;
        if (m_installed) h.installed = m_installed(p.key);
        if (h.installed) h.score += 5; // a tie goes to what is already on the system
        hits.append(h);
    }
    std::sort(hits.begin(), hits.end(), [](const SearchHit &a, const SearchHit &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.key < b.key;
    });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

QMap<QString, QString> RepoIndex::metadata(const QString &key, const QString &version) const
{
    const AvailablePackage *p = find(key);
    if (!p) return {};
    for (const AvailableVersion &v : p->versions) {
        if (v.version != version) continue;
        const QString location = m_repoLocations.value(v.repo);
        if (m_repoHasCache.value(v.repo)) {
            return parseCacheEntry(readAll(location + QStringLiteral("/metadata/md5-cache/") + key
                                           + QLatin1Char('-') + version));
        }
        return scrapeEbuild(QString::fromUtf8(readAll(QStringLiteral("%1/%2/%3-%4.ebuild")
                                                           .arg(location, key, p->name, version))));
    }
    return {};
}

QMap<QString, QString> RepoIndex::fetchExactMetadata(const QString &cpv, const QStringList &keys)
{
    QMap<QString, QString> out;
    QProcess p;
    p.start(QStringLiteral("portageq"),
            QStringList{QStringLiteral("metadata"), QStringLiteral("/"), QStringLiteral("ebuild"), cpv} + keys);
    if (!p.waitForFinished(60000) || p.exitCode() != 0) return out;
    // One value per line, in the order asked.
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'));
    for (int i = 0; i < keys.size() && i < lines.size(); ++i) out.insert(keys.at(i), lines.at(i));
    return out;
}

} // namespace QindaPortage::Core
