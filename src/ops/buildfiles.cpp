// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/buildfiles.h"

#include "core/version.h"

#include <QRegularExpression>

namespace QindaPortage::Ops {

QString DeclaredDependency::kindName(Kind k)
{
    switch (k) {
    case CMakePackage: return QStringLiteral("cmake");
    case PkgConfig: return QStringLiteral("pkg-config");
    case Meson: return QStringLiteral("meson");
    case Python: return QStringLiteral("python");
    case Cargo: return QStringLiteral("cargo");
    case Go: return QStringLiteral("go");
    case Autoconf: return QStringLiteral("autoconf-lib");
    case Node: return QStringLiteral("node");
    }
    return {};
}

QJsonObject DeclaredDependency::toJson() const
{
    QJsonObject o{{QStringLiteral("kind"), kindName(kind)}, {QStringLiteral("name"), name}};
    if (!components.isEmpty()) o[QStringLiteral("components")] = components.join(QLatin1Char(' '));
    if (!constraint.isEmpty()) o[QStringLiteral("constraint")] = constraint;
    if (optional) o[QStringLiteral("optional")] = true;
    if (build) o[QStringLiteral("build")] = true;
    return o;
}

namespace BuildFiles {

namespace {

// Removes "# …" comments line by line (CMake, Meson, TOML, shell, m4 "dnl").
QString stripComments(const QString &text, const QString &marker = QStringLiteral("#"))
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        int cut = -1;
        bool inQuote = false;
        QChar quote;
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line.at(i);
            if (inQuote) { if (c == quote) inQuote = false; continue; }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { inQuote = true; quote = c; continue; }
            if (line.mid(i, marker.size()) == marker) { cut = i; break; }
        }
        out.append(cut >= 0 ? line.left(cut) : line);
    }
    return out.join(QLatin1Char('\n'));
}

// The body of a TOML table: lines after "[table]" up to the next header.
QString tomlTable(const QString &text, const QString &table)
{
    const QStringList lines = stripComments(text).split(QLatin1Char('\n'));
    QStringList body;
    bool inside = false;
    const QString header = QLatin1Char('[') + table + QLatin1Char(']');
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('[')) && !line.startsWith(QStringLiteral("[["))
            && line.endsWith(QLatin1Char(']')) && !line.contains(QLatin1Char('='))) {
            inside = line == header;
            continue;
        }
        if (inside) body.append(raw);
    }
    return body.join(QLatin1Char('\n'));
}

// String items of a TOML array "key = [ "a", "b" ]" (may span lines).
// AGENT-NOTE: scanned by hand, not by regex: items such as
// "rich[jupyter]" contain brackets, and only an unquoted ']' ends the array.
QStringList tomlArray(const QString &body, const QString &key)
{
    const QRegularExpression re(QStringLiteral("(?:^|\\n)\\s*\"?%1\"?\\s*=\\s*\\[")
                                    .arg(QRegularExpression::escape(key)));
    const auto m = re.match(body);
    QStringList out;
    if (!m.hasMatch()) return out;
    QString current;
    QChar quote;
    for (int i = m.capturedEnd(); i < body.size(); ++i) {
        const QChar c = body.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                out.append(current);
                current.clear();
                quote = QChar();
            } else {
                current.append(c);
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char(']')) {
            break;
        }
    }
    return out;
}

// Keys of a table whose values are arrays ("yaml = [...]").
QStringList tomlArrayKeys(const QString &body)
{
    static const QRegularExpression re(QStringLiteral("^\\s*\"?([\\w.-]+)\"?\\s*=\\s*\\["),
                                       QRegularExpression::MultilineOption);
    QStringList out;
    auto it = re.globalMatch(body);
    while (it.hasNext()) out.append(it.next().captured(1));
    return out;
}

// "requests[socks]>=2.0; python_version<'3.8'" -> name "requests", ">=2.0".
DeclaredDependency pythonRequirement(const QString &spec, bool optional, bool build)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([A-Za-z0-9][A-Za-z0-9._-]*)\\s*(?:\\[[^\\]]*\\])?\\s*([^;]*)"));
    DeclaredDependency d;
    d.kind = DeclaredDependency::Python;
    const auto m = re.match(spec);
    d.name = m.captured(1);
    d.constraint = m.captured(2).trimmed();
    d.optional = optional;
    d.build = build;
    return d;
}

bool contains(const QVector<DeclaredDependency> &list, const QString &name, DeclaredDependency::Kind kind)
{
    for (const auto &d : list) if (d.name == name && d.kind == kind) return true;
    return false;
}

} // namespace

QString detectBuildSystem(const QStringList &files)
{
    auto has = [&](const char *name) { return files.contains(QString::fromLatin1(name)); };
    if (has("CMakeLists.txt")) return QStringLiteral("cmake");
    if (has("meson.build")) return QStringLiteral("meson");
    if (has("Cargo.toml")) return QStringLiteral("cargo");
    if (has("go.mod")) return QStringLiteral("go-module");
    if (has("pyproject.toml") || has("setup.py") || has("setup.cfg")) return QStringLiteral("distutils-r1");
    if (has("configure.ac") || has("configure.in") || has("autogen.sh") || has("configure")) {
        return QStringLiteral("autotools");
    }
    if (has("package.json")) return QStringLiteral("node");
    if (has("Makefile") || has("makefile") || has("GNUmakefile")) return QStringLiteral("make");
    return {};
}

QStringList interestingFiles(const QStringList &files)
{
    static const QStringList wanted = {
        QStringLiteral("CMakeLists.txt"), QStringLiteral("meson.build"), QStringLiteral("meson_options.txt"),
        QStringLiteral("meson.options"), QStringLiteral("Cargo.toml"), QStringLiteral("go.mod"),
        QStringLiteral("Cargo.lock"), QStringLiteral("pyproject.toml"), QStringLiteral("setup.py"), QStringLiteral("setup.cfg"),
        QStringLiteral("configure.ac"), QStringLiteral("configure.in"), QStringLiteral("package.json"),
        QStringLiteral("Makefile"), QStringLiteral("LICENSE"), QStringLiteral("LICENSE.md"),
        QStringLiteral("LICENSE.txt"), QStringLiteral("COPYING"), QStringLiteral("README.md")};
    QStringList out;
    for (const QString &f : wanted) if (files.contains(f)) out.append(f);
    return out;
}

QStringList cargoCrates(const QString &lockText, QStringList *gitSources)
{
    // AGENT-NOTE: cargo.eclass (EAPI 8) takes CRATES as "name@version"
    // entries; only crates.io packages (those with a checksum) belong there.
    // Git sources need GIT_CRATES and are reported for the assistant.
    QStringList out;
    const QStringList blocks = lockText.split(QStringLiteral("[[package]]"));
    static const QRegularExpression nameRe(QStringLiteral("\\bname\\s*=\\s*\"([^\"]+)\""));
    static const QRegularExpression versionRe(QStringLiteral("\\bversion\\s*=\\s*\"([^\"]+)\""));
    static const QRegularExpression sourceRe(QStringLiteral("\\bsource\\s*=\\s*\"([^\"]+)\""));
    for (int i = 1; i < blocks.size(); ++i) {
        const QString &b = blocks.at(i);
        const QString name = nameRe.match(b).captured(1);
        const QString version = versionRe.match(b).captured(1);
        const QString source = sourceRe.match(b).captured(1);
        if (name.isEmpty() || version.isEmpty() || source.isEmpty()) continue; // the package itself / path deps
        if (source.startsWith(QLatin1String("registry+"))) {
            out.append(name + QLatin1Char('@') + version);
        } else if (gitSources) {
            gitSources->append(name + QLatin1Char('@') + version + QStringLiteral(" (") + source + QLatin1Char(')'));
        }
    }
    out.removeDuplicates();
    return out;
}

QVector<DeclaredDependency> fromCMake(const QString &text)
{
    QVector<DeclaredDependency> out;
    const QString src = stripComments(text);
    static const QRegularExpression findRe(QStringLiteral("\\bfind_package\\s*\\(\\s*([A-Za-z][\\w.+-]*)([^)]*)\\)"),
                                           QRegularExpression::CaseInsensitiveOption);
    auto it = findRe.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        DeclaredDependency d;
        d.kind = DeclaredDependency::CMakePackage;
        d.name = m.captured(1);
        const QStringList args = m.captured(2).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        d.optional = !args.contains(QStringLiteral("REQUIRED"));
        bool inComponents = false;
        for (const QString &a : args) {
            if (a == QLatin1String("COMPONENTS")) { inComponents = true; continue; }
            if (a == QLatin1String("OPTIONAL_COMPONENTS") || a == QLatin1String("CONFIG")
                || a == QLatin1String("MODULE") || a == QLatin1String("QUIET") || a == QLatin1String("REQUIRED")
                || a == QLatin1String("NO_MODULE") || a == QLatin1String("EXACT")) {
                inComponents = a == QLatin1String("REQUIRED") ? inComponents : false;
                continue;
            }
            if (inComponents) d.components.append(a);
            else if (d.constraint.isEmpty() && !a.isEmpty() && a.at(0).isDigit()) d.constraint = QStringLiteral(">=") + a;
        }
        if (d.name.startsWith(QLatin1String("${"))) continue;
        if (!contains(out, d.name, d.kind)) out.append(d);
    }
    static const QRegularExpression pkgRe(QStringLiteral("\\bpkg_(?:check|search)_modules?\\s*\\(\\s*(\\S+)([^)]*)\\)"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QStringList keywords = {
        QStringLiteral("REQUIRED"), QStringLiteral("QUIET"), QStringLiteral("IMPORTED_TARGET"),
        QStringLiteral("GLOBAL"), QStringLiteral("NO_CMAKE_PATH"), QStringLiteral("NO_CMAKE_ENVIRONMENT_PATH")};
    auto pit = pkgRe.globalMatch(src);
    while (pit.hasNext()) {
        const auto m = pit.next();
        const QStringList args = m.captured(2).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const bool required = args.contains(QStringLiteral("REQUIRED"));
        for (const QString &a : args) {
            if (keywords.contains(a) || a.startsWith(QLatin1String("${"))) continue;
            static const QRegularExpression spec(QStringLiteral("^([A-Za-z0-9_.+-]+?)((?:>=|<=|=|>|<).*)?$"));
            const auto s = spec.match(a);
            if (!s.hasMatch()) continue;
            DeclaredDependency d;
            d.kind = DeclaredDependency::PkgConfig;
            d.name = s.captured(1);
            d.constraint = s.captured(2);
            d.optional = !required;
            if (!contains(out, d.name, d.kind)) out.append(d);
        }
    }
    return out;
}

QVector<DeclaredDependency> fromMeson(const QString &text)
{
    QVector<DeclaredDependency> out;
    static const QRegularExpression re(QStringLiteral("\\bdependency\\s*\\(\\s*'([^']+)'([^)]*)\\)"));
    auto it = re.globalMatch(stripComments(text));
    while (it.hasNext()) {
        const auto m = it.next();
        DeclaredDependency d;
        d.kind = DeclaredDependency::Meson;
        d.name = m.captured(1);
        const QString args = m.captured(2);
        static const QRegularExpression ver(QStringLiteral("version\\s*:\\s*'([^']+)'"));
        if (const auto v = ver.match(args); v.hasMatch()) d.constraint = v.captured(1).remove(QLatin1Char(' '));
        static const QRegularExpression req(QStringLiteral("required\\s*:\\s*(false|get_option)"));
        d.optional = req.match(args).hasMatch();
        static const QRegularExpression native(QStringLiteral("native\\s*:\\s*true"));
        d.build = native.match(args).hasMatch();
        if (d.name == QLatin1String("threads") || d.name.isEmpty()) continue;
        if (!contains(out, d.name, d.kind)) out.append(d);
    }
    return out;
}

QVector<DeclaredDependency> fromPyproject(const QString &text)
{
    QVector<DeclaredDependency> out;
    for (const QString &s : tomlArray(tomlTable(text, QStringLiteral("project")), QStringLiteral("dependencies"))) {
        out.append(pythonRequirement(s, false, false));
    }
    const QString optional = tomlTable(text, QStringLiteral("project.optional-dependencies"));
    for (const QString &extra : tomlArrayKeys(optional)) {
        for (const QString &s : tomlArray(optional, extra)) out.append(pythonRequirement(s, true, false));
    }
    for (const QString &s : tomlArray(tomlTable(text, QStringLiteral("build-system")), QStringLiteral("requires"))) {
        out.append(pythonRequirement(s, false, true));
    }
    // Poetry: name = "^1.0" lines under [tool.poetry.dependencies].
    static const QRegularExpression poetry(QStringLiteral("^\\s*([A-Za-z0-9][A-Za-z0-9._-]*)\\s*=\\s*(\"[^\"]*\"|\\{[^}]*\\})"),
                                           QRegularExpression::MultilineOption);
    auto pit = poetry.globalMatch(tomlTable(text, QStringLiteral("tool.poetry.dependencies")));
    while (pit.hasNext()) {
        const auto m = pit.next();
        if (m.captured(1) == QLatin1String("python")) continue;
        DeclaredDependency d = pythonRequirement(m.captured(1), m.captured(2).contains(QStringLiteral("optional = true")), false);
        if (m.captured(2).startsWith(QLatin1Char('"'))) d.constraint = m.captured(2).mid(1).chopped(1);
        out.append(d);
    }
    return out;
}

QVector<DeclaredDependency> fromSetupPy(const QString &text)
{
    QVector<DeclaredDependency> out;
    static const QRegularExpression re(QStringLiteral("install_requires\\s*=\\s*\\[([^\\]]*)\\]"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return out;
    static const QRegularExpression item(QStringLiteral("[\"']([^\"']+)[\"']"));
    auto it = item.globalMatch(m.captured(1));
    while (it.hasNext()) out.append(pythonRequirement(it.next().captured(1), false, false));
    return out;
}

QVector<DeclaredDependency> fromCargo(const QString &text)
{
    QVector<DeclaredDependency> out;
    static const QRegularExpression line(QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*(.*)$"),
                                         QRegularExpression::MultilineOption);
    for (const auto &[table, build] : {std::pair{QStringLiteral("dependencies"), false},
                                       std::pair{QStringLiteral("build-dependencies"), true}}) {
        auto it = line.globalMatch(tomlTable(text, table));
        while (it.hasNext()) {
            const auto m = it.next();
            DeclaredDependency d;
            d.kind = DeclaredDependency::Cargo;
            d.name = m.captured(1);
            d.build = build;
            const QString value = m.captured(2).trimmed();
            d.optional = value.contains(QStringLiteral("optional = true"));
            static const QRegularExpression ver(QStringLiteral("(?:^\"|version\\s*=\\s*\")([^\"]+)\""));
            if (const auto v = ver.match(value); v.hasMatch()) d.constraint = v.captured(1);
            out.append(d);
        }
    }
    // [dependencies.name] sub-tables.
    static const QRegularExpression sub(QStringLiteral("^\\s*\\[(build-)?dependencies\\.([A-Za-z0-9_-]+)\\]"),
                                        QRegularExpression::MultilineOption);
    auto sit = sub.globalMatch(text);
    while (sit.hasNext()) {
        const auto m = sit.next();
        DeclaredDependency d;
        d.kind = DeclaredDependency::Cargo;
        d.name = m.captured(2);
        d.build = !m.captured(1).isEmpty();
        out.append(d);
    }
    return out;
}

QVector<DeclaredDependency> fromGoMod(const QString &text)
{
    QVector<DeclaredDependency> out;
    static const QRegularExpression block(QStringLiteral("require\\s*\\(([^)]*)\\)"));
    // Same-line form only: "require (" opens the block handled above.
    static const QRegularExpression single(QStringLiteral("^require[ \\t]+([^\\s(]+)[ \\t]+(\\S+)"),
                                           QRegularExpression::MultilineOption);
    auto add = [&](const QString &line) {
        const QStringList parts = line.simplified().split(QLatin1Char(' '));
        if (parts.size() < 2 || parts.first().startsWith(QLatin1String("//"))) return;
        DeclaredDependency d;
        d.kind = DeclaredDependency::Go;
        d.name = parts.at(0);
        d.constraint = parts.at(1);
        d.optional = line.contains(QStringLiteral("// indirect"));
        out.append(d);
    };
    auto it = block.globalMatch(text);
    while (it.hasNext()) {
        for (const QString &l : it.next().captured(1).split(QLatin1Char('\n'))) add(l);
    }
    auto sit = single.globalMatch(text);
    while (sit.hasNext()) {
        const auto m = sit.next();
        add(m.captured(1) + QLatin1Char(' ') + m.captured(2));
    }
    return out;
}

QVector<DeclaredDependency> fromAutoconf(const QString &text)
{
    QVector<DeclaredDependency> out;
    const QString src = stripComments(stripComments(text), QStringLiteral("dnl"));
    static const QRegularExpression pkg(QStringLiteral("PKG_CHECK_MODULES\\s*\\(\\s*\\[?\\w+\\]?\\s*,\\s*\\[?([^\\],)]+)\\]?\\s*(,\\s*\\[?([^\\]]*)\\]?)?"));
    auto it = pkg.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        // "gtk+-3.0 >= 3.22 glib-2.0" -> modules with optional constraints.
        const QStringList tokens = m.captured(1).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const bool optional = !m.captured(3).trimmed().isEmpty() && m.captured(3).contains(QStringLiteral("no"));
        for (int i = 0; i < tokens.size(); ++i) {
            const QString t = tokens.at(i);
            if (t.startsWith(QLatin1Char('>')) || t.startsWith(QLatin1Char('<')) || t.startsWith(QLatin1Char('='))) {
                if (!out.isEmpty() && i + 1 < tokens.size()) out.last().constraint = t + tokens.at(++i);
                continue;
            }
            DeclaredDependency d;
            d.kind = DeclaredDependency::PkgConfig;
            d.name = t;
            d.optional = optional;
            out.append(d);
        }
    }
    static const QRegularExpression lib(QStringLiteral("AC_(?:CHECK|SEARCH)_LIBS?\\s*\\(\\s*\\[?([\\w.+-]+)\\]?"));
    auto lit = lib.globalMatch(src);
    while (lit.hasNext()) {
        DeclaredDependency d;
        d.kind = DeclaredDependency::Autoconf;
        d.name = lit.next().captured(1);
        if (!contains(out, d.name, d.kind)) out.append(d);
    }
    return out;
}

QVector<DeclaredDependency> fromPackageJson(const QString &text)
{
    QVector<DeclaredDependency> out;
    static const QRegularExpression block(QStringLiteral("\"(dependencies|devDependencies)\"\\s*:\\s*\\{([^}]*)\\}"));
    auto it = block.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        static const QRegularExpression entry(QStringLiteral("\"([^\"]+)\"\\s*:\\s*\"([^\"]*)\""));
        auto e = entry.globalMatch(m.captured(2));
        while (e.hasNext()) {
            const auto x = e.next();
            DeclaredDependency d;
            d.kind = DeclaredDependency::Node;
            d.name = x.captured(1);
            d.constraint = x.captured(2);
            d.build = m.captured(1) == QLatin1String("devDependencies");
            out.append(d);
        }
    }
    return out;
}

QString tomlValue(const QString &text, const QString &table, const QString &key)
{
    const QRegularExpression re(QStringLiteral("^\\s*%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')")
                                    .arg(QRegularExpression::escape(key)),
                                QRegularExpression::MultilineOption);
    const auto m = re.match(tomlTable(text, table));
    if (!m.hasMatch()) return {};
    return m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
}

QString pep517Backend(const QString &pyprojectText)
{
    const QString backend = tomlValue(pyprojectText, QStringLiteral("build-system"), QStringLiteral("build-backend"));
    if (backend.isEmpty()) return QStringLiteral("setuptools");
    static const QList<QPair<const char *, const char *>> map = {
        {"hatchling", "hatchling"}, {"flit_core", "flit"}, {"flit.", "flit"}, {"poetry", "poetry"},
        {"pdm", "pdm-backend"}, {"maturin", "maturin"}, {"scikit_build_core", "scikit-build-core"},
        {"mesonpy", "meson-python"}, {"setuptools", "setuptools"}, {"uv_build", "uv-build"}};
    for (const auto &[needle, name] : map) {
        if (backend.contains(QString::fromLatin1(needle))) return QString::fromLatin1(name);
    }
    return QStringLiteral("standalone");
}

bool hasPythonScripts(const QString &pyprojectText)
{
    return !tomlTable(pyprojectText, QStringLiteral("project.scripts")).trimmed().isEmpty()
        || !tomlTable(pyprojectText, QStringLiteral("project.gui-scripts")).trimmed().isEmpty()
        || !tomlTable(pyprojectText, QStringLiteral("tool.poetry.scripts")).trimmed().isEmpty();
}

QVector<DeclaredDependency> fromFile(const QString &fileName, const QString &text)
{
    const QString f = fileName.section(QLatin1Char('/'), -1);
    if (f == QLatin1String("CMakeLists.txt") || f.endsWith(QLatin1String(".cmake"))) return fromCMake(text);
    if (f == QLatin1String("meson.build")) return fromMeson(text);
    if (f == QLatin1String("pyproject.toml")) return fromPyproject(text);
    if (f == QLatin1String("setup.py")) return fromSetupPy(text);
    if (f == QLatin1String("Cargo.toml")) return fromCargo(text);
    if (f == QLatin1String("go.mod")) return fromGoMod(text);
    if (f == QLatin1String("configure.ac") || f == QLatin1String("configure.in")) return fromAutoconf(text);
    if (f == QLatin1String("package.json")) return fromPackageJson(text);
    return {};
}

} // namespace BuildFiles

} // namespace QindaPortage::Ops
