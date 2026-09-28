// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/buildlog.h"

#include <QTest>

using namespace QindaPortage::Core;

class TstBuildLog : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void missingHeader()
    {
        const BuildFailure f = BuildLog::analyseFile(QStringLiteral(TEST_DATA "/build-missing-header.log"));
        QCOMPARE(f.cpv, QStringLiteral("app-misc/tool-1.0::qinda-local"));
        QCOMPARE(f.phase, QStringLiteral("compile"));
        QCOMPARE(f.kind, QStringLiteral("missing-header"));
        QCOMPARE(f.missing, QStringList{QStringLiteral("nlohmann/json.hpp")});
        QVERIFY(f.errors.join(QLatin1Char('\n')).contains(QStringLiteral("fatal error")));
        QVERIFY(f.tail.last().contains(QStringLiteral("ninja -v")));
    }
    void classifies_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("kind");
        QTest::newRow("lib") << "/usr/bin/ld: cannot find -lfoo: No such file" << "missing-library";
        QTest::newRow("cmake") << "CMake Error at CMakeLists.txt:9 (find_package):\n  Could NOT find ZLIB (missing: ZLIB_LIBRARY)" << "cmake-config";
        QTest::newRow("oom") << "c++: fatal error: Killed signal terminated program cc1plus" << "out-of-memory";
        QTest::newRow("patch") << "Hunk #1 FAILED at 12." << "patch-failed";
        QTest::newRow("py") << "ModuleNotFoundError: No module named 'hatchling'" << "missing-python-module";
        QTest::newRow("undef") << "main.c:(.text+0x1): undefined reference to `png_create_read_struct'" << "undefined-reference";
    }
    void classifies()
    {
        QFETCH(QString, text);
        QFETCH(QString, kind);
        QCOMPARE(BuildLog::analyse(text).kind, kind);
    }
};

QTEST_GUILESS_MAIN(TstBuildLog)
#include "tst_buildlog.moc"
