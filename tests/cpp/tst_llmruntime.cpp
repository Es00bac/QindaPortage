// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent/llmruntime.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace QindaPortage::Agent;

// The "server" is python's http.server serving a directory with a /health
// file, started by a wrapper that understands --port like llama-server.
class TstLlmRuntime : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void choosesByMemory()
    {
        QTemporaryDir dir;
        auto make = [&](const QString &name, qint64 size) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.resize(size));
        };
        const qint64 GiB = 1024LL * 1024 * 1024;
        make(QStringLiteral("Qwen3.5-9B-UD-Q4_K_XL.gguf"), 6 * GiB);
        make(QStringLiteral("Qwen3.5-4B-UD-Q4_K_XL.gguf"), 3 * GiB);
        make(QStringLiteral("mmproj-F16.gguf"), GiB / 2);
        LlmRuntime rt;
        rt.scanModels({dir.path()});
        QCOMPARE(rt.models().size(), 2);
        QCOMPARE(rt.chooseModel(10 * GiB)->id, QStringLiteral("qwen3.5-9b"));
        QCOMPARE(rt.chooseModel(6 * GiB)->id, QStringLiteral("qwen3.5-4b"));
        QCOMPARE(rt.chooseModel(1 * GiB)->id, QStringLiteral("qwen3.5-4b")); // smallest as last resort
        rt.setPreferredModel(QStringLiteral("qwen3.5-9b"));
        QCOMPARE(rt.chooseModel(1 * GiB)->id, QStringLiteral("qwen3.5-9b"));
    }
    void loadsAndUnloads()
    {
        QTemporaryDir dir;
        QFile model(dir.filePath(QStringLiteral("Qwen3.5-4B-test.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        QDir(dir.path()).mkdir(QStringLiteral("www"));
        QFile health(dir.filePath(QStringLiteral("www/health")));
        QVERIFY(health.open(QIODevice::WriteOnly));
        health.write("{\"status\":\"ok\"}");
        health.close();
        const QString wrapper = dir.filePath(QStringLiteral("fake-server"));
        QFile w(wrapper);
        QVERIFY(w.open(QIODevice::WriteOnly));
        w.write("#!/bin/sh\nwhile [ $# -gt 0 ]; do [ \"$1\" = --port ] && port=$2; shift; done\n"
                "exec python3 -m http.server \"$port\" --bind 127.0.0.1 -d \"$(dirname \"$0\")/www\"\n");
        w.close();
        w.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        LlmRuntime rt;
        rt.scanModels({dir.path()});
        rt.setServerCommand(wrapper);
        QSignalSpy ready(&rt, &LlmRuntime::ready);
        rt.ensureLoaded();
        QCOMPARE(rt.stateName(), QStringLiteral("loading"));
        QVERIFY(ready.wait(15000));
        QCOMPARE(rt.stateName(), QStringLiteral("ready"));
        QVERIFY(rt.baseUrl().port() > 0);
        QSignalSpy unloaded(&rt, &LlmRuntime::unloaded);
        rt.unload();
        QVERIFY(unloaded.wait(8000));
        QCOMPARE(rt.stateName(), QStringLiteral("unloaded"));
        QCOMPARE(unloaded.first().first().toString(), QStringLiteral("requested"));
    }
    void idleUnload()
    {
        LlmRuntime rt;
        rt.setIdleSeconds(1);
        QCOMPARE(rt.idleSeconds(), 30); // floor
    }
};

QTEST_GUILESS_MAIN(TstLlmRuntime)
#include "tst_llmruntime.moc"
