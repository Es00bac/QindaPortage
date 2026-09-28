// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/depspec.h"

#include <QTest>

using namespace QindaPortage::Core;

static QStringList flat(const QVector<DepRequirement> &reqs)
{
    QStringList out;
    for (const auto &r : reqs) {
        QStringList alts;
        for (const auto &alt : r.alternatives) {
            QStringList atoms;
            for (const Atom &a : alt) atoms.append(a.text());
            alts.append(atoms.join(QLatin1Char('+')));
        }
        out.append(alts.join(QLatin1Char('|')));
    }
    return out;
}

class TstDepSpec : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void conditionalsAndGroups()
    {
        QString error;
        const DepNode root = DepSpec::parse(QStringLiteral(
            "a/b ssl? ( dev-libs/openssl:= ) !ssl? ( net-libs/gnutls ) "
            "|| ( x/one ( x/two x/three ) ) gui? ( || ( q/t5 q/t6 ) )"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(flat(DepSpec::evaluate(root, {QStringLiteral("ssl")})),
                 (QStringList{QStringLiteral("a/b"), QStringLiteral("dev-libs/openssl:="),
                              QStringLiteral("x/one|x/two+x/three")}));
        QCOMPARE(flat(DepSpec::evaluate(root, {QStringLiteral("gui")})),
                 (QStringList{QStringLiteral("a/b"), QStringLiteral("net-libs/gnutls"),
                              QStringLiteral("x/one|x/two+x/three"), QStringLiteral("q/t5|q/t6")}));
    }
    void anyOfWithFalseConditional()
    {
        const DepNode root = DepSpec::parse(QStringLiteral("|| ( foo? ( a/a ) b/b )"));
        QCOMPARE(flat(DepSpec::evaluate(root, {})), QStringList{QStringLiteral("b/b")});
    }
    void conditions()
    {
        const DepNode root = DepSpec::parse(QStringLiteral("x? ( !y? ( a/a ) ) b/b"));
        const auto list = DepSpec::atomsWithConditions(root);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).second, (QStringList{QStringLiteral("x"), QStringLiteral("!y")}));
        QVERIFY(list.at(1).second.isEmpty());
    }
    void errors()
    {
        QString error;
        DepSpec::parse(QStringLiteral("|| a/b"), &error);
        QVERIFY(!error.isEmpty());
        DepSpec::parse(QStringLiteral("x? ( a/b"), &error);
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstDepSpec)
#include "tst_depspec.moc"
