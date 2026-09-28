// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/portageconfig.h"

#include "core/shellvars.h"
#include "core/vardb.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>

namespace QindaPortage::Core {

namespace {

QStringList splitWords(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

// "VIDEO_CARDS: amdgpu radeonsi" -> "video_cards_amdgpu video_cards_radeonsi".
QStringList expandUseTokens(const QStringList &tokens)
{
    QStringList out;
    QString prefix;
    for (const QString &t : tokens) {
        if (t.endsWith(QLatin1Char(':'))) {
            prefix = t.chopped(1).toLower() + QLatin1Char('_');
            continue;
        }
        if (prefix.isEmpty()) {
            out.append(t);
        } else if (t.startsWith(QLatin1Char('-'))) {
            out.append(QLatin1Char('-') + prefix + t.mid(1));
        } else {
            out.append(prefix + t);
        }
    }
    return out;
}

// Result of stacking incremental USE tokens for one flag: 1 on, 0 off,
// -1 not mentioned.
int stackFlag(const QStringList &tokens, const QString &flag)
{
    int state = -1;
    for (const QString &t : tokens) {
        if (t == QLatin1String("-*")) state = 0;
        else if (t == flag) state = 1;
        else if (t.startsWith(QLatin1Char('-')) && t.mid(1) == flag) state = 0;
    }
    return state;
}

bool lineMatches(const PackageLine &line, const InstalledPackage &pkg)
{
    const Atom &a = line.atom;
    if (a.text() == QLatin1String("*/*")) return true;
    return a.matches(pkg.category, pkg.name, pkg.version, pkg.slot, pkg.repository);
}

} // namespace

QVector<PackageLine> PortageConfig::readPackageFile(const QString &path)
{
    QVector<PackageLine> out;
    QFileInfo info(path);
    if (info.isDir()) {
        QDir dir(path);
        for (const QString &name : dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            if (name.startsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char('~'))) continue;
            out += readPackageFile(dir.filePath(name));
        }
        return out;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    QStringList comment;
    int number = 0;
    while (!f.atEnd()) {
        ++number;
        const QString raw = QString::fromUtf8(f.readLine());
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#'))) {
            comment.append(line.mid(1).trimmed());
            continue;
        }
        const QString body = line.section(QLatin1Char('#'), 0, 0).trimmed();
        if (body.isEmpty()) {
            comment.clear();
            continue;
        }
        QStringList tokens = splitWords(body);
        PackageLine pl;
        // AGENT-NOTE: "*/*" is not a valid atom; the invalid Atom keeps its
        // text and lineMatches() recognises it by that.
        pl.atom = Atom::parse(tokens.takeFirst());
        pl.tokens = tokens;
        pl.file = path;
        pl.line = number;
        pl.comment = comment.join(QLatin1Char('\n'));
        comment.clear();
        out.append(pl);
    }
    return out;
}

QStringList PortageConfig::readLines(const QString &path) const
{
    QStringList out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).section(QLatin1Char('#'), 0, 0).trimmed();
        if (!line.isEmpty()) out.append(line);
    }
    return out;
}

bool PortageConfig::load(const QString &configRoot)
{
    m_root = configRoot.endsWith(QLatin1Char('/')) ? configRoot : configRoot + QLatin1Char('/');
    m_etc = m_root + QStringLiteral("etc/portage");
    loadRepos();
    loadProfiles();

    for (const char *kind : {"use", "accept_keywords", "mask", "unmask", "license", "env", "keywords"}) {
        const QString k = QString::fromLatin1(kind);
        m_package.insert(k, readPackageFile(m_etc + QStringLiteral("/package.") + k));
    }
    m_world = readLines(m_root + QStringLiteral("var/lib/portage/world"));
    m_worldSets = readLines(m_root + QStringLiteral("var/lib/portage/world_sets"));
    // User sets named in world_sets ("@name" -> /etc/portage/sets/name).
    m_setAtoms.clear();
    for (const QString &set : m_worldSets) {
        if (set.startsWith(QLatin1Char('@'))) {
            m_setAtoms += readLines(m_etc + QStringLiteral("/sets/") + set.mid(1));
        }
    }

    const ShellVars makeConf = ShellVars::readFile(
        QFileInfo(m_etc + QStringLiteral("/make.conf")).isDir()
            ? QString() : m_etc + QStringLiteral("/make.conf"));
    m_makeConfUse = splitWords(makeConf.value(QStringLiteral("USE")));
    // USE_EXPAND variables set in make.conf, flattened to flag form.
    for (auto it = makeConf.values().cbegin(); it != makeConf.values().cend(); ++it) {
        if (it.key() == QLatin1String("USE") || it.key().contains(QLatin1String("FLAGS"))) continue;
        static const QStringList expand = {
            QStringLiteral("VIDEO_CARDS"), QStringLiteral("INPUT_DEVICES"), QStringLiteral("L10N"),
            QStringLiteral("PYTHON_TARGETS"), QStringLiteral("PYTHON_SINGLE_TARGET"),
            QStringLiteral("LLVM_SLOT"), QStringLiteral("CPU_FLAGS_X86"), QStringLiteral("AMDGPU_TARGETS"),
            QStringLiteral("LUA_TARGETS"), QStringLiteral("RUBY_TARGETS"), QStringLiteral("GRUB_PLATFORMS")};
        if (!expand.contains(it.key())) continue;
        for (const QString &v : splitWords(it.value())) {
            m_makeConfUse.append(it.key().toLower() + QLatin1Char('_') + v);
        }
    }
    return true;
}

void PortageConfig::loadRepos()
{
    m_repos.clear();
    const QString conf = m_etc + QStringLiteral("/repos.conf");
    QStringList files;
    if (QFileInfo(conf).isDir()) {
        for (const QString &n : QDir(conf).entryList(QDir::Files, QDir::Name)) {
            if (!n.startsWith(QLatin1Char('.'))) files.append(conf + QLatin1Char('/') + n);
        }
    } else if (QFileInfo::exists(conf)) {
        files.append(conf);
    }
    for (const QString &file : files) {
        QSettings ini(file, QSettings::IniFormat);
        for (const QString &group : ini.childGroups()) {
            if (group == QLatin1String("DEFAULT") || group == QLatin1String("General")) continue;
            Repository repo;
            repo.name = group;
            repo.location = ini.value(group + QStringLiteral("/location")).toString();
            repo.priority = ini.value(group + QStringLiteral("/priority")).toInt();
            if (repo.location.isEmpty()) continue;
            repo.hasMd5Cache = QFileInfo(repo.location + QStringLiteral("/metadata/md5-cache")).isDir();
            bool replaced = false;
            for (Repository &existing : m_repos) {
                if (existing.name == repo.name) { existing = repo; replaced = true; }
            }
            if (!replaced) m_repos.append(repo);
        }
    }
    if (repositoryLocation(QStringLiteral("gentoo")).isEmpty()) {
        m_repos.prepend({QStringLiteral("gentoo"), m_root + QStringLiteral("var/db/repos/gentoo"), -1000,
                         QFileInfo(m_root + QStringLiteral("var/db/repos/gentoo/metadata/md5-cache")).isDir()});
    }
}

QString PortageConfig::repositoryLocation(const QString &name) const
{
    for (const Repository &r : m_repos) {
        if (r.name == name) return r.location;
    }
    return {};
}

void PortageConfig::addProfile(const QString &dir)
{
    const QString canonical = QFileInfo(dir).canonicalFilePath();
    if (canonical.isEmpty() || m_profiles.contains(canonical)) return;
    for (const QString &parent : readLines(canonical + QStringLiteral("/parent"))) {
        if (!parent.startsWith(QLatin1Char('/')) && parent.contains(QLatin1Char(':'))) {
            const QString repo = parent.section(QLatin1Char(':'), 0, 0);
            const QString rel = parent.section(QLatin1Char(':'), 1);
            addProfile(repositoryLocation(repo) + QStringLiteral("/profiles/") + rel);
        } else {
            addProfile(QFileInfo(canonical + QLatin1Char('/') + parent).absoluteFilePath());
        }
    }
    m_profiles.append(canonical);
}

void PortageConfig::loadProfiles()
{
    m_profiles.clear();
    const QString link = m_etc + QStringLiteral("/make.profile");
    addProfile(link);
    const QString leaf = m_profiles.isEmpty() ? QString() : m_profiles.last();
    // "qindaqt:qindaqt/systemd" style name for display.
    for (const Repository &r : m_repos) {
        const QString base = QFileInfo(r.location).canonicalFilePath() + QStringLiteral("/profiles/");
        if (!leaf.isEmpty() && leaf.startsWith(base)) {
            m_profileName = r.name + QLatin1Char(':') + leaf.mid(base.size());
        }
    }
    if (QFileInfo(m_etc + QStringLiteral("/profile")).isDir()) {
        m_profiles.append(QFileInfo(m_etc + QStringLiteral("/profile")).canonicalFilePath());
    }

    m_system.clear();
    m_profileSet.clear();
    m_useMask.clear();
    m_useForce.clear();
    m_profilePackageUse.clear();
    m_profilePackageUseMask.clear();
    m_profilePackageUseForce.clear();
    m_profileUseDefaults.clear();
    for (const QString &dir : m_profiles) {
        // "*atom" is @system, a bare atom is @profile (part of @world).
        for (const QString &line : readLines(dir + QStringLiteral("/packages"))) {
            if (line.startsWith(QStringLiteral("-*"))) m_system.removeAll(line.mid(2));
            else if (line.startsWith(QLatin1Char('*'))) m_system.append(line.mid(1));
            else if (line.startsWith(QLatin1Char('-'))) m_profileSet.removeAll(line.mid(1));
            else m_profileSet.append(line);
        }
        m_useMask += readLines(dir + QStringLiteral("/use.mask"));
        m_useForce += readLines(dir + QStringLiteral("/use.force"));
        m_profilePackageUse += readPackageFile(dir + QStringLiteral("/package.use"));
        m_profilePackageUseMask += readPackageFile(dir + QStringLiteral("/package.use.mask"));
        m_profilePackageUseForce += readPackageFile(dir + QStringLiteral("/package.use.force"));
        const ShellVars defaults = ShellVars::readFile(dir + QStringLiteral("/make.defaults"));
        m_profileUseDefaults += splitWords(defaults.value(QStringLiteral("USE")));
    }
    m_system.removeDuplicates();
    m_profileSet.removeDuplicates();
}

QStringList PortageConfig::packageFiles(const QString &kind) const
{
    const QString path = m_etc + QStringLiteral("/package.") + kind;
    QFileInfo info(path);
    if (!info.exists()) return {};
    if (!info.isDir()) return {path};
    QStringList out;
    for (const QString &n : QDir(path).entryList(QDir::Files, QDir::Name)) {
        if (!n.startsWith(QLatin1Char('.')) && !n.endsWith(QLatin1Char('~'))) out.append(path + QLatin1Char('/') + n);
    }
    return out;
}

QVector<PackageLine> PortageConfig::userUseLinesFor(const InstalledPackage &pkg) const
{
    QVector<PackageLine> out;
    for (const PackageLine &line : m_package.value(QStringLiteral("use"))) {
        if (lineMatches(line, pkg)) out.append(line);
    }
    return out;
}

UseOrigin PortageConfig::useOrigin(const InstalledPackage &pkg, const QString &flag) const
{
    UseOrigin origin;
    origin.enabled = pkg.use.contains(flag);

    auto lastMatch = [&](const QVector<PackageLine> &lines, bool expand) -> const PackageLine * {
        const PackageLine *found = nullptr;
        for (const PackageLine &line : lines) {
            if (!lineMatches(line, pkg)) continue;
            const QStringList tokens = expand ? expandUseTokens(line.tokens) : line.tokens;
            if (stackFlag(tokens, flag) >= 0) found = &line;
        }
        return found;
    };
    auto where = [](const PackageLine *l) {
        return QStringLiteral("%1:%2").arg(l->file).arg(l->line);
    };

    if (const PackageLine *l = lastMatch(m_profilePackageUseForce, false); l && origin.enabled) {
        return {UseOrigin::ProfileForce, where(l), true};
    }
    if (stackFlag(m_useForce, flag) == 1 && origin.enabled) {
        return {UseOrigin::ProfileForce, QStringLiteral("profile use.force"), true};
    }
    if (const PackageLine *l = lastMatch(m_profilePackageUseMask, false); l && !origin.enabled) {
        return {UseOrigin::ProfileMask, where(l), false};
    }
    if (stackFlag(m_useMask, flag) == 1 && !origin.enabled) {
        return {UseOrigin::ProfileMask, QStringLiteral("profile use.mask"), false};
    }
    if (const PackageLine *l = lastMatch(m_package.value(QStringLiteral("use")), true)) {
        return {UseOrigin::PackageUse, where(l), origin.enabled};
    }
    if (const PackageLine *l = lastMatch(m_profilePackageUse, true)) {
        return {UseOrigin::Profile, where(l), origin.enabled};
    }
    if (stackFlag(m_makeConfUse, flag) >= 0) {
        return {UseOrigin::MakeConf, QStringLiteral("make.conf"), origin.enabled};
    }
    if (stackFlag(m_profileUseDefaults, flag) >= 0) {
        return {UseOrigin::Profile, QStringLiteral("profile make.defaults"), origin.enabled};
    }
    if (pkg.iuseDefaultOn.contains(flag)) {
        return {UseOrigin::EbuildDefault, QStringLiteral("IUSE default (+%1)").arg(flag), origin.enabled};
    }
    origin.source = origin.enabled ? UseOrigin::Profile : UseOrigin::EbuildDefault;
    origin.where = origin.enabled ? QStringLiteral("profile") : QStringLiteral("default (off)");
    return origin;
}

QMap<QString, QString> PortageConfig::loadEnvironment()
{
    static const QStringList vars = {
        QStringLiteral("ARCH"), QStringLiteral("CHOST"), QStringLiteral("CFLAGS"), QStringLiteral("CXXFLAGS"),
        QStringLiteral("LDFLAGS"), QStringLiteral("MAKEOPTS"), QStringLiteral("EMERGE_DEFAULT_OPTS"),
        QStringLiteral("FEATURES"), QStringLiteral("ACCEPT_KEYWORDS"), QStringLiteral("ACCEPT_LICENSE"),
        QStringLiteral("USE"), QStringLiteral("VIDEO_CARDS"), QStringLiteral("PYTHON_TARGETS"),
        QStringLiteral("PYTHON_SINGLE_TARGET"), QStringLiteral("CPU_FLAGS_X86"), QStringLiteral("L10N"),
        QStringLiteral("PORTAGE_TMPDIR"), QStringLiteral("DISTDIR"), QStringLiteral("PKGDIR"),
        QStringLiteral("PORTAGE_BINHOST"), QStringLiteral("GENTOO_MIRRORS"), QStringLiteral("LLVM_SLOT")};
    QMap<QString, QString> out;
    QProcess p;
    p.start(QStringLiteral("portageq"), QStringList{QStringLiteral("envvar"), QStringLiteral("-v")} + vars);
    if (!p.waitForFinished(30000)) return out;
    // "VAR='value'" per line, shell-quoted.
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString v = line.mid(eq + 1);
        if (v.size() >= 2 && (v.startsWith(QLatin1Char('\'')) || v.startsWith(QLatin1Char('"')))) {
            v = v.mid(1, v.size() - 2);
        }
        out.insert(line.left(eq), v);
    }
    return out;
}

} // namespace QindaPortage::Core
