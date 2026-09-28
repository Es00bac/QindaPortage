// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/packagegraph.h"
#include "core/portageconfig.h"
#include "core/vardb.h"

#include <QTest>

using namespace QindaPortage::Core;

// Fixture: tests/data/root is a miniature system. world = app-misc/tool,
// system = sys-apps/base, app-misc/leftover is an orphan with a missing dep.
class TstGraph : public QObject
{
    Q_OBJECT

    VarDb db;
    PortageConfig config;
    PackageGraph graph;

    int node(const char *key) const { return db.byKey(QString::fromLatin1(key)).value(0, -1); }

private Q_SLOTS:
    void initTestCase()
    {
        const QString root = QStringLiteral(TEST_DATA "/root");
        QVERIFY(db.load(root + QStringLiteral("/var/db/pkg")));
        QVERIFY(config.load(root));
        graph.build(db, config);
        QCOMPARE(db.packages().size(), 7);
    }
    void roles()
    {
        QCOMPARE(graph.role(node("app-misc/tool")), PackageGraph::WorldRoot);
        QCOMPARE(graph.role(node("sys-apps/base")), PackageGraph::SystemRoot);
        QCOMPARE(graph.role(node("dev-lang/python")), PackageGraph::Dependency);
        QCOMPARE(graph.orphans(), QVector<int>{node("app-misc/leftover")});
    }
    void edgesFollowUse()
    {
        QStringList deps;
        for (int e : graph.outEdges(node("app-misc/tool"))) deps.append(db.packages().at(graph.edges().at(e).to).key());
        deps.sort();
        // ssl is off, gui is on; BDEPEND make is a build edge.
        QCOMPARE(deps, (QStringList{QStringLiteral("dev-build/make"), QStringLiteral("dev-libs/lib"),
                                    QStringLiteral("x11/toolkit")}));
    }
    void anyOfPicksInstalled()
    {
        const auto &out = graph.outEdges(node("dev-libs/lib"));
        QCOMPARE(out.size(), 1);
        QCOMPARE(graph.edges().at(out.first()).to, node("dev-lang/python"));
        QVERIFY(graph.edges().at(out.first()).anyOf);
    }
    void whyInstalled()
    {
        const QVector<int> chain = graph.whyInstalled(node("dev-lang/python"));
        QCOMPARE(chain, (QVector<int>{node("app-misc/tool"), node("dev-libs/lib"), node("dev-lang/python")}));
    }
    void unsatisfied()
    {
        QCOMPARE(graph.unsatisfied().size(), 1);
        QCOMPARE(graph.unsatisfied().first().requirement, QStringLiteral("dev-libs/missing"));
    }
    void reverse()
    {
        const QSet<int> r = graph.reverseDependencies(node("dev-libs/lib"), PackageGraph::Run | PackageGraph::Build);
        QCOMPARE(r, (QSet<int>{node("app-misc/tool"), node("x11/toolkit")}));
    }
    void useOrigins()
    {
        const InstalledPackage &tool = db.packages().at(node("app-misc/tool"));
        const UseOrigin gui = config.useOrigin(tool, QStringLiteral("gui"));
        QCOMPARE(gui.source, UseOrigin::PackageUse);
        QVERIFY(gui.where.endsWith(QStringLiteral("package.use/50-tool:2")));
        QCOMPARE(config.useOrigin(tool, QStringLiteral("ssl")).source, UseOrigin::MakeConf);
        QCOMPARE(config.userUseLinesFor(tool).first().comment, QStringLiteral("Toolkit front end is wanted."));
    }
};

QTEST_GUILESS_MAIN(TstGraph)
#include "tst_graph.moc"
