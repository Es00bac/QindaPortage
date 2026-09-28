// SPDX-License-Identifier: GPL-3.0-or-later
#include "ops/plan.h"

#include "core/atom.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace QindaPortage::Ops {

using Core::Atom;

namespace Validate {

// AGENT-CONTRACT: every pattern and allowlist here has a twin in
// helper/qportage_validate.py; the helper must never accept what this
// rejects, and tst_plan / helper/tests/test_apply.py cover both.

bool isAtom(const QString &text, QString *why)
{
    const Atom a = Atom::parse(text);
    if (!a.isValid()) {
        if (why) *why = a.error();
        return false;
    }
    if (a.blocker() != Atom::NoBlock) {
        if (why) *why = QStringLiteral("blockers are not allowed here");
        return false;
    }
    if (text.contains(QRegularExpression(QStringLiteral("\\s")))) {
        if (why) *why = QStringLiteral("whitespace in atom");
        return false;
    }
    return true;
}

bool isUseFlagToken(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^[+-]?[A-Za-z0-9][A-Za-z0-9+_@-]*$"));
    return re.match(text).hasMatch();
}

bool isKeyword(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^(~?[a-z0-9][a-z0-9-]*|\\*\\*|~\\*|\\*)$"));
    return re.match(text).hasMatch();
}

bool isLicense(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^(-?@?[A-Za-z0-9][A-Za-z0-9+_.-]*|\\*|-\\*)$"));
    return re.match(text).hasMatch();
}

bool isRepoName(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_-]*$"));
    return re.match(text).hasMatch();
}

bool isSetName(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^@[A-Za-z0-9_][A-Za-z0-9_.-]*$"));
    return re.match(text).hasMatch();
}

bool isSafeRelativePath(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_+][A-Za-z0-9._+-]*(/[A-Za-z0-9_+][A-Za-z0-9._+-]*)*$"));
    if (!re.match(path).hasMatch()) return false;
    for (const QString &part : path.split(QLatin1Char('/'))) {
        if (part == QLatin1String(".") || part == QLatin1String("..")) return false;
    }
    return true;
}

bool isAllowedEmergeOption(const QString &option)
{
    // AGENT-GUARD: no --jobs/--load-average (the operator's parallelism
    // applies), no --ask (the helper is not interactive), no --nodeps.
    static const QStringList flags = {
        QStringLiteral("--oneshot"), QStringLiteral("--update"), QStringLiteral("--deep"),
        QStringLiteral("--newuse"), QStringLiteral("--changed-use"), QStringLiteral("--noreplace"),
        QStringLiteral("--usepkg"), QStringLiteral("--usepkgonly"), QStringLiteral("--getbinpkg"),
        QStringLiteral("--getbinpkgonly"), QStringLiteral("--buildpkg"), QStringLiteral("--keep-going"),
        QStringLiteral("--verbose"), QStringLiteral("--changed-deps"), QStringLiteral("--changed-slot"),
        QStringLiteral("--complete-graph"), QStringLiteral("--emptytree"), QStringLiteral("--fetchonly")};
    static const QStringList yesNo = {
        QStringLiteral("--with-bdeps"), QStringLiteral("--select"), QStringLiteral("--keep-going"),
        QStringLiteral("--rebuild-if-new-slot"), QStringLiteral("--binpkg-respect-use"),
        QStringLiteral("--changed-deps"), QStringLiteral("--usepkg"), QStringLiteral("--getbinpkg"),
        QStringLiteral("--buildpkg")};
    if (flags.contains(option)) return true;
    const int eq = option.indexOf(QLatin1Char('='));
    if (eq < 0) return false;
    const QString name = option.left(eq);
    const QString value = option.mid(eq + 1);
    if (yesNo.contains(name)) return value == QLatin1String("y") || value == QLatin1String("n");
    if (name == QLatin1String("--backtrack")) {
        bool ok = false;
        const int n = value.toInt(&ok);
        return ok && n >= 0 && n <= 1000;
    }
    if (name == QLatin1String("--exclude") || name == QLatin1String("--usepkg-exclude")
        || name == QLatin1String("--useoldpkg-atoms")) {
        return isAtom(value);
    }
    return false;
}

QString sanitizeRationale(const QString &text)
{
    QString s = text.simplified();
    if (s.size() > 240) s = s.left(237) + QStringLiteral("...");
    return s;
}

} // namespace Validate

QString PlanStep::kindName(Kind kind)
{
    switch (kind) {
    case SetUse: return QStringLiteral("use");
    case AcceptKeyword: return QStringLiteral("keyword");
    case AcceptLicense: return QStringLiteral("license");
    case Unmask: return QStringLiteral("unmask");
    case Mask: return QStringLiteral("mask");
    case AddPatch: return QStringLiteral("patch");
    case WriteEbuild: return QStringLiteral("ebuild");
    case Emerge: return QStringLiteral("emerge");
    case Unmerge: return QStringLiteral("unmerge");
    case Depclean: return QStringLiteral("depclean");
    case Sync: return QStringLiteral("sync");
    }
    return {};
}

bool PlanStep::kindFromName(const QString &name, Kind *kind)
{
    for (int k = SetUse; k <= Sync; ++k) {
        if (kindName(Kind(k)) == name) {
            *kind = Kind(k);
            return true;
        }
    }
    return false;
}

QJsonObject PlanStep::toJson() const
{
    QJsonObject o{{QStringLiteral("kind"), kindName(kind)}};
    if (!atom.isEmpty()) o[QStringLiteral("atom")] = atom;
    if (!values.isEmpty()) o[QStringLiteral("values")] = QJsonArray::fromStringList(values);
    if (!rationale.isEmpty()) o[QStringLiteral("rationale")] = rationale;
    if (!path.isEmpty()) o[QStringLiteral("path")] = path;
    if (!content.isEmpty()) o[QStringLiteral("content")] = content;
    if (!options.isEmpty()) o[QStringLiteral("options")] = QJsonArray::fromStringList(options);
    return o;
}

static QStringList stringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &item : v.toArray()) out.append(item.toString());
    return out;
}

PlanStep PlanStep::fromJson(const QJsonObject &o, bool *ok)
{
    PlanStep s;
    *ok = kindFromName(o.value(QStringLiteral("kind")).toString(), &s.kind);
    s.atom = o.value(QStringLiteral("atom")).toString();
    s.values = stringList(o.value(QStringLiteral("values")));
    s.rationale = o.value(QStringLiteral("rationale")).toString();
    s.path = o.value(QStringLiteral("path")).toString();
    s.content = o.value(QStringLiteral("content")).toString();
    s.options = stringList(o.value(QStringLiteral("options")));
    return s;
}

QString PlanStep::validate() const
{
    using namespace Validate;
    QString why;
    const bool needsAtom = kind <= AddPatch;
    if (needsAtom && !isAtom(atom, &why)) return QStringLiteral("bad atom '%1': %2").arg(atom, why);
    if (kind <= Mask && !Atom::parse(atom).useDeps().isEmpty()) {
        return QStringLiteral("package.* entries cannot carry [USE] dependencies");
    }
    if (content.size() > 1024 * 1024) return QStringLiteral("content larger than 1 MiB");

    switch (kind) {
    case SetUse:
        if (values.isEmpty()) return QStringLiteral("no USE flags");
        for (const QString &v : values) if (!isUseFlagToken(v)) return QStringLiteral("bad USE flag '%1'").arg(v);
        break;
    case AcceptKeyword:
        if (values.isEmpty()) return QStringLiteral("no keywords");
        for (const QString &v : values) if (!isKeyword(v)) return QStringLiteral("bad keyword '%1'").arg(v);
        break;
    case AcceptLicense:
        if (values.isEmpty()) return QStringLiteral("no licenses");
        for (const QString &v : values) if (!isLicense(v)) return QStringLiteral("bad license '%1'").arg(v);
        break;
    case Unmask:
    case Mask:
        if (!values.isEmpty()) return QStringLiteral("mask entries take no values");
        break;
    case AddPatch: {
        const Atom a = Atom::parse(atom);
        if (a.op() != Atom::None && a.op() != Atom::Equal) return QStringLiteral("patch atom must be cat/pn or =cat/pn-ver");
        if (!a.slot().isEmpty() || !a.repository().isEmpty() || !a.useDeps().isEmpty()) {
            return QStringLiteral("patch atom must not carry slot, repository or USE");
        }
        if (!isSafeRelativePath(path) || path.contains(QLatin1Char('/'))) return QStringLiteral("bad patch file name '%1'").arg(path);
        if (!path.endsWith(QLatin1String(".patch")) && !path.endsWith(QLatin1String(".diff"))) {
            return QStringLiteral("patch file must end in .patch or .diff");
        }
        if (content.isEmpty()) return QStringLiteral("empty patch");
        break;
    }
    case WriteEbuild: {
        if (!isSafeRelativePath(path)) return QStringLiteral("bad overlay path '%1'").arg(path);
        const QStringList parts = path.split(QLatin1Char('/'));
        if (parts.size() < 3) return QStringLiteral("overlay path must be cat/pn/<file>");
        if (!Atom::isValidKey(parts.at(0) + QLatin1Char('/') + parts.at(1))) {
            return QStringLiteral("bad category/package in '%1'").arg(path);
        }
        const QString file = parts.at(2);
        const QString pn = parts.at(1);
        if (parts.size() == 3) {
            const bool ebuild = file.startsWith(pn + QLatin1Char('-')) && file.endsWith(QLatin1String(".ebuild"))
                && isAtom(QLatin1Char('=') + parts.at(0) + QLatin1Char('/') + file.chopped(7));
            if (!ebuild && file != QLatin1String("metadata.xml")) {
                return QStringLiteral("'%1' is neither pn-ver.ebuild nor metadata.xml").arg(file);
            }
        } else if (file != QLatin1String("files")) {
            return QStringLiteral("only files/ may hold extra overlay files");
        }
        if (content.isEmpty()) return QStringLiteral("empty file");
        break;
    }
    case Emerge:
    case Unmerge:
    case Depclean:
        if (kind != Depclean && values.isEmpty()) return QStringLiteral("no packages");
        for (const QString &v : values) {
            if (kind == Emerge && isSetName(v)) continue;
            if (!isAtom(v, &why)) return QStringLiteral("bad target '%1': %2").arg(v, why);
        }
        for (const QString &opt : options) {
            if (kind != Emerge || !isAllowedEmergeOption(opt)) return QStringLiteral("option '%1' is not allowed").arg(opt);
        }
        break;
    case Sync:
        for (const QString &v : values) if (!isRepoName(v)) return QStringLiteral("bad repository '%1'").arg(v);
        if (!options.isEmpty()) return QStringLiteral("sync takes no options");
        break;
    }
    return {};
}

QString PlanStep::configFile() const
{
    switch (kind) {
    case SetUse: return QStringLiteral("package.use");
    case AcceptKeyword: return QStringLiteral("package.accept_keywords");
    case AcceptLicense: return QStringLiteral("package.license");
    case Unmask: return QStringLiteral("package.unmask");
    case Mask: return QStringLiteral("package.mask");
    default: return {};
    }
}

QString PlanStep::configLine() const
{
    QStringList tokens{atom};
    for (QString v : values) {
        // package.use takes "flag" and "-flag"; "+flag" is the GUI's spelling.
        if (kind == SetUse && v.startsWith(QLatin1Char('+'))) v.remove(0, 1);
        tokens.append(v);
    }
    return tokens.join(QLatin1Char(' '));
}

QString PlanStep::describe() const
{
    const QString j = values.join(QLatin1Char(' '));
    switch (kind) {
    case SetUse: return QStringLiteral("Set USE %1 on %2 (package.use)").arg(j, atom);
    case AcceptKeyword: return QStringLiteral("Accept %1 for %2 (package.accept_keywords)").arg(j, atom);
    case AcceptLicense: return QStringLiteral("Accept license %1 for %2 (package.license)").arg(j, atom);
    case Unmask: return QStringLiteral("Unmask %1 (package.unmask)").arg(atom);
    case Mask: return QStringLiteral("Mask %1 (package.mask)").arg(atom);
    case AddPatch: return QStringLiteral("Add user patch %1 for %2 (/etc/portage/patches)").arg(path, atom);
    case WriteEbuild: return QStringLiteral("Write %1 in the qinda-local overlay").arg(path);
    case Emerge: return QStringLiteral("emerge %1%2").arg(options.isEmpty() ? QString() : options.join(QLatin1Char(' ')) + QLatin1Char(' '), j);
    case Unmerge: return QStringLiteral("Remove %1 (deselect, then depclean: refuses if still needed)").arg(j);
    case Depclean: return j.isEmpty() ? QStringLiteral("Remove packages nothing needs (depclean)")
                                      : QStringLiteral("depclean %1").arg(j);
    case Sync: return j.isEmpty() ? QStringLiteral("Sync all repositories") : QStringLiteral("Sync %1").arg(j);
    }
    return {};
}

QString Plan::newId()
{
    // "20260928-110512-a3f9": sortable, unique enough for one machine.
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-"))
        + QString::number(QRandomGenerator::global()->bounded(0x10000), 16).rightJustified(4, QLatin1Char('0'));
}

Plan Plan::fromJson(const QJsonObject &o, QString *error)
{
    Plan p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.title = o.value(QStringLiteral("title")).toString();
    p.goal = o.value(QStringLiteral("goal")).toString();
    p.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
    const QJsonArray steps = o.value(QStringLiteral("steps")).toArray();
    for (int i = 0; i < steps.size(); ++i) {
        bool ok = false;
        p.steps.append(PlanStep::fromJson(steps.at(i).toObject(), &ok));
        if (!ok && error && error->isEmpty()) *error = QStringLiteral("step %1: unknown kind").arg(i + 1);
    }
    return p;
}

QJsonObject Plan::toJson() const
{
    QJsonArray steps;
    for (const PlanStep &s : this->steps) steps.append(s.toJson());
    return QJsonObject{{QStringLiteral("version"), 1},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("goal"), goal},
                       {QStringLiteral("created"), created.toString(Qt::ISODate)},
                       {QStringLiteral("steps"), steps}};
}

QByteArray Plan::toJsonBytes() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
}

bool Plan::validate(QString *error) const
{
    if (steps.isEmpty()) {
        if (error) *error = QStringLiteral("the plan has no steps");
        return false;
    }
    for (int i = 0; i < steps.size(); ++i) {
        const QString why = steps.at(i).validate();
        if (!why.isEmpty()) {
            if (error) *error = QStringLiteral("step %1: %2").arg(i + 1).arg(why);
            return false;
        }
    }
    return true;
}

QStringList Plan::describe() const
{
    QStringList out;
    for (const PlanStep &s : steps) out.append(s.describe());
    return out;
}

QMap<QString, QString> Plan::configFragments(const QDate &date) const
{
    QMap<QString, QString> out;
    for (const PlanStep &s : steps) {
        const QString file = s.configFile();
        if (file.isEmpty()) continue;
        QString &text = out[file];
        const QString why = Validate::sanitizeRationale(s.rationale);
        text += QStringLiteral("\n# %1 — %2\n%3\n")
                    .arg(date.toString(Qt::ISODate), why.isEmpty() ? QStringLiteral("QindaPortage plan") : why,
                         s.configLine());
    }
    return out;
}

QStringList Plan::emergeTargets() const
{
    QStringList out;
    for (const PlanStep &s : steps) {
        if (s.kind != PlanStep::Emerge) continue;
        for (const QString &v : s.values) if (!out.contains(v)) out.append(v);
    }
    return out;
}

QStringList Plan::emergeOptions() const
{
    QStringList out;
    for (const PlanStep &s : steps) {
        if (s.kind != PlanStep::Emerge) continue;
        for (const QString &v : s.options) if (!out.contains(v)) out.append(v);
    }
    return out;
}

bool Plan::writesEbuilds() const
{
    for (const PlanStep &s : steps) if (s.kind == PlanStep::WriteEbuild) return true;
    return false;
}

} // namespace QindaPortage::Ops
