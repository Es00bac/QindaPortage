// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Ops {

// A dependency a project declares in its own build files.
struct DeclaredDependency
{
    enum Kind { CMakePackage, PkgConfig, Meson, Python, Cargo, Go, Autoconf, Node };
    Kind kind = PkgConfig;
    QString name;        // as written: "Qt6", "libpng", "requests", "serde"
    QStringList components; // CMake COMPONENTS (Qt6 Core Gui …)
    QString constraint;  // ">=2.0" / "1.2" when given
    bool optional = false;
    bool build = false;  // build-only (python build-system requires, dev tools)

    static QString kindName(Kind k);
    QJsonObject toJson() const;
};

// Pure parsers over build-file text; no I/O, so they are tested offline.
namespace BuildFiles {

// The eclass-level build system chosen from top-level file names, in
// priority order: "cmake", "meson", "cargo", "go-module", "distutils-r1",
// "autotools", "node", "make", or "" when unknown.
QString detectBuildSystem(const QStringList &topLevelFiles);
// Build files worth fetching for dependency extraction.
QStringList interestingFiles(const QStringList &topLevelFiles);

QVector<DeclaredDependency> fromCMake(const QString &text);
QVector<DeclaredDependency> fromMeson(const QString &text);
QVector<DeclaredDependency> fromPyproject(const QString &text);
QVector<DeclaredDependency> fromSetupPy(const QString &text);
QVector<DeclaredDependency> fromCargo(const QString &text);
QVector<DeclaredDependency> fromGoMod(const QString &text);
QVector<DeclaredDependency> fromAutoconf(const QString &text);
QVector<DeclaredDependency> fromPackageJson(const QString &text);
// Dispatches on the file name.
QVector<DeclaredDependency> fromFile(const QString &fileName, const QString &text);

// PEP 517 backend for DISTUTILS_USE_PEP517: "setuptools", "hatchling",
// "flit", "poetry", "pdm-backend", "maturin", "scikit-build-core",
// "meson-python"; "setuptools" when pyproject has no build-system.
QString pep517Backend(const QString &pyprojectText);
// Project metadata: name/version/description from pyproject or Cargo.toml.
QString tomlValue(const QString &text, const QString &table, const QString &key);
// Hints for categorisation.
bool declaresLibraryOnly(const QString &cmakeText);
bool hasPythonScripts(const QString &pyprojectText);

// Gentoo license name for an SPDX id ("GPL-3.0-or-later" -> "GPL-3+");
// empty when unknown. Also recognises LICENSE file text heuristically.
QString gentooLicense(const QString &spdx);
QString licenseFromText(const QString &text);

// "v1.2.3" / "jq-1.8.2" / "release-2.0" -> "1.2.3" (a PMS version) or "".
QString versionFromTag(const QString &tag);

} // namespace BuildFiles

} // namespace QindaPortage::Ops
