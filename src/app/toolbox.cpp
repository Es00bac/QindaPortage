// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/toolbox.h"

#include "app/systemmodel.h"
#include "core/atom.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

namespace QindaPortage::App {

using namespace Agent;
using namespace Core;

namespace {

QJsonObject error(const QString &message, const QStringList &didYouMean = {})
{
    QJsonObject o{{QStringLiteral("error"), message}};
    if (!didYouMean.isEmpty()) o[QStringLiteral("didYouMean")] = QJsonArray::fromStringList(didYouMean);
    return o;
}

QString roleText(PackageGraph::Role r)
{
    switch (r) {
    case PackageGraph::WorldRoot: return QStringLiteral("world (explicitly wanted)");
    case PackageGraph::SystemRoot: return QStringLiteral("system set");
    case PackageGraph::Dependency: return QStringLiteral("dependency");
    case PackageGraph::Orphan: return QStringLiteral("orphan (removable with depclean)");
    }
    return {};
}

} // namespace

Toolbox::Toolbox(SystemModel *system, QObject *parent)
    : QObject(parent)
    , m_system(system)
{
    registerCoreTools();
}

QString Toolbox::keyFromAtomText(const QString &text) const
{
    const QString t = text.trimmed();
    const Atom a = Atom::parse(t);
    if (a.isValid()) return a.key();
    if (Atom::isValidKey(t)) return t;
    return {};
}

QStringList Toolbox::suggestKeys(const QString &atomOrName, int limit) const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return {};
    QString name = atomOrName.trimmed();
    const Atom a = Atom::parse(name);
    if (a.isValid()) name = a.name();
    else if (name.contains(QLatin1Char('/'))) name = name.section(QLatin1Char('/'), 1);
    name = name.toLower();
    QStringList exact, prefix, contains;
    QString last;
    for (const InstalledPackage &p : s->db.packages()) {
        if (p.key() == last) continue;
        last = p.key();
        const QString n = p.name.toLower();
        if (n == name) exact.append(p.key());
        else if (n.startsWith(name)) prefix.append(p.key());
        else if (name.size() >= 3 && n.contains(name)) contains.append(p.key());
    }
    return (exact + prefix + contains).mid(0, limit);
}

QString Toolbox::systemProfile() const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return QStringLiteral("(system state still loading)");
    const PortageConfig &c = s->config;
    auto v = [&c](const char *name) { return c.value(QString::fromLatin1(name)); };
    QStringList repos;
    for (const Repository &r : c.repositories()) repos.append(r.name);
    QString use = v("USE");
    // The full USE is hundreds of expanded flags; the make.conf intent is
    // what matters, and package_info gives per-package detail.
    if (use.size() > 400) use = use.left(400) + QStringLiteral("…");
    return QStringLiteral(
               "- profile %1, CHOST %2, ACCEPT_KEYWORDS %3\n"
               "- MAKEOPTS %4; EMERGE_DEFAULT_OPTS %5\n"
               "- FEATURES %6\n"
               "- repositories: %7 (external software goes to the qinda-local overlay)\n"
               "- %8 packages installed, %9 in @world, %10 orphans\n"
               "- binary packages come from the qinda desktop's binhost when available")
        .arg(c.profileName(), v("CHOST"), v("ACCEPT_KEYWORDS"), v("MAKEOPTS"), v("EMERGE_DEFAULT_OPTS"),
             v("FEATURES"), repos.join(QStringLiteral(", ")))
        .arg(s->db.packages().size())
        .arg(s->graph.worldNodes().size())
        .arg(s->graph.orphans().size());
}

QString Toolbox::requestHints(const QString &text) const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return {};
    QStringList lines;
    static const QRegularExpression wordRe(QStringLiteral("[A-Za-z0-9][A-Za-z0-9+_.-]{1,}(?:/[A-Za-z0-9+_.-]+)?"));
    auto it = wordRe.globalMatch(text);
    QSet<QString> seen;
    while (it.hasNext() && lines.size() < 6) {
        const QString word = it.next().captured(0);
        if (word.size() < 2 || seen.contains(word.toLower())) continue;
        seen.insert(word.toLower());
        const QStringList keys = suggestKeys(word, 3);
        if (keys.isEmpty()) continue;
        const QString first = keys.first();
        // Only exact name matches are hints; prefixes are noise.
        if (first.section(QLatin1Char('/'), 1).toLower() != word.section(QLatin1Char('/'), -1).toLower()) continue;
        const int idx = m_system->indexOf(first);
        const InstalledPackage &p = s->db.packages().at(idx);
        lines.append(QStringLiteral("\"%1\" is installed as %2 (%3)").arg(word, p.cpv(), p.repository));
    }
    return lines.join(QLatin1Char('\n'));
}

QJsonObject Toolbox::findPackages(const QString &query) const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return error(QStringLiteral("system state is still loading"));
    const QString q = query.trimmed().toLower();
    if (q.isEmpty()) return error(QStringLiteral("empty query"));
    QJsonArray installed;
    QString last;
    for (const InstalledPackage &p : s->db.packages()) {
        if (installed.size() >= 15) break;
        if (p.key() == last) continue;
        if (p.key().toLower().contains(q) || p.description.toLower().contains(q)) {
            last = p.key();
            installed.append(QJsonObject{{QStringLiteral("atom"), p.key()},
                                         {QStringLiteral("version"), p.versionText},
                                         {QStringLiteral("description"), p.description.left(90)}});
        }
    }
    return QJsonObject{{QStringLiteral("installed"), installed}};
}

QJsonObject Toolbox::packageInfo(const QString &atom) const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return error(QStringLiteral("system state is still loading"));
    const QString key = keyFromAtomText(atom);
    const int i = key.isEmpty() ? -1 : m_system->indexOf(key);
    if (i < 0) {
        return error(QStringLiteral("%1 is not installed (or not a valid category/name)").arg(atom),
                     suggestKeys(atom));
    }
    const InstalledPackage &p = s->db.packages().at(i);
    const PackageGraph &g = s->graph;
    QJsonArray use;
    for (const QString &flag : p.iuse) {
        const UseOrigin o = s->config.useOrigin(p, flag);
        QString text = (o.enabled ? QStringLiteral("+") : QStringLiteral("-")) + flag;
        if (o.source == UseOrigin::PackageUse || o.source == UseOrigin::ProfileForce
            || o.source == UseOrigin::ProfileMask || o.source == UseOrigin::MakeConf) {
            text += QStringLiteral(" (") + QFileInfo(o.where).fileName() + QLatin1Char(')');
        }
        use.append(text);
    }
    QJsonArray deps;
    for (int e : g.outEdges(i)) {
        if (deps.size() >= 40) break;
        const auto &edge = g.edges().at(e);
        deps.append(s->db.packages().at(edge.to).key() + QStringLiteral(" [") + PackageGraph::typesText(edge.types)
                    + (edge.conditions.isEmpty() ? QString() : QStringLiteral(" if ") + edge.conditions.join(QLatin1Char(',')))
                    + QLatin1Char(']'));
    }
    QJsonArray rdeps;
    for (int e : g.inEdges(i)) {
        if (rdeps.size() >= 20) break;
        rdeps.append(s->db.packages().at(g.edges().at(e).from).key());
    }
    QJsonArray why;
    for (int n : g.whyInstalled(i)) why.append(s->db.packages().at(n).key());
    return QJsonObject{{QStringLiteral("atom"), p.cpv()}, {QStringLiteral("slot"), p.slot},
                       {QStringLiteral("repo"), p.repository}, {QStringLiteral("description"), p.description},
                       {QStringLiteral("role"), roleText(g.role(i))}, {QStringLiteral("use"), use},
                       {QStringLiteral("dependencies"), deps},
                       {QStringLiteral("reverseDependencies"), rdeps},
                       {QStringLiteral("reverseDependencyCount"), int(g.inEdges(i).size())},
                       {QStringLiteral("whyInstalled"), why}};
}

QJsonObject Toolbox::reverseDeps(const QString &atom, bool transitive) const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return error(QStringLiteral("system state is still loading"));
    const int i = m_system->indexOf(keyFromAtomText(atom));
    if (i < 0) return error(QStringLiteral("%1 is not installed").arg(atom), suggestKeys(atom));
    const PackageGraph &g = s->graph;
    QJsonArray direct;
    for (int e : g.inEdges(i)) {
        const auto &edge = g.edges().at(e);
        direct.append(QJsonObject{{QStringLiteral("atom"), s->db.packages().at(edge.from).key()},
                                  {QStringLiteral("type"), PackageGraph::typesText(edge.types)},
                                  {QStringLiteral("requires"), edge.atom},
                                  {QStringLiteral("onlyIf"), edge.conditions.join(QLatin1Char(','))}});
        if (direct.size() >= 40) break;
    }
    QJsonObject o{{QStringLiteral("direct"), direct}, {QStringLiteral("directCount"), int(g.inEdges(i).size())}};
    if (transitive) o[QStringLiteral("transitiveCount")] = int(g.reverseDependencies(i, 0xff).size());
    return o;
}

QJsonObject Toolbox::readConfig(const QString &path) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    // AGENT-GUARD: read-only allowlist; the model may not read arbitrary
    // files (secrets live elsewhere under /etc and $HOME).
    const bool allowed = canonical.startsWith(QStringLiteral("/etc/portage/"))
                         || canonical == QLatin1String("/etc/portage")
                         || canonical == QLatin1String("/var/lib/portage/world")
                         || canonical == QLatin1String("/var/lib/portage/world_sets")
                         || canonical.startsWith(QStringLiteral("/var/db/repos/"));
    if (canonical.isEmpty()) return error(QStringLiteral("%1 does not exist").arg(path));
    if (!allowed) return error(QStringLiteral("only /etc/portage, the world file and repositories are readable"));
    QFileInfo info(canonical);
    if (info.isDir()) {
        return QJsonObject{{QStringLiteral("directory"), canonical},
                           {QStringLiteral("entries"), QJsonArray::fromStringList(QDir(canonical).entryList(
                                                           QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name))}};
    }
    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly)) return error(QStringLiteral("cannot read %1").arg(canonical));
    const QByteArray data = f.read(12000);
    QJsonObject o{{QStringLiteral("path"), canonical}, {QStringLiteral("text"), QString::fromUtf8(data)}};
    if (!f.atEnd()) o[QStringLiteral("truncated")] = true;
    return o;
}

QJsonObject Toolbox::useFlagInfo(const QString &flag) const
{
    const SystemSnapshot *s = m_system->snapshot();
    if (!s) return error(QStringLiteral("system state is still loading"));
    QJsonArray on, off;
    for (const InstalledPackage &p : s->db.packages()) {
        if (!p.iuse.contains(flag)) continue;
        (p.use.contains(flag) ? on : off).append(p.key());
    }
    return QJsonObject{{QStringLiteral("flag"), flag},
                       {QStringLiteral("enabledOn"), on.size() > 30 ? QJsonArray() : on},
                       {QStringLiteral("disabledOn"), off.size() > 30 ? QJsonArray() : off},
                       {QStringLiteral("enabledCount"), on.size()}, {QStringLiteral("disabledCount"), off.size()}};
}

void Toolbox::registerCoreTools()
{
    using R = ToolRegistry;
    m_registry.add({QStringLiteral("system_profile"),
                    QStringLiteral("Profile, compiler and emerge settings, repositories and package counts of this machine."),
                    R::object({}, {}),
                    [this](const QJsonObject &, const ToolDone &done) {
                        done({{QStringLiteral("profile"), systemProfile()}});
                    }});
    m_registry.add({QStringLiteral("find_packages"),
                    QStringLiteral("Search packages by name or description. Use before naming any package."),
                    R::object({{QStringLiteral("query"), R::string(QStringLiteral("name or words, e.g. \"jq\" or \"json processor\""))}},
                              {QStringLiteral("query")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        done(findPackages(a.value(QStringLiteral("query")).toString()));
                    }});
    m_registry.add({QStringLiteral("package_info"),
                    QStringLiteral("An installed package: version, slot, repo, USE flags with where each is set, dependencies, reverse dependencies and why it is installed."),
                    R::object({{QStringLiteral("atom"), R::string(QStringLiteral("category/name"))}}, {QStringLiteral("atom")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        done(packageInfo(a.value(QStringLiteral("atom")).toString()));
                    }});
    m_registry.add({QStringLiteral("reverse_deps"),
                    QStringLiteral("Which installed packages depend on a package, how, and under which USE conditions."),
                    R::object({{QStringLiteral("atom"), R::string(QStringLiteral("category/name"))},
                               {QStringLiteral("transitive"), R::boolean(QStringLiteral("also count indirect dependents"))}},
                              {QStringLiteral("atom")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        done(reverseDeps(a.value(QStringLiteral("atom")).toString(),
                                         a.value(QStringLiteral("transitive")).toBool()));
                    }});
    m_registry.add({QStringLiteral("read_config"),
                    QStringLiteral("Read a file or list a directory under /etc/portage (make.conf, package.use/…), the world file, or a repository."),
                    R::object({{QStringLiteral("path"), R::string(QStringLiteral("absolute path"))}}, {QStringLiteral("path")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        done(readConfig(a.value(QStringLiteral("path")).toString()));
                    }});
    m_registry.add({QStringLiteral("use_flag_info"),
                    QStringLiteral("Which installed packages declare a USE flag and whether it is on or off for them."),
                    R::object({{QStringLiteral("flag"), R::string(QStringLiteral("flag name without +/-"))}}, {QStringLiteral("flag")}),
                    [this](const QJsonObject &a, const ToolDone &done) {
                        done(useFlagInfo(a.value(QStringLiteral("flag")).toString()));
                    }});
}

} // namespace QindaPortage::App
