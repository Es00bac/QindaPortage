// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ops/sourceinfo.h"

#include <QDateTime>
#include <QMap>
#include <QString>

namespace QindaPortage::Ops::EbuildDraft {

// Proposed PN: the repository name, lower-cased, invalid characters -> '-'.
QString packageName(const SourceInfo &info);
// Category heuristics: dev-python for Python libraries, dev-libs for
// libraries, games-misc / media-* / media-fonts from topics and description,
// gui-apps for toolkit-linked programs, else app-misc.
QString suggestedCategory(const SourceInfo &info);
// "<pn>-<version>.ebuild"
QString fileName(const SourceInfo &info);

// A first-draft EAPI 8 ebuild. `dependencyAtoms` maps a declared
// dependency name to space-separated atoms (DependencyHints::atomMap).
// AGENT-NOTE: a draft, not a finished ebuild: Cargo/Go vendoring and
// optional dependencies are left as marked TODOs for the assistant and the
// pretend/build repair loop.
QString generate(const SourceInfo &info, const QMap<QString, QString> &dependencyAtoms,
                 const QDateTime &now = QDateTime::currentDateTimeUtc());

} // namespace QindaPortage::Ops::EbuildDraft
