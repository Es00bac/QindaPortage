// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/executor.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaPortage::Ops;

#define QVERIFY_OPEN(f) \
    do { if (!(f).open(QIODevice::WriteOnly)) qFatal("cannot write %s", qPrintable((f).fileName())); } while (false)

// A fake helper: records its arguments and stdin, then prints the protocol
// lines it was given (docs/helper.md).
class TstExecutor : public QObject
{
    Q_OBJECT

    QTemporaryDir tmp;
    int counter = 0;

    QString fakeHelper(const QString &events, int exitCode = 0)
    {
        const QString path = tmp.filePath(QStringLiteral("helper-%1").arg(counter++));
        QFile f(path);
        QVERIFY_OPEN(f);
        f.write(QStringLiteral("#!/bin/sh\necho \"$*\" > %1/args\ncat > %1/stdin\ncat <<'EOF'\n%2\nEOF\nexit %3\n")
                    .arg(tmp.path(), events).arg(exitCode).toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
    QString read(const char *name) const
    {
        QFile f(tmp.filePath(QString::fromLatin1(name)));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    static Plan plan()
    {
        Plan p;
        p.title = QStringLiteral("t");
        PlanStep s;
        s.kind = PlanStep::Emerge;
        s.values = {QStringLiteral("app-misc/jq")};
        p.steps = {s};
        return p;
    }

private Q_SLOTS:
    void sessionHandlesSeveralTransactions()
    {
        // A fake `serve` helper: one process answers every request line.
        const QString path = tmp.filePath(QStringLiteral("serve-helper"));
        QFile f(path);
        QVERIFY_OPEN(f);
        f.write(QStringLiteral("#!/bin/sh\necho \"$*\" >> %1/serve-args\necho '{\"event\":\"ready\"}'\nn=0\n"
                               "while read -r line; do n=$((n+1)); echo \"$line\" >> %1/requests; "
                               "echo \"{\\\"event\\\":\\\"result\\\",\\\"ok\\\":true,\\\"txn\\\":\\\"t$n\\\",\\\"error\\\":\\\"\\\"}\"; "
                               "echo '{\"event\":\"ready\"}'; done\n")
                    .arg(tmp.path()).toUtf8());
        f.close();
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        Executor ex;
        ex.setCommand({path, {}});
        ex.setSessionIdleSeconds(60);
        QSignalSpy finished(&ex, &Executor::finished);
        QSignalSpy session(&ex, &Executor::sessionChanged);
        QVERIFY(ex.start(plan()));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("t1"));
        QVERIFY(ex.sessionActive());
        QVERIFY(!ex.isRunning());
        QVERIFY(ex.revert(QStringLiteral("20260928-120000-abcd")));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.last().at(1).toString(), QStringLiteral("t2"));
        QCOMPARE(read("serve-args").trimmed(), QStringLiteral("--idle-timeout 60 serve")); // one process
        const QStringList requests = read("requests").trimmed().split(QLatin1Char('\n'));
        QCOMPARE(requests.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(requests.at(0).toUtf8()).object().value(QStringLiteral("command")).toString(),
                 QStringLiteral("apply"));
        QCOMPARE(QJsonDocument::fromJson(requests.at(1).toUtf8()).object().value(QStringLiteral("txn")).toString(),
                 QStringLiteral("20260928-120000-abcd"));
        ex.endSession();
        QTRY_VERIFY_WITH_TIMEOUT(!ex.sessionActive(), 5000);
        QVERIFY(session.size() >= 2);
    }
    void applyStreamsEvents()
    {
        Executor ex;
        ex.setCommand({fakeHelper(QStringLiteral(
            "{\"event\":\"step\",\"index\":0,\"text\":\"emerge app-misc/jq\"}\n"
            "{\"event\":\"log\",\"line\":\">>> Emerging (1 of 1) app-misc/jq-1.8.2\"}\n"
            "not json at all\n"
            "{\"event\":\"result\",\"ok\":true,\"txn\":\"20260928-120000-abcd\",\"error\":\"\"}")),
            {QStringLiteral("--test")}});
        QSignalSpy about(&ex, &Executor::aboutToStart);
        QSignalSpy steps(&ex, &Executor::stepStarted);
        QSignalSpy logs(&ex, &Executor::logLine);
        QSignalSpy done(&ex, &Executor::finished);
        QString error;
        QVERIFY2(ex.start(plan(), &error), qPrintable(error));
        QCOMPARE(about.count(), 1);
        QVERIFY(ex.isRunning());
        QVERIFY(done.wait(10000));
        QCOMPARE(steps.count(), 1);
        QCOMPARE(steps.first().at(1).toString(), QStringLiteral("emerge app-misc/jq"));
        QCOMPARE(logs.count(), 2);
        QCOMPARE(done.first().at(0).toBool(), true);
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("20260928-120000-abcd"));
        QTRY_VERIFY(!ex.isRunning());
        QCOMPARE(read("args").trimmed(), QStringLiteral("--test apply"));
        const QJsonObject sent = QJsonDocument::fromJson(read("stdin").toUtf8()).object();
        QCOMPARE(sent.value(QStringLiteral("title")).toString(), QStringLiteral("t"));
        QVERIFY(!sent.value(QStringLiteral("id")).toString().isEmpty());
    }
    void missingResultIsFailure()
    {
        Executor ex;
        ex.setCommand({fakeHelper(QStringLiteral("{\"event\":\"log\",\"line\":\"x\"}"), 3), {}});
        QSignalSpy done(&ex, &Executor::finished);
        QVERIFY(ex.start(plan()));
        QVERIFY(done.wait(10000));
        QCOMPARE(done.first().at(0).toBool(), false);
        QVERIFY(done.first().at(2).toString().contains(QStringLiteral("exited with 3")));
    }
    void revertAndUndo()
    {
        Executor ex;
        ex.setCommand({fakeHelper(QStringLiteral(
            "{\"event\":\"undo\",\"commands\":[\"emerge --ask --depclean app-misc/jq\"]}\n"
            "{\"event\":\"result\",\"ok\":true,\"txn\":\"20260928-130000-0001\",\"error\":\"\"}")), {}});
        QSignalSpy undo(&ex, &Executor::undoSuggested);
        QSignalSpy done(&ex, &Executor::finished);
        QString error;
        QVERIFY(!ex.revert(QStringLiteral("../../etc"), &error));
        QVERIFY(ex.revert(QStringLiteral("20260928-120000-abcd"), &error));
        QVERIFY(done.wait(10000));
        QCOMPARE(undo.first().at(0).toStringList(), QStringList{QStringLiteral("emerge --ask --depclean app-misc/jq")});
        QCOMPARE(read("args").trimmed(), QStringLiteral("revert 20260928-120000-abcd"));
    }
    void invalidPlanNotStarted()
    {
        Executor ex;
        ex.setCommand({fakeHelper(QString()), {}});
        QSignalSpy about(&ex, &Executor::aboutToStart);
        QString error;
        QVERIFY(!ex.start(Plan(), &error));
        QCOMPARE(about.count(), 0);
    }
    void journalNewestFirst()
    {
        QTemporaryDir dir;
        for (const char *id : {"20260101-000000-0001", "20260202-000000-0002"}) {
            QFile f(dir.filePath(QString::fromLatin1(id) + QStringLiteral(".json")));
            QVERIFY_OPEN(f);
            f.write(QByteArray("{\"id\":\"") + id + "\"}");
        }
        QFile junk(dir.filePath(QStringLiteral("broken.json")));
        QVERIFY_OPEN(junk);
        junk.write("{");
        junk.close();
        const auto entries = Executor::journal(dir.path());
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().value(QStringLiteral("id")).toString(), QStringLiteral("20260202-000000-0002"));
    }
};

QTEST_GUILESS_MAIN(TstExecutor)
#include "tst_executor.moc"
