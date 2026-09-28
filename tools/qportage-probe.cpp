// SPDX-License-Identifier: GPL-3.0-or-later
//
// qportage-probe: runs SourceProbe on a URL or directory and prints the
// SourceInfo, the dependency mapping and the drafted ebuild. A development
// and verification seam, not installed.
//
//   qportage-probe <url|dir> [--no-index] [--cache DIR]

#include "core/portageconfig.h"
#include "core/repoindex.h"
#include "ops/dependencyhints.h"
#include "ops/ebuilddraft.h"
#include "ops/sourceprobe.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

using namespace QindaPortage;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    QTextStream out(stdout);
    if (args.isEmpty()) {
        out << "usage: qportage-probe <url|dir> [--no-index] [--cache DIR]\n";
        return 2;
    }
    const bool useIndex = !args.removeAll(QStringLiteral("--no-index"));
    Ops::SourceProbe probe;
    if (const int c = args.indexOf(QStringLiteral("--cache")); c >= 0 && c + 1 < args.size()) {
        probe.setCacheDir(args.at(c + 1));
        args.remove(c, 2);
    }

    Core::PortageConfig config;
    Core::RepoIndex index;
    if (useIndex) {
        config.load();
        index.load(config);
    }

    QObject::connect(&probe, &Ops::SourceProbe::progress, [&out](const QString &m) { out << "... " << m << "\n"; out.flush(); });
    QObject::connect(&probe, &Ops::SourceProbe::finished, [&](const Ops::SourceInfo &info) {
        out << QJsonDocument(info.toJson()).toJson(QJsonDocument::Indented);
        if (!info.ok()) {
            app.exit(1);
            return;
        }
        const auto resolutions = Ops::DependencyHints::resolve(info.dependencies, useIndex ? &index : nullptr);
        out << "--- dependencies\n" << QJsonDocument(Ops::DependencyHints::toJson(resolutions)).toJson(QJsonDocument::Indented);
        out << "--- " << Ops::EbuildDraft::suggestedCategory(info) << "/" << Ops::EbuildDraft::packageName(info) << "/"
            << Ops::EbuildDraft::fileName(info) << "\n";
        out << Ops::EbuildDraft::generate(info, Ops::DependencyHints::atomMap(resolutions));
        app.exit(0);
    });
    probe.probe(args.first());
    return app.exec();
}
