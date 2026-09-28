// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/version.h"

#include <QTest>

using QindaPortage::Core::Version;

class TstVersion : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ordering_data()
    {
        QTest::addColumn<QString>("lower");
        QTest::addColumn<QString>("higher");
        QTest::newRow("numeric") << "1.2" << "1.10";
        QTest::newRow("first component numeric") << "9" << "10";
        QTest::newRow("leading zero string") << "1.01" << "1.1";
        QTest::newRow("trailing zero") << "1.010" << "1.02";
        QTest::newRow("more components") << "1.2" << "1.2.0";
        QTest::newRow("letter") << "1.2" << "1.2a";
        QTest::newRow("rc before release") << "1.0_rc3" << "1.0";
        QTest::newRow("alpha before beta") << "1.0_alpha9" << "1.0_beta1";
        QTest::newRow("release before p") << "1.0" << "1.0_p1";
        QTest::newRow("pre before rc") << "2_pre20260101" << "2_rc1";
        QTest::newRow("revision") << "3.24.5" << "3.24.5-r1";
        QTest::newRow("revision numeric") << "1-r2" << "1-r10";
        QTest::newRow("multi suffix") << "1_alpha_p1" << "1_alpha_p2";
        QTest::newRow("huge number") << "20260927" << "202609270";
    }
    void ordering()
    {
        QFETCH(QString, lower);
        QFETCH(QString, higher);
        const Version a = Version::parse(lower);
        const Version b = Version::parse(higher);
        QVERIFY2(a.isValid() && b.isValid(), qPrintable(lower + QLatin1Char(' ') + higher));
        QVERIFY(Version::compare(a, b) < 0);
        QVERIFY(Version::compare(b, a) > 0);
    }
    void equality()
    {
        QCOMPARE(Version::compare(Version::parse(QStringLiteral("1.0")), Version::parse(QStringLiteral("1.0-r0"))), 0);
        QCOMPARE(Version::compare(Version::parse(QStringLiteral("01.2")), Version::parse(QStringLiteral("1.2"))), 0);
    }
    void invalid()
    {
        QVERIFY(!Version::parse(QStringLiteral("1.0-rc1")).isValid());
        QVERIFY(!Version::parse(QStringLiteral("abc")).isValid());
        QVERIFY(!Version::parse(QStringLiteral("1..2")).isValid());
    }
    void glob()
    {
        const Version prefix = Version::parse(QStringLiteral("3.1"));
        QVERIFY(Version::parse(QStringLiteral("3.1")).matchesGlob(prefix));
        QVERIFY(Version::parse(QStringLiteral("3.1.4")).matchesGlob(prefix));
        QVERIFY(Version::parse(QStringLiteral("3.1_rc1")).matchesGlob(prefix));
        QVERIFY(!Version::parse(QStringLiteral("3.10")).matchesGlob(prefix));
    }
    void split_data()
    {
        QTest::addColumn<QString>("pf");
        QTest::addColumn<QString>("pn");
        QTest::addColumn<QString>("pv");
        QTest::newRow("simple") << "jq-1.8.2" << "jq" << "1.8.2";
        QTest::newRow("plus") << "gtk+-3.24.5-r1" << "gtk+" << "3.24.5-r1";
        QTest::newRow("dashed name") << "font-adobe-100dpi-1.0.4" << "font-adobe-100dpi" << "1.0.4";
        QTest::newRow("digits in name") << "qt6-base-6.9.1_p20260101" << "qt6-base" << "6.9.1_p20260101";
        QTest::newRow("date version") << "ca-certificates-20260601.3.112.5" << "ca-certificates" << "20260601.3.112.5";
    }
    void split()
    {
        QFETCH(QString, pf);
        QFETCH(QString, pn);
        QFETCH(QString, pv);
        QString name, version;
        QVERIFY(Version::splitNameVersion(pf, &name, &version));
        QCOMPARE(name, pn);
        QCOMPARE(version, pv);
    }
};

QTEST_GUILESS_MAIN(TstVersion)
#include "tst_version.moc"
