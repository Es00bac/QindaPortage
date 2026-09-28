// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/pretendrunner.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaPortage::Ops;

#define QVERIFY_OPEN(f) \
    do { if (!(f).open(QIODevice::WriteOnly)) qFatal("cannot write %s", qPrintable((f).fileName())); } while (false)

// Offline: a copy of the fixture root's /etc/portage, a fake emerge that
// prints a captured fixture, and a fake ebuild that writes a Manifest.
class TstPretendRunner : public QObject
{
    Q_OBJECT

    QTemporaryDir tmp;

    QString script(const QString &name, const QString &body)
    {
        const QString path = tmp.filePath(name);
        QFile f(path);
        QVERIFY_OPEN(f);
        f.write(body.toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }

    static QString read(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

    static Plan planWithEbuild()
    {
        Plan p;
        PlanStep use;
        use.kind = PlanStep::SetUse;
        use.atom = QStringLiteral("app-misc/tool");
        use.values = {QStringLiteral("ssl")};
        use.rationale = QStringLiteral("needs TLS");
        PlanStep eb;
        eb.kind = PlanStep::WriteEbuild;
        eb.path = QStringLiteral("dev-util/newthing/newthing-0.1.ebuild");
        eb.content = QStringLiteral("EAPI=8\n");
        p.steps = {use, eb};
        return p;
    }

    PretendRunner *runner(const QString &emerge, const QString &ebuild, const QString &scratch)
    {
        auto *r = new PretendRunner(this);
        r->setEmergeProgram(emerge);
        r->setEbuildProgram(ebuild);
        r->setSourceConfigDir(QStringLiteral(TEST_DATA "/root/etc/portage"));
        r->setSourceOverlayDir(tmp.filePath(QStringLiteral("none")));
        r->setScratchDir(tmp.filePath(scratch));
        return r;
    }

private Q_SLOTS:
    void prepareConfigRoot()
    {
        const QString root = tmp.filePath(QStringLiteral("cfg"));
        QStringList ebuilds;
        QString error;
        QVERIFY2(PretendRunner::prepareConfigRoot(QStringLiteral(TEST_DATA "/root/etc/portage"), root,
                                                  tmp.filePath(QStringLiteral("no-overlay")),
                                                  tmp.filePath(QStringLiteral("overlay")), planWithEbuild(),
                                                  &ebuilds, &error), qPrintable(error));
        // The relative make.profile link still resolves from the copy.
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/etc/portage/make.profile/packages")));
        const QString use = read(root + QStringLiteral("/etc/portage/package.use/90-qindaportage"));
        QVERIFY2(use.contains(QStringLiteral("— needs TLS\napp-misc/tool ssl\n")), qPrintable(use));
        QVERIFY(read(root + QStringLiteral("/etc/portage/package.use/50-tool")).contains(QStringLiteral("app-misc/tool gui")));
        // The original is untouched.
        QVERIFY(!QFileInfo::exists(QStringLiteral(TEST_DATA "/root/etc/portage/package.use/90-qindaportage")));
        QCOMPARE(ebuilds, QStringList{tmp.filePath(QStringLiteral("overlay/dev-util/newthing/newthing-0.1.ebuild"))});
        QCOMPARE(read(tmp.filePath(QStringLiteral("overlay/profiles/repo_name"))), QStringLiteral("qinda-local\n"));
        QCOMPARE(read(tmp.filePath(QStringLiteral("overlay/profiles/categories"))), QStringLiteral("dev-util\n"));
        QVERIFY(read(root + QStringLiteral("/etc/portage/repos.conf/zz-qinda-local-pretend.conf"))
                    .contains(QStringLiteral("location = ") + tmp.filePath(QStringLiteral("overlay"))));
    }
    void managedFilePlacement()
    {
        const QString etc = QStringLiteral(TEST_DATA "/root/etc/portage");
        QCOMPARE(PretendRunner::managedFile(etc, QStringLiteral("package.use")),
                 etc + QStringLiteral("/package.use/90-qindaportage"));
        QCOMPARE(PretendRunner::managedFile(etc, QStringLiteral("make.conf")), etc + QStringLiteral("/make.conf"));
        QCOMPARE(PretendRunner::managedFile(etc, QStringLiteral("package.mask")),
                 etc + QStringLiteral("/package.mask/90-qindaportage"));
    }
    void emergeArguments()
    {
        QCOMPARE(PretendRunner::emergeArguments({QStringLiteral("app-misc/jq")},
                                                {QStringLiteral("--oneshot"), QStringLiteral("--update")}),
                 (QStringList{QStringLiteral("--pretend"), QStringLiteral("--verbose"), QStringLiteral("--tree"),
                              QStringLiteral("--color=n"), QStringLiteral("--nospinner"), QStringLiteral("--update"),
                              QStringLiteral("app-misc/jq")}));
    }
    void runsManifestThenEmergeWithConfigRoot()
    {
        const QString log = tmp.filePath(QStringLiteral("calls.log"));
        const QString emerge = script(QStringLiteral("emerge"), QStringLiteral(
            "#!/bin/sh\necho \"emerge $PORTAGE_CONFIGROOT $*\" >> %1\ncat %2\nexit 0\n")
            .arg(log, QStringLiteral(TEST_DATA "/emerge/clean.txt")));
        const QString ebuild = script(QStringLiteral("ebuild"), QStringLiteral(
            "#!/bin/sh\necho \"ebuild $DISTDIR $*\" >> %1\ntouch \"$(dirname \"$1\")/Manifest\"\n").arg(log));
        PretendRunner *r = runner(emerge, ebuild, QStringLiteral("scratch"));
        QSignalSpy done(r, &PretendRunner::finished);
        QSignalSpy stages(r, &PretendRunner::stageChanged);
        QVERIFY(r->start({QStringLiteral("app-misc/jq")}, {}, planWithEbuild()));
        QVERIFY(done.wait(10000));
        QCOMPARE(done.first().at(0).toBool(), true);
        QCOMPARE(r->result().total, 1);
        QCOMPARE(stages.last().at(0).toString(), QStringLiteral("resolving"));
        const QStringList calls = read(log).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(calls.size(), 2);
        QVERIFY(calls.at(0).startsWith(QStringLiteral("ebuild ") + tmp.filePath(QStringLiteral("scratch/distfiles"))));
        QVERIFY(calls.at(1).startsWith(QStringLiteral("emerge ") + tmp.filePath(QStringLiteral("scratch/root"))));
        QVERIFY(calls.at(1).endsWith(QStringLiteral("--nospinner app-misc/jq")));
    }
    void plainRunUsesRealConfig()
    {
        const QString log = tmp.filePath(QStringLiteral("plain.log"));
        const QString emerge = script(QStringLiteral("emerge3"), QStringLiteral(
            "#!/bin/sh\necho \"[$PORTAGE_CONFIGROOT]\" >> %1\ncat %2\nexit 1\n")
            .arg(log, QStringLiteral(TEST_DATA "/emerge/missing.txt")));
        PretendRunner *r = runner(emerge, QStringLiteral("false"), QStringLiteral("scratch4"));
        QSignalSpy done(r, &PretendRunner::finished);
        QVERIFY(r->start({QStringLiteral("app-misc/doesnotexist")}));
        QVERIFY(done.wait(10000));
        // A resolver failure is still a successful pretend with problems.
        QCOMPARE(done.first().at(0).toBool(), true);
        QCOMPARE(r->exitCode(), 1);
        QVERIFY(!r->result().resolved());
        QCOMPARE(read(log).trimmed(), QStringLiteral("[]"));
    }
    void manifestFailureStops()
    {
        const QString emerge = script(QStringLiteral("emerge2"), QStringLiteral("#!/bin/sh\nexit 0\n"));
        const QString ebuild = script(QStringLiteral("ebuild2"), QStringLiteral("#!/bin/sh\necho 'fetch failed'\nexit 1\n"));
        PretendRunner *r = runner(emerge, ebuild, QStringLiteral("scratch2"));
        QSignalSpy done(r, &PretendRunner::finished);
        QVERIFY(r->start({QStringLiteral("dev-util/newthing")}, {}, planWithEbuild()));
        QVERIFY(done.wait(10000));
        QCOMPARE(done.first().at(0).toBool(), false);
        QVERIFY(r->error().contains(QStringLiteral("fetch failed")));
    }
    // Opt-in (QINDAPORTAGE_LIVE_TESTS=1): the real emerge and ebuild against
    // this machine's /etc/portage, as the unprivileged user. Writes only
    // into the scratch directory.
    void liveVirtualPlan()
    {
        if (qEnvironmentVariableIntValue("QINDAPORTAGE_LIVE_TESTS") != 1) QSKIP("set QINDAPORTAGE_LIVE_TESTS=1");
        Plan p;
        PlanStep jq;
        jq.kind = PlanStep::SetUse;
        jq.atom = QStringLiteral("app-misc/jq");
        jq.values = {QStringLiteral("static-libs")};
        PlanStep onig = jq;
        onig.atom = QStringLiteral("dev-libs/oniguruma");
        PlanStep eb;
        eb.kind = PlanStep::WriteEbuild;
        eb.path = QStringLiteral("app-misc/qportage-probe/qportage-probe-1.0.ebuild");
        eb.content = QStringLiteral("EAPI=8\nDESCRIPTION=\"QindaPortage pretend probe\"\n"
                                    "HOMEPAGE=\"https://example.invalid\"\nS=\"${WORKDIR}\"\n"
                                    "LICENSE=\"MIT\"\nSLOT=\"0\"\nKEYWORDS=\"amd64\"\n");
        p.steps = {jq, onig, eb};
        PretendRunner r;
        r.setScratchDir(tmp.filePath(QStringLiteral("live")));
        QSignalSpy done(&r, &PretendRunner::finished);
        QVERIFY(r.start({QStringLiteral("app-misc/jq"), QStringLiteral("app-misc/qportage-probe")}, {}, p));
        QVERIFY(done.wait(180000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(r.error()));
        QVERIFY2(r.result().resolved(), qPrintable(r.rawOutput().right(3000)));
        bool jqStatic = false, probe = false;
        for (const auto &item : r.result().items) {
            if (item.key == QLatin1String("app-misc/jq")) {
                for (const auto &f : item.use) jqStatic |= f.flag == QLatin1String("static-libs") && f.enabled;
            }
            probe |= item.key == QLatin1String("app-misc/qportage-probe") && item.repository == QLatin1String("qinda-local");
        }
        QVERIFY2(jqStatic, qPrintable(r.rawOutput().right(3000)));
        QVERIFY2(probe, qPrintable(r.rawOutput().right(3000)));
        QVERIFY(!QFileInfo::exists(QStringLiteral("/var/db/repos/qinda-local/app-misc/qportage-probe")));
    }
    void invalidPlanRefused()
    {
        Plan bad;
        PlanStep s;
        s.kind = PlanStep::SetUse;
        s.atom = QStringLiteral("not an atom");
        s.values = {QStringLiteral("x")};
        bad.steps = {s};
        PretendRunner *r = runner(QStringLiteral("true"), QStringLiteral("true"), QStringLiteral("scratch3"));
        QVERIFY(!r->start({QStringLiteral("app-misc/jq")}, {}, bad));
        QVERIFY(!r->error().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstPretendRunner)
#include "tst_pretendrunner.moc"
