// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/buildhistory.h"

#include <QTest>

using namespace QindaPortage::Core;

// Fixture tests/data/emerge.log follows the real log format on qinda-top:
// a failed session, an interleaved --jobs session, a sync, a revision
// rebuild, and a merge still open at the end of the log.
class TstBuildHistory : public QObject
{
    Q_OBJECT

    BuildHistory history;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(history.load(QStringLiteral(TEST_DATA "/emerge.log")));
        QCOMPARE(history.records().size(), 7);
    }
    void durationsAndBinaries()
    {
        const auto jq = history.history(QStringLiteral("app-misc/jq"));
        QCOMPARE(jq.size(), 3);
        QCOMPARE(jq.at(0).seconds, 60);
        QVERIFY(jq.at(0).ok && !jq.at(0).binary);
        // Interleaved --jobs session: completions out of start order.
        QCOMPARE(jq.at(1).seconds, 100);
        QCOMPARE(history.history(QStringLiteral("dev-lang/python")).first().seconds, 74);
        QCOMPARE(jq.at(2).cpv, QStringLiteral("app-misc/jq-1.8.2-r1"));
        const auto foo = history.history(QStringLiteral("dev-libs/foo"));
        QVERIFY(foo.first().binary && foo.first().ok);
    }
    void estimates()
    {
        // Source builds 60, 100, 80 -> median 80; binary-only -> unknown.
        QCOMPARE(history.estimateSeconds(QStringLiteral("app-misc/jq")), 80);
        QCOMPARE(history.estimateSeconds(QStringLiteral("dev-libs/foo")), -1);
        QCOMPARE(history.estimateSeconds(QStringLiteral("no/such")), -1);
    }
    void failures()
    {
        const auto failed = history.failures(10);
        QCOMPARE(failed.size(), 2);
        QCOMPARE(failed.at(0).key, QStringLiteral("www-client/firefox"));
        QCOMPARE(failed.at(1).key, QStringLiteral("app-misc/broken"));
        QCOMPARE(failed.at(1).seconds, 20);
        QCOMPARE(history.recent(1).first().key, QStringLiteral("www-client/firefox"));
    }
    void syncs()
    {
        QCOMPARE(history.lastSync(), 1790100070);
        QCOMPARE(history.lastSync(QStringLiteral("gentoo")), 1790100050);
        QCOMPARE(history.lastSync(QStringLiteral("guru")), 0);
    }
    void missingFile()
    {
        BuildHistory h;
        QVERIFY(!h.load(QStringLiteral("/nonexistent/emerge.log")));
        QVERIFY(!h.error().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstBuildHistory)
#include "tst_buildhistory.moc"
