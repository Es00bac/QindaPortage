// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/plan.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

using namespace QindaPortage::Ops;

static PlanStep step(PlanStep::Kind kind, const QString &atom, const QStringList &values = {},
                     const QString &path = {}, const QString &content = {}, const QStringList &options = {})
{
    PlanStep s;
    s.kind = kind;
    s.atom = atom;
    s.values = values;
    s.path = path;
    s.content = content;
    s.options = options;
    return s;
}

// AGENT-CONTRACT: the rejection cases mirror helper/tests/test_apply.py
// (test_rejections); both validators must refuse the same inputs.
class TstPlan : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip()
    {
        Plan p;
        p.id = QStringLiteral("20260928-120000-abcd");
        p.title = QStringLiteral("jq static");
        p.goal = QStringLiteral("I want a static jq");
        p.created = QDateTime(QDate(2026, 9, 28), QTime(12, 0), QTimeZone::UTC);
        PlanStep use = step(PlanStep::SetUse, QStringLiteral("app-misc/jq"), {QStringLiteral("+static-libs")});
        use.rationale = QStringLiteral("static binary for rescue use");
        p.steps = {use, step(PlanStep::Emerge, {}, {QStringLiteral("app-misc/jq")}, {}, {},
                             {QStringLiteral("--oneshot")})};
        QString error;
        const Plan back = Plan::fromJson(p.toJson(), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(back.id, p.id);
        QCOMPARE(back.created, p.created);
        QCOMPARE(back.steps.size(), 2);
        QCOMPARE(back.steps.at(0).kind, PlanStep::SetUse);
        QCOMPARE(back.steps.at(0).rationale, use.rationale);
        QCOMPARE(back.steps.at(1).options, QStringList{QStringLiteral("--oneshot")});
        QCOMPARE(p.toJson().value(QStringLiteral("steps")).toArray().at(0).toObject()
                     .value(QStringLiteral("kind")).toString(), QStringLiteral("use"));
        QVERIFY(back.validate(&error));
    }
    void unknownKindReported()
    {
        QString error;
        const QJsonObject o{{QStringLiteral("steps"), QJsonArray{QJsonObject{{QStringLiteral("kind"), QStringLiteral("shell")}}}}};
        Plan::fromJson(o, &error);
        QVERIFY(error.contains(QStringLiteral("unknown kind")));
    }
    void fragmentsHouseStyle()
    {
        Plan p;
        PlanStep a = step(PlanStep::SetUse, QStringLiteral("app-misc/jq"), {QStringLiteral("+static-libs"), QStringLiteral("-test")});
        a.rationale = QStringLiteral("static\nbinary");
        PlanStep b = step(PlanStep::AcceptKeyword, QStringLiteral("=app-misc/newtool-0.9"), {QStringLiteral("~amd64")});
        p.steps = {a, b, step(PlanStep::Emerge, {}, {QStringLiteral("app-misc/jq")})};
        const auto frags = p.configFragments(QDate(2026, 9, 28));
        QCOMPARE(frags.size(), 2);
        QCOMPARE(frags.value(QStringLiteral("package.use")),
                 QStringLiteral("\n# 2026-09-28 — static binary\napp-misc/jq static-libs -test\n"));
        QCOMPARE(frags.value(QStringLiteral("package.accept_keywords")),
                 QStringLiteral("\n# 2026-09-28 — QindaPortage plan\n=app-misc/newtool-0.9 ~amd64\n"));
        QCOMPARE(p.emergeTargets(), QStringList{QStringLiteral("app-misc/jq")});
    }
    void rejects_data()
    {
        QTest::addColumn<int>("index");
        const char *names[] = {"shell in atom", "command substitution flag", "use dep in package.use",
                               "path escape", "ebuild for other package", "patch in subdir", "ask option",
                               "jobs option", "option as target", "sync path", "absolute overlay path",
                               "blocker target", "empty use", "bad keyword"};
        for (int i = 0; i < int(std::size(names)); ++i) QTest::newRow(names[i]) << i;
    }
    void rejects()
    {
        QFETCH(int, index);
        const QVector<PlanStep> bad = {
            step(PlanStep::SetUse, QStringLiteral("app-misc/x; rm -rf /"), {QStringLiteral("a")}),
            step(PlanStep::SetUse, QStringLiteral("app-misc/x"), {QStringLiteral("$(id)")}),
            step(PlanStep::SetUse, QStringLiteral("app-misc/x[foo]"), {QStringLiteral("a")}),
            step(PlanStep::WriteEbuild, {}, {}, QStringLiteral("../../etc/shadow"), QStringLiteral("x")),
            step(PlanStep::WriteEbuild, {}, {}, QStringLiteral("app-misc/x/other-1.0.ebuild"), QStringLiteral("x")),
            step(PlanStep::AddPatch, QStringLiteral("app-misc/x"), {}, QStringLiteral("sub/x.patch"), QStringLiteral("x")),
            step(PlanStep::Emerge, {}, {QStringLiteral("app-misc/x")}, {}, {}, {QStringLiteral("--ask")}),
            step(PlanStep::Emerge, {}, {QStringLiteral("app-misc/x")}, {}, {}, {QStringLiteral("--jobs=64")}),
            step(PlanStep::Emerge, {}, {QStringLiteral("--nodeps")}),
            step(PlanStep::Sync, {}, {QStringLiteral("../gentoo")}),
            step(PlanStep::WriteEbuild, {}, {}, QStringLiteral("/etc/x/y"), QStringLiteral("x")),
            step(PlanStep::Emerge, {}, {QStringLiteral("!app-misc/x")}),
            step(PlanStep::SetUse, QStringLiteral("app-misc/x"), {}),
            step(PlanStep::AcceptKeyword, QStringLiteral("app-misc/x"), {QStringLiteral("~AMD64 x")}),
        };
        const PlanStep &s = bad.at(index);
        QVERIFY2(!s.validate().isEmpty(), qPrintable(s.describe()));
    }
    void accepts()
    {
        const QVector<PlanStep> good = {
            step(PlanStep::WriteEbuild, {}, {}, QStringLiteral("app-misc/newthing/newthing-0.1.ebuild"), QStringLiteral("EAPI=8\n")),
            step(PlanStep::WriteEbuild, {}, {}, QStringLiteral("app-misc/newthing/files/fix-build.patch"), QStringLiteral("--- a\n")),
            step(PlanStep::WriteEbuild, {}, {}, QStringLiteral("app-misc/newthing/metadata.xml"), QStringLiteral("<x/>")),
            step(PlanStep::AddPatch, QStringLiteral("=app-misc/tool-2.0"), {}, QStringLiteral("gcc15.patch"), QStringLiteral("---")),
            step(PlanStep::Emerge, {}, {QStringLiteral("@world")}, {}, {},
                 {QStringLiteral("--update"), QStringLiteral("--deep"), QStringLiteral("--with-bdeps=y"),
                  QStringLiteral("--exclude=dev-lang/rust")}),
            step(PlanStep::AcceptKeyword, QStringLiteral("=dev-vcs/git-9999"), {QStringLiteral("**")}),
            step(PlanStep::AcceptLicense, QStringLiteral("app-misc/x"), {QStringLiteral("@BINARY-REDISTRIBUTABLE")}),
            step(PlanStep::Depclean, {}),
            step(PlanStep::Sync, {}, {QStringLiteral("gentoo")}),
        };
        for (const PlanStep &s : good) {
            QVERIFY2(s.validate().isEmpty(), qPrintable(s.describe() + QLatin1String(": ") + s.validate()));
        }
    }
    void emptyPlanInvalid()
    {
        QString error;
        QVERIFY(!Plan().validate(&error));
        QVERIFY(!error.isEmpty());
    }
    void newIdShape()
    {
        QVERIFY(QRegularExpression(QStringLiteral("^[0-9]{8}-[0-9]{6}-[0-9a-f]{4}$")).match(Plan::newId()).hasMatch());
    }
};

QTEST_GUILESS_MAIN(TstPlan)
#include "tst_plan.moc"
