// SPDX-License-Identifier: GPL-3.0-or-later
//
// qportage-dump: prints what QindaPortage's core reads from this machine.
// A development and verification seam, not installed.
//
//   qportage-dump summary
//   qportage-dump pkg <category/name>
//   qportage-dump orphans
//   qportage-dump unsatisfied

#include "core/packagegraph.h"
#include "core/portageconfig.h"
#include "core/vardb.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

using namespace QindaPortage::Core;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    QTextStream out(stdout);
    const QString cmd = args.value(0, QStringLiteral("summary"));

    QElapsedTimer t;
    t.start();
    VarDb db;
    db.load();
    const qint64 dbMs = t.restart();
    PortageConfig config;
    config.load();
    const qint64 cfgMs = t.restart();
    PackageGraph graph;
    graph.build(db, config);
    const qint64 graphMs = t.elapsed();

    if (cmd == QLatin1String("summary")) {
        out << "packages " << db.packages().size() << " (vardb " << dbMs << " ms)\n";
        out << "profile " << config.profileName() << " chain " << config.profileChain().size()
            << " (config " << cfgMs << " ms)\n";
        for (const Repository &r : config.repositories()) {
            out << "repo " << r.name << " " << r.location << (r.hasMd5Cache ? " md5-cache" : "") << "\n";
        }
        out << "world atoms " << config.worldAtoms().size() << " nodes " << graph.worldNodes().size() << "\n";
        out << "system atoms " << config.systemAtoms().size() << "\n";
        out << "edges " << graph.edges().size() << " unsatisfied " << graph.unsatisfied().size()
            << " orphans " << graph.orphans().size() << " (graph " << graphMs << " ms)\n";
        for (const char *k : {"use", "accept_keywords", "mask", "license", "env"}) {
            out << "package." << k << " lines " << config.packageLines(QString::fromLatin1(k)).size() << "\n";
        }
    } else if (cmd == QLatin1String("pkg")) {
        for (int idx : db.byKey(args.value(1))) {
            const InstalledPackage &p = db.packages().at(idx);
            out << p.cpv() << ":" << p.slot << "::" << p.repository << "  " << p.description << "\n";
            out << "  role " << graph.role(idx) << " depth " << graph.depth(idx) << "\n  why:";
            for (int n : graph.whyInstalled(idx)) out << " " << db.packages().at(n).key();
            out << "\n  USE:\n";
            for (const QString &flag : p.iuse) {
                const UseOrigin o = config.useOrigin(p, flag);
                out << "    " << (o.enabled ? "+" : "-") << flag << "  [" << o.where << "]\n";
            }
            out << "  deps:\n";
            for (int e : graph.outEdges(idx)) {
                const auto &edge = graph.edges().at(e);
                out << "    " << db.packages().at(edge.to).cpv() << " (" << PackageGraph::typesText(edge.types)
                    << ") via " << edge.atom << (edge.conditions.isEmpty() ? QString()
                    : QStringLiteral(" if ") + edge.conditions.join(QLatin1Char(','))) << "\n";
            }
            out << "  rdeps " << graph.inEdges(idx).size() << ":";
            for (int e : graph.inEdges(idx)) out << " " << db.packages().at(graph.edges().at(e).from).key();
            out << "\n";
        }
    } else if (cmd == QLatin1String("orphans")) {
        for (int n : graph.orphans()) out << db.packages().at(n).cpv() << "\n";
    } else if (cmd == QLatin1String("unsatisfied")) {
        for (const auto &u : graph.unsatisfied()) {
            out << db.packages().at(u.from).cpv() << " (" << PackageGraph::typesText(u.types) << "): "
                << u.requirement << "\n";
        }
    }
    return 0;
}
