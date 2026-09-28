// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/emergeplan.h"

#include <QFile>
#include <QJsonArray>
#include <QTest>

using namespace QindaPortage::Core;

// Fixtures in tests/data/emerge: *.txt without "synthetic-" were captured on
// qinda-top with `emerge --pretend --verbose --tree --color=n --nospinner`;
// synthetic-* follow emerge's printed formats for cases the machine cannot
// produce on demand.
static EmergePlan load(const char *name)
{
    QFile f(QStringLiteral(TEST_DATA "/emerge/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) qFatal("missing fixture %s", name);
    return EmergePlanParser::parse(QString::fromUtf8(f.readAll()));
}

class TstEmergePlan : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanInstall()
    {
        const EmergePlan p = load("clean.txt");
        QVERIFY(p.resolved());
        QCOMPARE(p.total, 1);
        const MergeItem &i = p.items.first();
        QCOMPARE(i.action, MergeItem::New);
        QCOMPARE(i.key, QStringLiteral("app-misc/jq"));
        QCOMPARE(i.version, QStringLiteral("1.8.2"));
        QCOMPARE(i.slot, QStringLiteral("0/1"));
        QCOMPARE(i.downloadKiB, 1407);
        QCOMPARE(i.use.size(), 3);
    }
    void worldUpdate()
    {
        const EmergePlan p = load("world.txt");
        QVERIFY(p.resolved());
        // "Total: 219 packages (26 upgrades, 1 new, 192 reinstalls, 105 binaries)"
        QCOMPARE(p.total, 219);
        QCOMPARE(p.upgrades, 26);
        QCOMPARE(p.newPackages, 1);
        QCOMPARE(p.reinstalls, 192);
        QCOMPARE(p.binaries, 105);
        QVERIFY(!p.rebuildCauses.isEmpty());
        QVERIFY(p.rebuildCauses.first().startsWith(QStringLiteral("dev-lang/perl-5.44.0-1")));
    }
    void binaryBuildIdAndOldVersion()
    {
        bool ok = false;
        const MergeItem i = EmergePlanParser::parseItemLine(QStringLiteral(
            "[binary    gU  ]    x11-misc/sddm-0.21.0_p20251101-1::gentoo [0.21.0_p20250502-r1::gentoo] "
            "USE=\"systemd (-elogind) -test (-X%*)\" 3,560 KiB"), &ok);
        QVERIFY(ok);
        QCOMPARE(i.key, QStringLiteral("x11-misc/sddm"));
        QCOMPARE(i.version, QStringLiteral("0.21.0_p20251101"));
        QCOMPARE(i.buildId, 1);
        QCOMPARE(i.oldVersion, QStringLiteral("0.21.0_p20250502-r1"));
        QCOMPARE(i.action, MergeItem::Upgrade);
        QCOMPARE(i.depth, 3);
        QVERIFY(i.binary);
        QCOMPARE(i.changedFlags(), QStringList{QStringLiteral("-X")});
        const MergeItem perl = EmergePlanParser::parseItemLine(QStringLiteral(
            "[nomerge       ] dev-lang/perl-5.44.0-1:0/5.44::gentoo [5.42.2:0/5.42::gentoo] USE=\"gdbm\""), &ok);
        QCOMPARE(perl.version, QStringLiteral("5.44.0"));
        QCOMPARE(perl.slot, QStringLiteral("0/5.44"));
        QCOMPARE(perl.oldSlot, QStringLiteral("0/5.42"));
    }
    void useExpand()
    {
        bool ok = false;
        const MergeItem i = EmergePlanParser::parseItemLine(QStringLiteral(
            "[ebuild   R    ] sys-auth/nss-mdns-0.15.1::gentoo  USE=\"-test\" ABI_X86=\"32* (64) (-x32)\" 367 KiB"), &ok);
        QCOMPARE(i.use.size(), 4);
        QCOMPARE(i.use.at(1).flag, QStringLiteral("abi_x86_32"));
        QVERIFY(i.use.at(1).changed && i.use.at(1).enabled);
        QVERIFY(i.use.at(2).locked);
        QCOMPARE(i.action, MergeItem::Reinstall);
    }
    void newSlot()
    {
        const EmergePlan p = load("slot.txt");
        QCOMPARE(p.newSlots, 1);
        QCOMPARE(p.newPackages, 6);
    }
    void maskedKeyword()
    {
        const EmergePlan p = load("keyword.txt");
        QCOMPARE(p.problems.size(), 1);
        QCOMPARE(p.problems.first().kind, Problem::Masked);
        QCOMPARE(p.problems.first().fixes.first().kind, Fix::AcceptKeyword);
        QCOMPARE(p.problems.first().fixes.first().atom, QStringLiteral("=dev-vcs/git-9999"));
        QCOMPARE(p.problems.first().fixes.first().values, QStringList{QStringLiteral("**")});
    }
    void useChangeForArgument()
    {
        const EmergePlan p = load("usechange.txt");
        QCOMPARE(p.problems.size(), 1);
        QCOMPARE(p.problems.first().kind, Problem::UseChange);
        QCOMPARE(p.problems.first().fixes.first().values, QStringList{QStringLiteral("+static-libs")});
    }
    void requiredUse()
    {
        const EmergePlan p = load("requireduse.txt");
        QCOMPARE(p.problems.size(), 1);
        const Problem &pr = p.problems.first();
        QCOMPARE(pr.kind, Problem::RequiredUse);
        QStringList values;
        for (const Fix &f : pr.fixes) values += f.values;
        // vulkan? ( any-of ( X wayland ) ) xv? ( X )
        QVERIFY(values.contains(QStringLiteral("-vulkan")));
        QVERIFY(values.contains(QStringLiteral("+wayland")));
        QVERIFY(values.contains(QStringLiteral("-xv")));
        QVERIFY(values.contains(QStringLiteral("+X")));
        QCOMPARE(pr.fixes.first().atom, QStringLiteral("=media-video/mpv-0.41.0-r2"));
    }
    void notFoundWithSuggestions()
    {
        const EmergePlan p = load("missing.txt");
        QCOMPARE(p.problems.first().kind, Problem::NotFound);
        QCOMPARE(p.problems.first().fixes.size(), 3);
        QCOMPARE(p.problems.first().fixes.first().atom, QStringLiteral("app-misc/evtest"));
    }
    void slotConflict()
    {
        const EmergePlan p = load("synthetic-slotconflict.txt");
        QCOMPARE(p.problems.size(), 1);
        QCOMPARE(p.problems.first().kind, Problem::SlotConflict);
        QCOMPARE(p.problems.first().packages, QStringList{QStringLiteral("dev-libs/foo:0")});
        QCOMPARE(p.problems.first().fixes.size(), 2);
        QCOMPARE(p.problems.first().fixes.at(1).atom, QStringLiteral("app-misc/baz"));
    }
    void autounmask()
    {
        const EmergePlan p = load("synthetic-autounmask.txt");
        QCOMPARE(p.problems.size(), 3);
        QCOMPARE(p.problems.at(0).kind, Problem::KeywordChange);
        QCOMPARE(p.problems.at(0).fixes.first().values, QStringList{QStringLiteral("~amd64")});
        QCOMPARE(p.problems.at(1).fixes.first().atom, QStringLiteral(">=dev-libs/baz-2.1"));
        QVERIFY(p.problems.at(1).fixes.first().reason.contains(QStringLiteral("newtool-0.9::guru")));
        QCOMPARE(p.problems.at(2).fixes.first().values, QStringList{QStringLiteral("Newtool-EULA")});
        QVERIFY(p.items.first().keywordMasked);
    }
    void blocker()
    {
        const EmergePlan p = load("synthetic-blocker.txt");
        QCOMPARE(p.problems.size(), 1);
        QCOMPARE(p.problems.first().kind, Problem::Blocker);
    }
    void unsatisfied()
    {
        const EmergePlan p = load("synthetic-unsatisfied.txt");
        QCOMPARE(p.problems.first().kind, Problem::Unsatisfied);
        QVERIFY(p.problems.first().packages.contains(QStringLiteral("app-misc/tool-2.0::guru")));
    }
    void compactJson()
    {
        const QJsonObject o = load("world.txt").toJson(10);
        QCOMPARE(o.value(QStringLiteral("merges")).toArray().size(), 10);
        QVERIFY(o.value(QStringLiteral("truncated")).toInt() > 0);
        // Upgrades are listed before plain rebuilds.
        QCOMPARE(o.value(QStringLiteral("merges")).toArray().first().toObject()
                     .value(QStringLiteral("action")).toString() == QStringLiteral("rebuild"), false);
    }
};

QTEST_GUILESS_MAIN(TstEmergePlan)
#include "tst_emergeplan.moc"
