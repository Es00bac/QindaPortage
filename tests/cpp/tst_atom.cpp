// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/atom.h"

#include <QTest>

using namespace QindaPortage::Core;

class TstAtom : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesFullAtom()
    {
        const Atom a = Atom::parse(QStringLiteral(">=dev-libs/openssl-3.0.1-r2:0/3=::gentoo[ssl,-bindist,abi_x86_32(-)?]"));
        QVERIFY2(a.isValid(), qPrintable(a.error()));
        QCOMPARE(a.op(), Atom::GreaterEqual);
        QCOMPARE(a.key(), QStringLiteral("dev-libs/openssl"));
        QCOMPARE(a.version().text(), QStringLiteral("3.0.1-r2"));
        QCOMPARE(a.slot(), QStringLiteral("0"));
        QCOMPARE(a.subSlot(), QStringLiteral("3"));
        QCOMPARE(a.slotOperator(), QLatin1Char('='));
        QCOMPARE(a.repository(), QStringLiteral("gentoo"));
        QCOMPARE(a.useDeps().size(), 3);
        QCOMPARE(a.useDeps().at(1).kind, UseDep::Disabled);
        QCOMPARE(a.useDeps().at(2).kind, UseDep::IfEnabled);
        QCOMPARE(a.useDeps().at(2).missingDefault, QLatin1Char('-'));
    }
    void blockers()
    {
        QCOMPARE(Atom::parse(QStringLiteral("!!<sys-apps/foo-2")).blocker(), Atom::StrongBlock);
        QCOMPARE(Atom::parse(QStringLiteral("!sys-apps/foo")).blocker(), Atom::WeakBlock);
    }
    void slotOperatorOnly()
    {
        const Atom a = Atom::parse(QStringLiteral("dev-libs/icu:="));
        QVERIFY(a.isValid());
        QVERIFY(a.slot().isEmpty());
        QCOMPARE(a.slotOperator(), QLatin1Char('='));
    }
    void rejects()
    {
        QVERIFY(!Atom::parse(QStringLiteral("openssl")).isValid());
        QVERIFY(!Atom::parse(QStringLiteral(">=dev-libs/openssl")).isValid());
        QVERIFY(!Atom::parse(QStringLiteral("~dev-libs/foo-1.0-r1")).isValid());
        QVERIFY(!Atom::parse(QStringLiteral("dev-libs/foo[")).isValid());
    }
    void matching()
    {
        const Version v = Version::parse(QStringLiteral("1.2.3-r1"));
        auto m = [&](const char *atom, const char *slot = "0") {
            return Atom::parse(QString::fromLatin1(atom))
                .matches(QStringLiteral("a"), QStringLiteral("b"), v, QString::fromLatin1(slot), QStringLiteral("gentoo"));
        };
        QVERIFY(m("a/b"));
        QVERIFY(m(">=a/b-1.2"));
        QVERIFY(!m("<a/b-1.2.3"));
        QVERIFY(m("~a/b-1.2.3"));
        QVERIFY(m("=a/b-1.2*"));
        QVERIFY(!m("=a/b-1.2.3"));
        QVERIFY(m("a/b:0"));
        QVERIFY(!m("a/b:1"));
        QVERIFY(!m("a/b::guru"));
        QVERIFY(m("a/b:0/2", "0/2"));
        QVERIFY(!m("a/b:0/2", "0/3"));
    }
    void useMatching()
    {
        const Atom a = Atom::parse(QStringLiteral("x/y[foo,-bar,baz?,qux=,missing(+)]"));
        const QSet<QString> iuse{QStringLiteral("foo"), QStringLiteral("bar"), QStringLiteral("baz"), QStringLiteral("qux")};
        QVERIFY(a.matchesUse({QStringLiteral("foo"), QStringLiteral("baz")}, iuse, {QStringLiteral("baz")}));
        QVERIFY(!a.matchesUse({QStringLiteral("foo")}, iuse, {QStringLiteral("baz")}));
        QVERIFY(!a.matchesUse({QStringLiteral("foo"), QStringLiteral("bar")}, iuse, {}));
        QVERIFY(!a.matchesUse({QStringLiteral("foo")}, iuse, {QStringLiteral("qux")}));
    }
};

QTEST_GUILESS_MAIN(TstAtom)
#include "tst_atom.moc"
