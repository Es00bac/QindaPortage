// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ops/buildfiles.h"

#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core { class RepoIndex; }

namespace QindaPortage::Ops {

// How one declared dependency maps onto Portage.
struct DependencyResolution
{
    enum How { Hint, Index, Eclass, Vendored, Unresolved };
    DeclaredDependency dep;
    QStringList atoms;   // e.g. {"dev-qt/qtbase:6[gui,widgets]"}
    How how = Unresolved;
    QString note;

    static QString howName(How h);
};

namespace DependencyHints {

// Common CMake package / pkg-config / meson names -> Gentoo atom
// (lower-case keys). Every atom was checked against ::gentoo on qinda-top.
const QMap<QString, QString> &table();

// Maps each dependency: the hint table first, then (for python, pkg-config
// and unknown names) an exact package-name match in `index` when given.
// Cargo and Go dependencies are vendored by the eclass except the *-sys
// crates that link a system library.
QVector<DependencyResolution> resolve(const QVector<DeclaredDependency> &deps, const Core::RepoIndex *index);

// name -> space-separated atoms, the input EbuildDraft::generate expects.
QMap<QString, QString> atomMap(const QVector<DependencyResolution> &resolutions);
QJsonArray toJson(const QVector<DependencyResolution> &resolutions);

} // namespace DependencyHints

} // namespace QindaPortage::Ops
