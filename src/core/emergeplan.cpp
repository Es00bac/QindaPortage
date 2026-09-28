// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/emergeplan.h"

#include "core/version.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace QindaPortage::Core {

namespace {

// "cat/pf[:slot]::repo" -> parts. Binary build ids ("-1" after the
// version) are split off: a PN cannot end in "-<version>", so "x-1.0-2" can
// only be version 1.0, build 2.
bool splitCpv(const QString &text, QString *key, QString *version, QString *slot, QString *repo, int *buildId)
{
    QString s = text;
    const int repoAt = s.indexOf(QStringLiteral("::"));
    if (repoAt >= 0) {
        if (repo) *repo = s.mid(repoAt + 2);
        s.truncate(repoAt);
    }
    const int slash = s.indexOf(QLatin1Char('/'));
    const int colon = s.indexOf(QLatin1Char(':'), slash);
    if (colon >= 0) {
        if (slot) *slot = s.mid(colon + 1);
        s.truncate(colon);
    }
    if (slash <= 0) return false;
    const QString category = s.left(slash);
    QString pf = s.mid(slash + 1);
    static const QRegularExpression buildRe(QStringLiteral("^(.*)-(\\d+)$"));
    if (const auto m = buildRe.match(pf); m.hasMatch()) {
        QString pn, pv;
        if (Version::splitNameVersion(m.captured(1), &pn, &pv)) {
            if (buildId) *buildId = m.captured(2).toInt();
            pf = m.captured(1);
        }
    }
    QString pn, pv;
    if (!Version::splitNameVersion(pf, &pn, &pv)) return false;
    if (key) *key = category + QLatin1Char('/') + pn;
    if (version) *version = pv;
    return true;
}

QVector<UseFlagState> parseUse(const QString &rest)
{
    QVector<UseFlagState> out;
    static const QRegularExpression groupRe(QStringLiteral("\\b([A-Z0-9_]+)=\"([^\"]*)\""));
    auto it = groupRe.globalMatch(rest);
    while (it.hasNext()) {
        const auto g = it.next();
        const QString var = g.captured(1);
        const QString prefix = var == QLatin1String("USE") ? QString() : var.toLower() + QLatin1Char('_');
        for (QString tok : g.captured(2).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            UseFlagState f;
            if (tok.startsWith(QLatin1Char('(')) && tok.contains(QLatin1Char(')'))) {
                f.locked = true;
                tok.remove(QLatin1Char('('));
                tok.remove(QLatin1Char(')'));
            }
            while (tok.endsWith(QLatin1Char('*')) || tok.endsWith(QLatin1Char('%'))) {
                if (tok.endsWith(QLatin1Char('*'))) f.changed = true;
                else f.added = true;
                tok.chop(1);
            }
            f.enabled = !tok.startsWith(QLatin1Char('-'));
            if (!f.enabled) tok.remove(0, 1);
            f.flag = prefix + tok;
            out.append(f);
        }
    }
    return out;
}

Fix::Kind fixKindForSection(const QString &what)
{
    if (what == QLatin1String("USE")) return Fix::SetUse;
    if (what == QLatin1String("keyword")) return Fix::AcceptKeyword;
    if (what == QLatin1String("license")) return Fix::AcceptLicense;
    if (what == QLatin1String("mask")) return Fix::Unmask;
    return Fix::Other;
}

Problem::Kind problemKindForSection(const QString &what)
{
    if (what == QLatin1String("USE")) return Problem::UseChange;
    if (what == QLatin1String("keyword")) return Problem::KeywordChange;
    if (what == QLatin1String("license")) return Problem::LicenseChange;
    if (what == QLatin1String("mask")) return Problem::MaskChange;
    return Problem::Other;
}

// Splits "atom tokens..." while keeping the version-range atom whole.
QPair<QString, QStringList> atomAndTokens(const QString &line)
{
    QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString atom = parts.isEmpty() ? QString() : parts.takeFirst();
    return {atom, parts};
}

} // namespace

QStringList MergeItem::changedFlags() const
{
    QStringList out;
    for (const UseFlagState &f : use) {
        if (f.changed || f.added) out.append((f.enabled ? QStringLiteral("+") : QStringLiteral("-")) + f.flag);
    }
    return out;
}

QString MergeItem::actionName(Action a)
{
    switch (a) {
    case New: return QStringLiteral("new");
    case NewSlot: return QStringLiteral("new-slot");
    case Upgrade: return QStringLiteral("upgrade");
    case Downgrade: return QStringLiteral("downgrade");
    case Reinstall: return QStringLiteral("reinstall");
    case Rebuild: return QStringLiteral("rebuild");
    case NoMerge: return QStringLiteral("nomerge");
    case Block: return QStringLiteral("block");
    case Uninstall: return QStringLiteral("uninstall");
    }
    return {};
}

QString Fix::kindName(Kind k)
{
    switch (k) {
    case SetUse: return QStringLiteral("use");
    case AcceptKeyword: return QStringLiteral("keyword");
    case AcceptLicense: return QStringLiteral("license");
    case Unmask: return QStringLiteral("unmask");
    case RebuildOrUpdate: return QStringLiteral("rebuild");
    case TryAlternative: return QStringLiteral("alternative");
    case Other: return QStringLiteral("other");
    }
    return {};
}

QString Problem::kindName(Kind k)
{
    switch (k) {
    case SlotConflict: return QStringLiteral("slot-conflict");
    case Blocker: return QStringLiteral("blocker");
    case Masked: return QStringLiteral("masked");
    case RequiredUse: return QStringLiteral("required-use");
    case UseChange: return QStringLiteral("use-change-needed");
    case KeywordChange: return QStringLiteral("keyword-change-needed");
    case LicenseChange: return QStringLiteral("license-change-needed");
    case MaskChange: return QStringLiteral("unmask-needed");
    case NotFound: return QStringLiteral("not-found");
    case Unsatisfied: return QStringLiteral("unsatisfied-dependency");
    case Other: return QStringLiteral("other");
    }
    return {};
}

bool EmergePlan::resolved() const
{
    return problems.isEmpty();
}

QVector<MergeItem> EmergePlan::merges() const
{
    QVector<MergeItem> out;
    for (const MergeItem &i : items) {
        if (i.action != MergeItem::NoMerge && i.action != MergeItem::Block) out.append(i);
    }
    return out;
}

MergeItem EmergePlanParser::parseItemLine(const QString &line, bool *ok)
{
    MergeItem item;
    *ok = false;
    static const QRegularExpression re(QStringLiteral(
        "^\\[(ebuild|binary|nomerge|blocks|uninstall)([^\\]]*)\\](\\s*)(\\S+)(.*)$"));
    const auto m = re.match(line);
    if (!m.hasMatch()) return item;
    item.kind = m.captured(1);
    item.flags = m.captured(2);
    item.depth = std::max<qsizetype>(0, m.captured(3).size() - 1);
    QString rest = m.captured(5);

    if (item.kind == QLatin1String("blocks")) {
        item.action = MergeItem::Block;
        item.key = m.captured(4);
        item.blocker = rest.trimmed();
        item.blockerResolved = !item.flags.contains(QLatin1Char('B'));
        *ok = true;
        return item;
    }
    if (!splitCpv(m.captured(4), &item.key, &item.version, &item.slot, &item.repository, &item.buildId)) {
        return item;
    }
    // "[old-version:slot::repo]" right after the cpv.
    static const QRegularExpression oldRe(QStringLiteral("^\\s*\\[([^\\]]+)\\]"));
    if (const auto o = oldRe.match(rest); o.hasMatch()) {
        QString oldText = o.captured(1);
        QString oldRepo;
        const int r = oldText.indexOf(QStringLiteral("::"));
        if (r >= 0) oldText.truncate(r);
        const int c = oldText.indexOf(QLatin1Char(':'));
        if (c >= 0) {
            item.oldSlot = oldText.mid(c + 1);
            oldText.truncate(c);
        }
        item.oldVersion = oldText;
        rest = rest.mid(o.capturedLength());
    }
    item.use = parseUse(rest);
    static const QRegularExpression sizeRe(QStringLiteral("([\\d,]+) KiB\\s*$"));
    if (const auto s = sizeRe.match(rest); s.hasMatch()) {
        item.downloadKiB = s.captured(1).remove(QLatin1Char(',')).toLongLong();
    }

    const QString f = item.flags;
    item.binary = item.kind == QLatin1String("binary");
    item.keywordMasked = f.contains(QLatin1Char('~')) || f.contains(QLatin1Char('*')) || f.contains(QLatin1Char('#'));
    item.fetchRestricted = f.contains(QLatin1Char('F'));
    item.interactive = f.contains(QLatin1Char('I'));
    if (item.kind == QLatin1String("nomerge")) item.action = MergeItem::NoMerge;
    else if (item.kind == QLatin1String("uninstall")) item.action = MergeItem::Uninstall;
    else if (f.contains(QLatin1Char('D'))) item.action = MergeItem::Downgrade;
    else if (f.contains(QLatin1Char('U'))) item.action = MergeItem::Upgrade;
    else if (f.contains(QLatin1Char('S'))) item.action = MergeItem::NewSlot;
    else if (f.contains(QLatin1Char('N'))) item.action = MergeItem::New;
    else if (f.contains(QLatin1Char('r'))) item.action = MergeItem::Rebuild;
    else if (f.contains(QLatin1Char('R'))) item.action = MergeItem::Reinstall;
    *ok = true;
    return item;
}

QVector<Fix> EmergePlanParser::fixesForRequiredUse(const QString &atom, const QString &constraints,
                                                   const QStringList &enabled)
{
    // AGENT-NOTE: handles the shapes emerge prints as "unsatisfied":
    // "a? ( b )", "a? ( !b )", "a? ( any-of ( b c ) )", "any-of ( b c )",
    // "exactly-one-of ( ... )". Each alternative becomes one candidate fix;
    // the assistant chooses between them.
    QVector<Fix> fixes;
    static const QRegularExpression condRe(QStringLiteral(
        "(!?)([\\w+@.-]+)\\?\\s+\\(\\s+(?:(any-of|exactly-one-of|at-most-one-of|\\|\\||\\^\\^|\\?\\?)\\s+\\(\\s+)?([^()]+?)\\s+\\)"));
    auto it = condRe.globalMatch(constraints);
    while (it.hasNext()) {
        const auto m = it.next();
        const bool negated = !m.captured(1).isEmpty();
        const QString flag = m.captured(2);
        const QStringList wanted = m.captured(4).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // Option 1: drop the condition flag.
        fixes.append({Fix::SetUse, atom, {(negated ? QStringLiteral("+") : QStringLiteral("-")) + flag},
                      QStringLiteral("turn %1 %2 so \"%3\" no longer applies")
                          .arg(flag, negated ? QStringLiteral("on") : QStringLiteral("off"), m.captured(0))});
        // Option 2..n: satisfy the requirement.
        for (const QString &w : wanted) {
            const bool off = w.startsWith(QLatin1Char('!'));
            const QString name = off ? w.mid(1) : w;
            if (!off && enabled.contains(name)) continue;
            fixes.append({Fix::SetUse, atom, {(off ? QStringLiteral("-") : QStringLiteral("+")) + name},
                          QStringLiteral("satisfy \"%1\"").arg(m.captured(0))});
        }
    }
    return fixes;
}

EmergePlan EmergePlanParser::parse(const QString &output)
{
    EmergePlan plan;
    const QStringList lines = output.split(QLatin1Char('\n'));
    static const QRegularExpression sectionRe(QStringLiteral(
        "^The following (USE|keyword|license|mask) changes are necessary to proceed:"));
    static const QRegularExpression notFoundRe(QStringLiteral(
        "^emerge: there are no ebuilds (built with USE flags )?to satisfy \"([^\"]+)\"(?:\\.|)"));
    static const QRegularExpression maskedRe(QStringLiteral(
        "^!!! All ebuilds that could satisfy \"([^\"]+)\" have been masked"));
    static const QRegularExpression maskedLineRe(QStringLiteral(
        "^- (\\S+) \\(masked by: ([^)]+)\\)"));
    static const QRegularExpression changeUseRe(QStringLiteral("^- (\\S+) \\(Change USE: ([^)]+)\\)"));
    static const QRegularExpression unmetRe(QStringLiteral(
        "^!!! The ebuild selected to satisfy \"([^\"]+)\" has unmet requirements"));
    static const QRegularExpression requiredByRe(QStringLiteral(
        "^\\(dependency required by \"([^\"]+)\""));
    static const QRegularExpression slotHeadRe(QStringLiteral("^(\\S+/\\S+:\\S+)$"));
    static const QRegularExpression slotPkgRe(QStringLiteral("^  \\(([^,]+), ([^)]+)\\)"));
    static const QRegularExpression slotReqRe(QStringLiteral("^    (\\S+) required by \\(([^,]+),"));
    static const QRegularExpression causeRe(QStringLiteral("^  \\(([^,]+), [^)]*\\) causes rebuilds for:"));

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        const QString t = line.trimmed();

        if (line.startsWith(QLatin1Char('['))) {
            bool ok = false;
            MergeItem item = parseItemLine(line, &ok);
            if (!ok) continue;
            if (item.action == MergeItem::Block && !item.blockerResolved) {
                Problem p;
                p.kind = Problem::Blocker;
                p.title = QStringLiteral("%1 blocks the request").arg(item.key);
                p.packages = {item.key};
                p.detail = line;
                p.fixes.append({Fix::Other, item.key, {},
                                QStringLiteral("uninstall or update the blocking package first")});
                plan.problems.append(p);
            }
            plan.items.append(item);
            continue;
        }
        if (t.startsWith(QLatin1String("Total: "))) {
            plan.summary = t;
            continue;
        }
        if (const auto m = sectionRe.match(t); m.hasMatch()) {
            const QString what = m.captured(1);
            Problem p;
            p.kind = problemKindForSection(what);
            p.title = QStringLiteral("%1 changes are needed").arg(what);
            QStringList reason;
            for (++i; i < lines.size(); ++i) {
                const QString l = lines.at(i).trimmed();
                if (l.isEmpty()) {
                    if (!p.fixes.isEmpty()) break;
                    continue;
                }
                if (l.startsWith(QLatin1String("(see "))) continue;
                p.detail += l + QLatin1Char('\n');
                if (l.startsWith(QLatin1Char('#'))) {
                    reason.append(l.mid(1).trimmed());
                    continue;
                }
                const auto [atom, tokens] = atomAndTokens(l);
                p.fixes.append({fixKindForSection(what), atom, tokens, reason.join(QStringLiteral("; "))});
                p.packages.append(atom);
                reason.clear();
            }
            plan.problems.append(p);
            continue;
        }
        if (const auto m = maskedRe.match(t); m.hasMatch()) {
            Problem p;
            p.kind = Problem::Masked;
            p.title = QStringLiteral("every version of %1 is masked").arg(m.captured(1));
            p.packages = {m.captured(1)};
            for (++i; i < lines.size(); ++i) {
                const QString l = lines.at(i).trimmed();
                const auto mm = maskedLineRe.match(l);
                if (!mm.hasMatch()) {
                    if (l.startsWith(QLatin1String("For more information"))) break;
                    if (!l.isEmpty() && !l.startsWith(QLatin1String("!!!"))) p.detail += l + QLatin1Char('\n');
                    continue;
                }
                p.detail += l + QLatin1Char('\n');
                QString key, version, slot, repo;
                splitCpv(mm.captured(1), &key, &version, &slot, &repo, nullptr);
                const QString atom = QLatin1Char('=') + key + QLatin1Char('-') + version;
                const QString why = mm.captured(2);
                if (why.contains(QLatin1String("missing keyword"))) {
                    p.fixes.append({Fix::AcceptKeyword, atom, {QStringLiteral("**")}, why});
                } else if (why.contains(QLatin1Char('~'))) {
                    p.fixes.append({Fix::AcceptKeyword, atom, {why.section(QLatin1Char(' '), 0, 0)}, why});
                } else if (why.contains(QLatin1String("package.mask"))) {
                    p.fixes.append({Fix::Unmask, atom, {}, why});
                } else if (why.contains(QLatin1String("license"))) {
                    p.fixes.append({Fix::AcceptLicense, atom, why.section(QLatin1Char(' '), 0, -2)
                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts), why});
                } else {
                    p.fixes.append({Fix::Other, atom, {}, why});
                }
            }
            plan.problems.append(p);
            continue;
        }
        if (const auto m = notFoundRe.match(t); m.hasMatch()) {
            Problem p;
            const bool useBuilt = !m.captured(1).isEmpty();
            p.kind = useBuilt ? Problem::UseChange : Problem::NotFound;
            p.title = useBuilt ? QStringLiteral("%1 needs different USE flags").arg(m.captured(2))
                               : QStringLiteral("nothing provides %1").arg(m.captured(2));
            p.packages = {m.captured(2)};
            p.detail = t + QLatin1Char('\n');
            for (int j = i + 1; j < lines.size() && j < i + 12; ++j) {
                const QString l = lines.at(j).trimmed();
                if (l.startsWith(QLatin1String(" * ")) || l.startsWith(QLatin1String("* IMPORTANT"))) break;
                if (l.isEmpty()) continue;
                p.detail += l + QLatin1Char('\n');
                if (const auto c = changeUseRe.match(l); c.hasMatch()) {
                    QString key, version;
                    splitCpv(c.captured(1), &key, &version, nullptr, nullptr, nullptr);
                    p.fixes.append({Fix::SetUse, QLatin1Char('=') + key + QLatin1Char('-') + version,
                                    c.captured(2).split(QLatin1Char(' '), Qt::SkipEmptyParts), l});
                }
                if (const auto r = requiredByRe.match(l); r.hasMatch()) {
                    p.kind = Problem::Unsatisfied;
                    p.title = QStringLiteral("%1 (needed by %2) is not available").arg(m.captured(2), r.captured(1));
                    p.packages.append(r.captured(1));
                }
                if (l.startsWith(QLatin1String("emerge: Maybe you meant any of these: "))) {
                    QString list = l.mid(38);
                    list.chop(list.endsWith(QLatin1Char('?')) ? 1 : 0);
                    for (const QString &s : list.split(QStringLiteral(", "), Qt::SkipEmptyParts)) {
                        p.fixes.append({Fix::TryAlternative, s.trimmed(), {}, QStringLiteral("similar name")});
                    }
                }
            }
            plan.problems.append(p);
            continue;
        }
        if (const auto m = unmetRe.match(t); m.hasMatch()) {
            Problem p;
            p.kind = Problem::RequiredUse;
            p.packages = {m.captured(1)};
            QString cpvAtom = m.captured(1);
            QStringList enabled;
            QString constraints;
            for (++i; i < lines.size(); ++i) {
                const QString l = lines.at(i).trimmed();
                if (l.startsWith(QLatin1String("* ")) || l.startsWith(QLatin1String("!!!"))) { --i; break; }
                if (l.isEmpty()) continue;
                if (l.startsWith(QLatin1String("- "))) {
                    bool ok = false;
                    const QString cpvText = l.mid(2).section(QLatin1Char(' '), 0, 0);
                    QString key, version;
                    if (splitCpv(cpvText, &key, &version, nullptr, nullptr, nullptr)) {
                        cpvAtom = QLatin1Char('=') + key + QLatin1Char('-') + version;
                    }
                    for (const UseFlagState &f : parseUse(l)) if (f.enabled) enabled.append(f.flag);
                    Q_UNUSED(ok);
                }
                p.detail += l + QLatin1Char('\n');
                if (l.startsWith(QLatin1String("The following REQUIRED_USE flag constraints are unsatisfied:"))
                    && i + 1 < lines.size()) {
                    constraints = lines.at(i + 1).trimmed();
                }
                if (l.startsWith(QLatin1String("The above constraints"))) {
                    ++i; // skip the complete expression
                    break;
                }
            }
            p.title = QStringLiteral("%1: REQUIRED_USE not met (%2)").arg(p.packages.first(), constraints);
            p.fixes = fixesForRequiredUse(cpvAtom, constraints, enabled);
            plan.problems.append(p);
            continue;
        }
        if (t.startsWith(QLatin1String("!!! Multiple package instances within a single package slot"))) {
            Problem p;
            p.kind = Problem::SlotConflict;
            p.title = QStringLiteral("slot conflict");
            for (++i; i < lines.size(); ++i) {
                const QString l = lines.at(i);
                if (l.startsWith(QLatin1String("It may be possible")) || l.startsWith(QLatin1String("For more information"))
                    || l.startsWith(QLatin1String("NOTE: Use the '--verbose-conflicts'"))) {
                    break;
                }
                if (!l.trimmed().isEmpty()) p.detail += l + QLatin1Char('\n');
                if (const auto h = slotHeadRe.match(l.trimmed()); h.hasMatch() && !l.startsWith(QLatin1Char(' '))) {
                    p.packages.append(h.captured(1));
                    p.title = QStringLiteral("slot conflict on %1").arg(h.captured(1));
                }
                if (const auto r = slotReqRe.match(l); r.hasMatch()) {
                    QString key;
                    splitCpv(r.captured(2), &key, nullptr, nullptr, nullptr, nullptr);
                    p.fixes.append({Fix::RebuildOrUpdate, key, {r.captured(1)},
                                    QStringLiteral("%1 requires %2").arg(r.captured(2), r.captured(1))});
                }
            }
            plan.problems.append(p);
            continue;
        }
        if (t == QLatin1String("The following packages are causing rebuilds:")) {
            for (++i; i < lines.size(); ++i) {
                const QString l = lines.at(i);
                if (const auto c = causeRe.match(l); c.hasMatch()) {
                    int count = 0;
                    while (i + 1 < lines.size() && lines.at(i + 1).startsWith(QLatin1String("    ("))) { ++i; ++count; }
                    plan.rebuildCauses.append(QStringLiteral("%1 causes %2 rebuild(s)").arg(c.captured(1)).arg(count));
                } else if (!l.trimmed().isEmpty() && !l.startsWith(QLatin1Char(' '))) {
                    --i;
                    break;
                }
            }
            continue;
        }
    }

    // Counts from the list itself (more reliable than the summary wording).
    for (const MergeItem &it : plan.items) {
        switch (it.action) {
        case MergeItem::New: ++plan.newPackages; break;
        case MergeItem::NewSlot: ++plan.newSlots; break;
        case MergeItem::Upgrade: ++plan.upgrades; break;
        case MergeItem::Downgrade: ++plan.downgrades; break;
        case MergeItem::Reinstall: case MergeItem::Rebuild: ++plan.reinstalls; break;
        case MergeItem::Uninstall: ++plan.uninstalls; break;
        default: break;
        }
        if (it.action != MergeItem::NoMerge && it.action != MergeItem::Block) {
            ++plan.total;
            if (it.binary) ++plan.binaries;
            if (it.downloadKiB > 0) plan.downloadKiB += it.downloadKiB;
        }
    }
    return plan;
}

QJsonObject EmergePlan::toJson(int maxItems) const
{
    QJsonObject o;
    o[QStringLiteral("resolved")] = resolved();
    o[QStringLiteral("summary")] = summary;
    o[QStringLiteral("counts")] = QJsonObject{
        {QStringLiteral("merges"), total}, {QStringLiteral("new"), newPackages},
        {QStringLiteral("newSlot"), newSlots}, {QStringLiteral("upgrade"), upgrades},
        {QStringLiteral("downgrade"), downgrades}, {QStringLiteral("rebuild"), reinstalls},
        {QStringLiteral("binary"), binaries}, {QStringLiteral("downloadKiB"), double(downloadKiB)}};
    QJsonArray merges;
    int n = 0;
    // Most informative first: new, upgrades, downgrades, then rebuilds with USE changes.
    for (int pass = 0; pass < 2; ++pass) {
        for (const MergeItem &it : items) {
            if (it.action == MergeItem::NoMerge || it.action == MergeItem::Block) continue;
            const bool primary = it.action != MergeItem::Reinstall && it.action != MergeItem::Rebuild;
            if ((pass == 0) != primary) continue;
            if (n++ >= maxItems) break;
            QJsonObject m{{QStringLiteral("action"), MergeItem::actionName(it.action)},
                          {QStringLiteral("atom"), it.cpv()},
                          {QStringLiteral("repo"), it.repository},
                          {QStringLiteral("binary"), it.binary}};
            if (!it.oldVersion.isEmpty()) m[QStringLiteral("from")] = it.oldVersion;
            if (it.keywordMasked) m[QStringLiteral("needsKeyword")] = true;
            const QStringList changed = it.changedFlags();
            if (!changed.isEmpty()) m[QStringLiteral("useChanges")] = QJsonArray::fromStringList(changed);
            merges.append(m);
        }
    }
    if (n > maxItems) o[QStringLiteral("truncated")] = n - maxItems;
    o[QStringLiteral("merges")] = merges;
    QJsonArray probs;
    for (const Problem &p : problems) {
        QJsonArray fixes;
        for (const Fix &f : p.fixes) {
            fixes.append(QJsonObject{{QStringLiteral("kind"), Fix::kindName(f.kind)},
                                     {QStringLiteral("atom"), f.atom},
                                     {QStringLiteral("values"), QJsonArray::fromStringList(f.values)},
                                     {QStringLiteral("reason"), f.reason}});
        }
        probs.append(QJsonObject{{QStringLiteral("kind"), Problem::kindName(p.kind)},
                                 {QStringLiteral("title"), p.title},
                                 {QStringLiteral("packages"), QJsonArray::fromStringList(p.packages)},
                                 {QStringLiteral("detail"), p.detail.left(1500)},
                                 {QStringLiteral("fixes"), fixes}});
    }
    o[QStringLiteral("problems")] = probs;
    if (!rebuildCauses.isEmpty()) o[QStringLiteral("rebuildCauses")] = QJsonArray::fromStringList(rebuildCauses);
    return o;
}

} // namespace QindaPortage::Core
