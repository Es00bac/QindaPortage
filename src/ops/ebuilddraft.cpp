// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/ebuilddraft.h"

#include <QRegularExpression>

namespace QindaPortage::Ops::EbuildDraft {

namespace {

bool mentions(const SourceInfo &info, const QStringList &words)
{
    const QString hay = (info.description + QLatin1Char(' ') + info.topics.join(QLatin1Char(' ')) + QLatin1Char(' ')
                         + info.repo).toLower();
    for (const QString &w : words) {
        if (QRegularExpression(QStringLiteral("\\b%1").arg(QRegularExpression::escape(w))).match(hay).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool linksToolkit(const SourceInfo &info)
{
    static const QStringList gui = {QStringLiteral("qt6"), QStringLiteral("qt5"), QStringLiteral("gtk"),
                                    QStringLiteral("gtk+-3.0"), QStringLiteral("gtk4"), QStringLiteral("sdl2"),
                                    QStringLiteral("sdl3"), QStringLiteral("glfw3"), QStringLiteral("glfw"),
                                    QStringLiteral("wayland-client"), QStringLiteral("x11"),
                                    QStringLiteral("libadwaita-1"), QStringLiteral("pyqt6"),
                                    QStringLiteral("pyside6"), QStringLiteral("egui"), QStringLiteral("iced"),
                                    QStringLiteral("gtk4-rs"), QStringLiteral("winit")};
    for (const DeclaredDependency &d : info.dependencies) {
        if (gui.contains(d.name.toLower())) return true;
    }
    return false;
}

// Dependencies sorted into the ebuild's variables.
struct DepVars
{
    QStringList depend, rdependOnly, bdepend, test, optional;
};

DepVars sortDependencies(const SourceInfo &info, const QMap<QString, QString> &atoms)
{
    DepVars v;
    bool pkgconfig = false;
    auto add = [](QStringList &list, const QString &atomText) {
        for (const QString &a : atomText.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (!list.contains(a)) list.append(a);
        }
    };
    static const QStringList testNames = {QStringLiteral("gtest"), QStringLiteral("googletest"),
                                          QStringLiteral("catch2"), QStringLiteral("pytest")};
    for (const DeclaredDependency &d : info.dependencies) {
        if (d.kind == DeclaredDependency::PkgConfig || d.kind == DeclaredDependency::Meson) pkgconfig = true;
        const QString atom = atoms.value(d.name);
        if (atom.isEmpty()) continue;
        if (testNames.contains(d.name.toLower())) add(v.test, atom);
        else if (d.optional) v.optional.append(QStringLiteral("%1  (%2)").arg(atom, d.name));
        else if (d.build || atom.startsWith(QLatin1String("virtual/pkgconfig")) || atom.startsWith(QLatin1String("dev-build/")))
            add(v.bdepend, atom);
        else if (d.kind == DeclaredDependency::Python) add(v.rdependOnly, atom);
        else add(v.depend, atom);
    }
    if (pkgconfig && !v.bdepend.contains(QStringLiteral("virtual/pkgconfig"))) v.bdepend.prepend(QStringLiteral("virtual/pkgconfig"));
    return v;
}

QString block(const QString &var, const QStringList &atoms, const QString &prefix = QString())
{
    if (atoms.isEmpty() && prefix.isEmpty()) return {};
    QStringList lines;
    if (!prefix.isEmpty()) lines.append(QLatin1Char('\t') + prefix);
    for (const QString &a : atoms) lines.append(QLatin1Char('\t') + a);
    return QStringLiteral("%1=\"\n%2\n\"\n").arg(var, lines.join(QLatin1Char('\n')));
}

// SRC_URI / S / inherit additions for where the source comes from.
struct Fetch
{
    QString preamble; // variables before inherit
    QString srcUri;
    QString s;
    bool gitR3 = false;
};

Fetch fetchFor(const SourceInfo &info)
{
    Fetch f;
    const QString pv = info.version;
    if (info.host == QLatin1String("github") && !info.snapshot && !info.tag.isEmpty()) {
        QString tagExpr = info.tag;
        // "1.2", "v1.2", "jq-1.2" -> "${PV}", "v${PV}", "jq-${PV}".
        if (!pv.isEmpty() && info.tag.endsWith(pv)) tagExpr = info.tag.chopped(pv.size()) + QStringLiteral("${PV}");
        // GitHub drops a leading "v" before a digit from the directory name.
        const bool vPrefix = tagExpr.startsWith(QStringLiteral("v$"))
            || (tagExpr.size() > 1 && tagExpr.at(0) == QLatin1Char('v') && tagExpr.at(1).isDigit());
        const QString dirTag = vPrefix ? tagExpr.mid(1) : tagExpr;
        f.srcUri = QStringLiteral("https://github.com/%1/%2/archive/refs/tags/%3.tar.gz -> ${P}.gh.tar.gz")
                       .arg(info.owner, info.repo, tagExpr);
        f.s = QStringLiteral("${WORKDIR}/%1-%2").arg(info.repo, dirTag);
    } else if (info.host == QLatin1String("github") && !info.commit.isEmpty()) {
        // AGENT-NOTE: a commit archive is Manifest-verified and works under
        // network-sandbox and binpkgs, unlike git-r3; used for GitHub snapshots.
        f.preamble = QStringLiteral("COMMIT=\"%1\"\n").arg(info.commit);
        f.srcUri = QStringLiteral("https://github.com/%1/%2/archive/${COMMIT}.tar.gz -> ${P}.gh.tar.gz")
                       .arg(info.owner, info.repo);
        f.s = QStringLiteral("${WORKDIR}/%1-${COMMIT}").arg(info.repo);
    } else if (!info.tarballUrl.isEmpty()) {
        f.srcUri = info.tarballUrl + QStringLiteral(" -> ${P}.tar.gz");
        if (!info.archiveDir.isEmpty()) f.s = QStringLiteral("${WORKDIR}/") + info.archiveDir;
    } else if (!info.gitUrl.isEmpty()) {
        f.gitR3 = true;
        f.preamble = QStringLiteral("EGIT_REPO_URI=\"%1\"\n").arg(info.gitUrl);
        if (!info.commit.isEmpty()) f.preamble += QStringLiteral("EGIT_COMMIT=\"%1\"\n").arg(info.commit);
        else if (!info.tag.isEmpty()) f.preamble += QStringLiteral("EGIT_COMMIT=\"%1\"\n").arg(info.tag);
    }
    return f;
}

} // namespace

QString packageName(const SourceInfo &info)
{
    QString base = info.name.isEmpty() ? info.repo : info.name;
    if (base.isEmpty()) base = QStringLiteral("unnamed");
    base = base.toLower();
    // Python packages use the PyPI-normalised name ("Fancy_Tool" -> "fancy-tool").
    if (info.buildSystem == QLatin1String("distutils-r1")) base.replace(QLatin1Char('_'), QLatin1Char('-'));
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9+_-]")), QStringLiteral("-"));
    // A PN may not end in something that looks like a version.
    base.replace(QRegularExpression(QStringLiteral("-(\\d[\\w.]*)$")), QStringLiteral("\\1"));
    while (base.startsWith(QLatin1Char('-'))) base.remove(0, 1);
    return base;
}

QString suggestedCategory(const SourceInfo &info)
{
    if (info.buildSystem == QLatin1String("distutils-r1") && !info.pythonScripts && !linksToolkit(info)) {
        return QStringLiteral("dev-python");
    }
    if (mentions(info, {QStringLiteral("font"), QStringLiteral("typeface")})) return QStringLiteral("media-fonts");
    if (mentions(info, {QStringLiteral("game"), QStringLiteral("emulator")})) return QStringLiteral("games-misc");
    if (info.libraryOnly || (info.repo.toLower().startsWith(QLatin1String("lib"))
                             && !mentions(info, {QStringLiteral("tool"), QStringLiteral("command-line"), QStringLiteral("cli")}))) {
        return QStringLiteral("dev-libs");
    }
    if (mentions(info, {QStringLiteral("audio"), QStringLiteral("music"), QStringLiteral("sound")})) return QStringLiteral("media-sound");
    if (mentions(info, {QStringLiteral("video"), QStringLiteral("movie"), QStringLiteral("stream")})) return QStringLiteral("media-video");
    if (mentions(info, {QStringLiteral("image"), QStringLiteral("photo"), QStringLiteral("graphics")})) return QStringLiteral("media-gfx");
    if (linksToolkit(info)) return QStringLiteral("gui-apps");
    return QStringLiteral("app-misc");
}

QString fileName(const SourceInfo &info)
{
    return QStringLiteral("%1-%2.ebuild").arg(packageName(info), info.version.isEmpty() ? QStringLiteral("0") : info.version);
}

QString generate(const SourceInfo &info, const QMap<QString, QString> &atoms, const QDateTime &now)
{
    const QString bs = info.buildSystem;
    const Fetch fetch = fetchFor(info);
    const DepVars deps = sortDependencies(info, atoms);
    QStringList inherit;
    QString pre;
    QString phases;

    if (bs == QLatin1String("distutils-r1")) {
        pre += QStringLiteral("DISTUTILS_USE_PEP517=%1\nPYTHON_COMPAT=( python3_{12..14} )\n")
                   .arg(info.pep517Backend.isEmpty() ? QStringLiteral("setuptools") : info.pep517Backend);
    }
    if (fetch.gitR3) inherit.append(QStringLiteral("git-r3"));
    if (bs == QLatin1String("cmake") || bs == QLatin1String("meson") || bs == QLatin1String("autotools")
        || bs == QLatin1String("cargo") || bs == QLatin1String("go-module") || bs == QLatin1String("distutils-r1")) {
        inherit.append(bs);
    }
    if (bs == QLatin1String("cargo")) {
        pre += QStringLiteral("# AGENT: fill CRATES from Cargo.lock (pycargoebuild) before Manifest.\nCRATES=\"\"\n");
    }
    if (bs == QLatin1String("autotools") && !info.files.contains(QStringLiteral("configure"))) {
        phases += QStringLiteral("src_prepare() {\n\tdefault\n\teautoreconf\n}\n\n");
    }
    if (bs == QLatin1String("go-module")) {
        phases += QStringLiteral("src_compile() {\n\tego build -o \"${PN}\" .\n}\n\nsrc_install() {\n"
                                 "\tdobin \"${PN}\"\n\teinstalldocs\n}\n\n");
    }
    if (bs == QLatin1String("make")) {
        phases += QStringLiteral("src_install() {\n\temake DESTDIR=\"${D}\" PREFIX=\"${EPREFIX}/usr\" install\n"
                                 "\teinstalldocs\n}\n\n");
    }
    if (!deps.test.isEmpty() && bs == QLatin1String("cmake")) {
        phases += QStringLiteral("src_configure() {\n\tlocal mycmakeargs=(\n\t\t-DBUILD_TESTING=$(usex test)\n"
                                 "\t)\n\tcmake_src_configure\n}\n\n");
    }

    QString e;
    e += QStringLiteral("# Copyright %1 QindaPortage users\n"
                        "# Distributed under the terms of the GNU General Public License v2\n\n")
             .arg(now.date().year());
    e += QStringLiteral("# Generated by QindaPortage from %1 on %2 (%3 build system).\n"
                        "# A first draft: review, then let the pretend/build loop repair it.\n\n")
             .arg(info.url, now.toString(Qt::ISODate), bs.isEmpty() ? QStringLiteral("unknown") : bs);
    e += QStringLiteral("EAPI=8\n\n");
    if (!pre.isEmpty()) e += pre + QLatin1Char('\n');
    if (!fetch.preamble.isEmpty()) e += fetch.preamble + QLatin1Char('\n');
    if (!inherit.isEmpty()) e += QStringLiteral("inherit %1\n\n").arg(inherit.join(QLatin1Char(' ')));
    QString description = info.description.isEmpty() ? info.repo : info.description;
    description.replace(QLatin1Char('"'), QLatin1Char('\''));
    if (description.size() > 80) description = description.left(77) + QStringLiteral("...");
    e += QStringLiteral("DESCRIPTION=\"%1\"\n").arg(description);
    e += QStringLiteral("HOMEPAGE=\"%1\"\n").arg(info.homepage.isEmpty() ? info.url : info.homepage);
    if (!fetch.srcUri.isEmpty()) {
        e += QStringLiteral("SRC_URI=\"%1").arg(fetch.srcUri);
        if (bs == QLatin1String("cargo")) e += QStringLiteral("\n\t${CARGO_CRATE_URIS}");
        e += QStringLiteral("\"\n");
    }
    if (!fetch.s.isEmpty()) e += QStringLiteral("S=\"%1\"\n").arg(fetch.s);
    e += QLatin1Char('\n');
    e += QStringLiteral("LICENSE=\"%1\"\n").arg(info.license.isEmpty() ? QStringLiteral("all-rights-reserved") : info.license);
    if (info.license.isEmpty()) e += QStringLiteral("# AGENT: upstream license not detected; confirm it.\n");
    if (bs == QLatin1String("cargo")) e += QStringLiteral("# AGENT: add the crates' licenses (pycargoebuild prints them).\n");
    e += QStringLiteral("SLOT=\"0\"\nKEYWORDS=\"~amd64\"\n");
    if (!deps.test.isEmpty()) e += QStringLiteral("IUSE=\"test\"\nRESTRICT=\"!test? ( test )\"\n");
    e += QLatin1Char('\n');

    QStringList depend = deps.depend;
    if (!deps.test.isEmpty()) depend.append(QStringLiteral("test? ( %1 )").arg(deps.test.join(QLatin1Char(' '))));
    if (!depend.isEmpty()) {
        e += block(QStringLiteral("DEPEND"), depend);
        e += block(QStringLiteral("RDEPEND"), deps.rdependOnly, QStringLiteral("${DEPEND}"));
    } else if (!deps.rdependOnly.isEmpty()) {
        e += block(QStringLiteral("RDEPEND"), deps.rdependOnly);
    }
    e += block(QStringLiteral("BDEPEND"), deps.bdepend);
    if (!deps.optional.isEmpty()) {
        e += QStringLiteral("# Optional upstream dependencies (candidates for USE flags):\n");
        for (const QString &o : deps.optional) e += QStringLiteral("#   ") + o + QLatin1Char('\n');
    }
    if (bs == QLatin1String("cargo")) e += QStringLiteral("\nQA_FLAGS_IGNORED=\"usr/bin/${PN}\"\n");
    if (bs == QLatin1String("go-module")) {
        e += QStringLiteral("\n# AGENT: Go modules must be vendored into a deps tarball added to SRC_URI.\n");
    }
    if (bs == QLatin1String("node")) {
        e += QStringLiteral("\n# AGENT: npm dependencies cannot be fetched under network-sandbox;\n"
                            "# this needs a vendored node_modules tarball or is not packageable as is.\n");
    }
    if (bs.isEmpty()) e += QStringLiteral("\n# AGENT: build system not detected; write src_compile/src_install.\n");
    if (!phases.isEmpty()) e += (e.endsWith(QStringLiteral("\n\n")) ? QString() : QStringLiteral("\n")) + phases;
    // One blank line between sections, one newline at the end.
    static const QRegularExpression blankRuns(QStringLiteral("\\n{3,}"));
    e.replace(blankRuns, QStringLiteral("\n\n"));
    while (e.endsWith(QStringLiteral("\n\n"))) e.chop(1);
    return e;
}

} // namespace QindaPortage::Ops::EbuildDraft
