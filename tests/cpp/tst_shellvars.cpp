// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/shellvars.h"

#include <QTest>

using QindaPortage::Core::ShellVars;

class TstShellVars : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void assignments()
    {
        const ShellVars v = ShellVars::parse(QStringLiteral(
            "# comment\n"
            "COMMON_FLAGS=\"-O2 -pipe\"\n"
            "CFLAGS=\"${COMMON_FLAGS} -march=native\"\n"
            "USE=\"X a52\n"
            "  -gnome \\\n"
            "  wayland\"\n"
            "MAKEOPTS='-j32 -l16'\n"
            "export LC_MESSAGES=C # trailing\n"
            "LITERAL='$HOME'\n"));
        QCOMPARE(v.value(QStringLiteral("CFLAGS")), QStringLiteral("-O2 -pipe -march=native"));
        QCOMPARE(v.value(QStringLiteral("USE")), QStringLiteral("X a52 -gnome wayland"));
        QCOMPARE(v.value(QStringLiteral("MAKEOPTS")), QStringLiteral("-j32 -l16"));
        QCOMPARE(v.value(QStringLiteral("LC_MESSAGES")), QStringLiteral("C"));
        QCOMPARE(v.value(QStringLiteral("LITERAL")), QStringLiteral("$HOME"));
        QCOMPARE(v.lineOf(QStringLiteral("USE")), 4);
        QCOMPARE(v.lineOf(QStringLiteral("MAKEOPTS")), 7);
    }
};

QTEST_GUILESS_MAIN(TstShellVars)
#include "tst_shellvars.moc"
