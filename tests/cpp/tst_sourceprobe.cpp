// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/buildfiles.h"
#include "ops/dependencyhints.h"
#include "ops/ebuilddraft.h"
#include "ops/sourceprobe.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

using namespace QindaPortage::Ops;

// Offline: build-file fixtures in tests/data/sources, no network.
namespace {

QString fixture(const char *path)
{
    QFile f(QStringLiteral(TEST_DATA "/sources/") + QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly)) qFatal("missing fixture %s", path);
    return QString::fromUtf8(f.readAll());
}

const DeclaredDependency *dep(const QVector<DeclaredDependency> &list, const char *name)
{
    for (const auto &d : list) if (d.name == QLatin1String(name)) return &d;
    return nullptr;
}

QStringList names(const QVector<DeclaredDependency> &list)
{
    QStringList out;
    for (const auto &d : list) out.append(d.name);
    return out;
}

} // namespace

class TstSourceProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cmake()
    {
        const auto deps = BuildFiles::fromCMake(fixture("cmake/CMakeLists.txt"));
        QCOMPARE(names(deps), (QStringList{QStringLiteral("Qt6"), QStringLiteral("ZLIB"), QStringLiteral("PkgConfig"),
                                           QStringLiteral("GTest"), QStringLiteral("libpng"), QStringLiteral("fontconfig"),
                                           QStringLiteral("libpipewire-0.3")}));
        const auto *qt = dep(deps, "Qt6");
        QCOMPARE(qt->components, (QStringList{QStringLiteral("Core"), QStringLiteral("Gui"), QStringLiteral("Widgets"),
                                              QStringLiteral("Svg")}));
        QCOMPARE(qt->constraint, QStringLiteral(">=6.5"));
        QVERIFY(!qt->optional);
        QVERIFY(dep(deps, "GTest")->optional);
        QCOMPARE(dep(deps, "libpng")->constraint, QStringLiteral(">=1.6"));
        QCOMPARE(dep(deps, "libpng")->kind, DeclaredDependency::PkgConfig);
        QVERIFY(dep(deps, "libpipewire-0.3")->optional);
    }
    void meson()
    {
        const auto deps = BuildFiles::fromMeson(fixture("meson/meson.build"));
        QCOMPARE(names(deps), (QStringList{QStringLiteral("pixman-1"), QStringLiteral("wayland-client"),
                                           QStringLiteral("libutf8proc"), QStringLiteral("wayland-scanner")}));
        QCOMPARE(dep(deps, "wayland-client")->constraint, QStringLiteral(">=1.20"));
        QVERIFY(dep(deps, "libutf8proc")->optional);
        QVERIFY(dep(deps, "wayland-scanner")->build);
    }
    void pyproject()
    {
        const QString text = fixture("python/pyproject.toml");
        const auto deps = BuildFiles::fromPyproject(text);
        QCOMPARE(names(deps), (QStringList{QStringLiteral("requests"), QStringLiteral("rich"), QStringLiteral("click"),
                                           QStringLiteral("PyYAML"), QStringLiteral("hatchling")}));
        QCOMPARE(dep(deps, "requests")->constraint, QStringLiteral(">=2.31"));
        QVERIFY(dep(deps, "PyYAML")->optional);
        QVERIFY(dep(deps, "hatchling")->build);
        QCOMPARE(BuildFiles::pep517Backend(text), QStringLiteral("hatchling"));
        QCOMPARE(BuildFiles::pep517Backend(QString()), QStringLiteral("setuptools"));
        QVERIFY(BuildFiles::hasPythonScripts(text));
        QCOMPARE(BuildFiles::tomlValue(text, QStringLiteral("project"), QStringLiteral("name")), QStringLiteral("Fancy_Tool"));
        QCOMPARE(names(BuildFiles::fromSetupPy(QStringLiteral("setup(install_requires=['numpy>=1.2', \"six\"])"))),
                 (QStringList{QStringLiteral("numpy"), QStringLiteral("six")}));
    }
    void cargoAndGo()
    {
        const auto cargo = BuildFiles::fromCargo(fixture("cargo/Cargo.toml"));
        QCOMPARE(names(cargo), (QStringList{QStringLiteral("anyhow"), QStringLiteral("clap"), QStringLiteral("openssl-sys"),
                                            QStringLiteral("cc"), QStringLiteral("regex")}));
        QCOMPARE(dep(cargo, "clap")->constraint, QStringLiteral("4"));
        QVERIFY(dep(cargo, "openssl-sys")->optional);
        QVERIFY(dep(cargo, "cc")->build);
        const auto go = BuildFiles::fromGoMod(fixture("go/go.mod"));
        QCOMPARE(names(go), (QStringList{QStringLiteral("github.com/spf13/cobra"), QStringLiteral("golang.org/x/sys"),
                                         QStringLiteral("github.com/stretchr/testify")}));
        QVERIFY(dep(go, "golang.org/x/sys")->optional);
    }
    void autoconf()
    {
        const auto deps = BuildFiles::fromAutoconf(fixture("autotools/configure.ac"));
        QCOMPARE(names(deps), (QStringList{QStringLiteral("gtk+-3.0"), QStringLiteral("glib-2.0"), QStringLiteral("onig")}));
        QCOMPARE(dep(deps, "gtk+-3.0")->constraint, QStringLiteral(">=3.22"));
        QCOMPARE(dep(deps, "onig")->kind, DeclaredDependency::Autoconf);
    }
    void buildSystemDetection()
    {
        QCOMPARE(BuildFiles::detectBuildSystem({QStringLiteral("CMakeLists.txt"), QStringLiteral("Makefile")}), QStringLiteral("cmake"));
        QCOMPARE(BuildFiles::detectBuildSystem({QStringLiteral("configure.ac")}), QStringLiteral("autotools"));
        QCOMPARE(BuildFiles::detectBuildSystem({QStringLiteral("setup.py")}), QStringLiteral("distutils-r1"));
        QCOMPARE(BuildFiles::detectBuildSystem({QStringLiteral("README")}), QString());
    }
    void versionsAndLicenses()
    {
        QCOMPARE(BuildFiles::versionFromTag(QStringLiteral("v1.2.3")), QStringLiteral("1.2.3"));
        QCOMPARE(BuildFiles::versionFromTag(QStringLiteral("jq-1.8.2")), QStringLiteral("1.8.2"));
        QCOMPARE(BuildFiles::versionFromTag(QStringLiteral("release-2.0rc1")), QStringLiteral("2.0_rc1"));
        QCOMPARE(BuildFiles::versionFromTag(QStringLiteral("1.0b2")), QStringLiteral("1.0_beta2"));
        QCOMPARE(BuildFiles::versionFromTag(QStringLiteral("nightly")), QString());
        QCOMPARE(BuildFiles::versionFromTag(QStringLiteral("2024-01-01")), QString());
        QCOMPARE(BuildFiles::gentooLicense(QStringLiteral("GPL-3.0-or-later")), QStringLiteral("GPL-3+"));
        QCOMPARE(BuildFiles::gentooLicense(QStringLiteral("BSD-3-Clause")), QStringLiteral("BSD"));
        QCOMPARE(BuildFiles::gentooLicense(QStringLiteral("WTFPL-ish")), QString());
        QCOMPARE(BuildFiles::licenseFromText(fixture("autotools/COPYING")), QStringLiteral("GPL-3+"));
        QCOMPARE(BuildFiles::licenseFromText(fixture("cmake/LICENSE")), QStringLiteral("MIT"));
    }
    void classify()
    {
        SourceInfo gh;
        QVERIFY(SourceProbe::classifyUrl(QStringLiteral("https://github.com/sharkdp/fd.git"), &gh));
        QCOMPARE(gh.host, QStringLiteral("github"));
        QCOMPARE(gh.owner, QStringLiteral("sharkdp"));
        QCOMPARE(gh.repo, QStringLiteral("fd"));
        QCOMPARE(gh.gitUrl, QStringLiteral("https://github.com/sharkdp/fd.git"));
        SourceInfo tree;
        QVERIFY(SourceProbe::classifyUrl(QStringLiteral("https://gitlab.com/group/proj/-/tree/main"), &tree));
        QCOMPARE(tree.host, QStringLiteral("gitlab"));
        QCOMPARE(tree.repo, QStringLiteral("proj"));
        SourceInfo tar;
        QVERIFY(SourceProbe::classifyUrl(QStringLiteral("https://example.org/dl/tool-1.4.tar.xz"), &tar));
        QCOMPARE(tar.host, QStringLiteral("tarball"));
        SourceInfo local;
        QVERIFY(SourceProbe::classifyUrl(QStringLiteral(TEST_DATA "/sources/cmake"), &local));
        QCOMPARE(local.host, QStringLiteral("local"));
        SourceInfo bad;
        QVERIFY(!SourceProbe::classifyUrl(QStringLiteral("/nonexistent/dir"), &bad));
    }
    void analyzeLocalTrees()
    {
        SourceInfo cm;
        SourceProbe::analyzeTree(QStringLiteral(TEST_DATA "/sources/cmake"), &cm);
        QCOMPARE(cm.buildSystem, QStringLiteral("cmake"));
        QCOMPARE(cm.license, QStringLiteral("MIT"));
        QVERIFY(cm.files.contains(QStringLiteral("main.cpp")));
        QCOMPARE(cm.dependencies.size(), 7);
        QVERIFY(!cm.libraryOnly);

        SourceInfo py;
        SourceProbe::analyzeTree(QStringLiteral(TEST_DATA "/sources/python"), &py);
        QCOMPARE(py.buildSystem, QStringLiteral("distutils-r1"));
        QCOMPARE(py.pep517Backend, QStringLiteral("hatchling"));
        QCOMPARE(py.name, QStringLiteral("Fancy_Tool"));
        QCOMPARE(py.version, QStringLiteral("0.4.2"));
        QCOMPARE(py.description, QStringLiteral("A fancy command line tool"));
        QVERIFY(py.pythonScripts);
        QVERIFY(py.toJson().contains(QStringLiteral("dependencies")));
    }
    void resolveDependencies()
    {
        const auto res = DependencyHints::resolve(BuildFiles::fromCMake(fixture("cmake/CMakeLists.txt")), nullptr);
        const auto atoms = DependencyHints::atomMap(res);
        QCOMPARE(atoms.value(QStringLiteral("Qt6")), QStringLiteral("dev-qt/qtbase:6[gui,widgets] dev-qt/qtsvg:6"));
        QCOMPARE(atoms.value(QStringLiteral("ZLIB")), QStringLiteral("virtual/zlib:="));
        QCOMPARE(atoms.value(QStringLiteral("GTest")), QStringLiteral("dev-cpp/gtest"));
        QCOMPARE(atoms.value(QStringLiteral("libpng")), QStringLiteral("media-libs/libpng:="));
        QVERIFY(!atoms.contains(QStringLiteral("PkgConfig")));
        QCOMPARE(res.at(2).how, DependencyResolution::Eclass);
        const auto cargo = DependencyHints::resolve(BuildFiles::fromCargo(fixture("cargo/Cargo.toml")), nullptr);
        QCOMPARE(cargo.at(0).how, DependencyResolution::Vendored);
        QCOMPARE(cargo.at(2).atoms, QStringList{QStringLiteral("dev-libs/openssl:=")});
    }
    void draftCMakeRelease()
    {
        SourceInfo info;
        SourceProbe::analyzeTree(QStringLiteral(TEST_DATA "/sources/cmake"), &info);
        info.url = QStringLiteral("https://github.com/example/viewer");
        info.host = QStringLiteral("github");
        info.owner = QStringLiteral("example");
        info.repo = QStringLiteral("viewer");
        info.name = info.repo;
        info.tag = QStringLiteral("v2.1");
        info.version = QStringLiteral("2.1");
        info.description = QStringLiteral("A desktop notes application");
        const auto atoms = DependencyHints::atomMap(DependencyHints::resolve(info.dependencies, nullptr));
        const QString e = EbuildDraft::generate(info, atoms, QDateTime(QDate(2026, 9, 28), QTime(12, 0), QTimeZone::UTC));
        QVERIFY2(e.contains(QStringLiteral("EAPI=8\n\ninherit cmake\n")), qPrintable(e));
        QVERIFY(e.contains(QStringLiteral("SRC_URI=\"https://github.com/example/viewer/archive/refs/tags/v${PV}.tar.gz -> ${P}.gh.tar.gz\"")));
        QVERIFY(e.contains(QStringLiteral("S=\"${WORKDIR}/viewer-${PV}\"")));
        QVERIFY(e.contains(QStringLiteral("LICENSE=\"MIT\"")));
        QVERIFY(e.contains(QStringLiteral("\tdev-qt/qtbase:6[gui,widgets]\n")));
        QVERIFY(e.contains(QStringLiteral("\ttest? ( dev-cpp/gtest )\n")));
        QVERIFY(e.contains(QStringLiteral("IUSE=\"test\"")));
        QVERIFY(e.contains(QStringLiteral("BDEPEND=\"\n\tvirtual/pkgconfig\n\"")));
        QVERIFY(e.contains(QStringLiteral("#   media-video/pipewire:=  (libpipewire-0.3)")));
        QVERIFY(e.contains(QStringLiteral("-DBUILD_TESTING=$(usex test)")));
        QVERIFY(!e.contains(QStringLiteral("\n\n\n")));
        QCOMPARE(EbuildDraft::suggestedCategory(info), QStringLiteral("gui-apps"));
        QCOMPARE(EbuildDraft::fileName(info), QStringLiteral("viewer-2.1.ebuild"));
    }
    void draftPythonAndSnapshot()
    {
        SourceInfo info;
        SourceProbe::analyzeTree(QStringLiteral(TEST_DATA "/sources/python"), &info);
        info.url = QStringLiteral("https://github.com/example/fancy");
        info.host = QStringLiteral("github");
        info.owner = QStringLiteral("example");
        info.repo = QStringLiteral("fancy");
        info.snapshot = true;
        info.commit = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
        info.version = QStringLiteral("0_p20260928");
        const QMap<QString, QString> atoms{{QStringLiteral("requests"), QStringLiteral("dev-python/requests[${PYTHON_USEDEP}]")},
                                           {QStringLiteral("click"), QStringLiteral("dev-python/click[${PYTHON_USEDEP}]")}};
        const QString e = EbuildDraft::generate(info, atoms);
        QVERIFY2(e.contains(QStringLiteral("DISTUTILS_USE_PEP517=hatchling\nPYTHON_COMPAT=( python3_{12..14} )")), qPrintable(e));
        QVERIFY(e.contains(QStringLiteral("inherit distutils-r1")));
        QVERIFY(e.contains(QStringLiteral("COMMIT=\"0123456789abcdef0123456789abcdef01234567\"")));
        QVERIFY(e.contains(QStringLiteral("archive/${COMMIT}.tar.gz -> ${P}.gh.tar.gz")));
        QVERIFY(e.contains(QStringLiteral("S=\"${WORKDIR}/fancy-${COMMIT}\"")));
        QVERIFY(e.contains(QStringLiteral("RDEPEND=\"\n\tdev-python/requests[${PYTHON_USEDEP}]\n\tdev-python/click[${PYTHON_USEDEP}]\n\"")));
        QVERIFY(!e.contains(QStringLiteral("DEPEND=\"\n\t${DEPEND}")));
        QCOMPARE(EbuildDraft::packageName(info), QStringLiteral("fancy-tool"));
        // Has console scripts: a program, not a library.
        QCOMPARE(EbuildDraft::suggestedCategory(info), QStringLiteral("app-misc"));
    }
    void localProbeIsAsyncAndOffline()
    {
        SourceProbe probe;
        QSignalSpy finished(&probe, &SourceProbe::finished);
        probe.probe(QStringLiteral(TEST_DATA "/sources/meson"));
        QCOMPARE(finished.count(), 0); // never synchronous
        QVERIFY(probe.busy());
        QVERIFY(finished.wait(5000));
        const SourceInfo info = finished.first().first().value<SourceInfo>();
        QVERIFY(info.ok());
        QCOMPARE(info.buildSystem, QStringLiteral("meson"));
        QVERIFY(!probe.busy());
        QSignalSpy failed(&probe, &SourceProbe::finished);
        probe.probe(QStringLiteral("not a url"));
        QVERIFY(failed.wait(5000));
        QVERIFY(!failed.first().first().value<SourceInfo>().ok());
        // A second probe supersedes the first; only the second reports.
        QSignalSpy superseded(&probe, &SourceProbe::finished);
        probe.probe(QStringLiteral(TEST_DATA "/sources/cmake"));
        probe.probe(QStringLiteral(TEST_DATA "/sources/python"));
        QVERIFY(superseded.wait(5000));
        QTest::qWait(50);
        QCOMPARE(superseded.count(), 1);
        QCOMPARE(superseded.first().first().value<SourceInfo>().buildSystem, QStringLiteral("distutils-r1"));
    }
};

QTEST_GUILESS_MAIN(TstSourceProbe)
#include "tst_sourceprobe.moc"
