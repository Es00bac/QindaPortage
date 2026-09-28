// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/atom.h"

#include <QRegularExpression>

namespace QindaPortage::Core {

namespace {

const QRegularExpression &categoryPattern()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9+_.-]*$"));
    return re;
}

const QRegularExpression &namePattern()
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9+_-]*$"));
    return re;
}

bool parseUseDeps(const QString &body, QVector<UseDep> *out)
{
    // AGENT-NOTE: "flag(+)=" style defaults sit between the name and the
    // conditional suffix; PMS §8.3.4.
    static const QRegularExpression re(QStringLiteral(
        "^(!|-)?([A-Za-z0-9][A-Za-z0-9+_@-]*)(?:\\(([+-])\\))?([=?])?$"));
    for (const QString &raw : body.split(QLatin1Char(','))) {
        const auto m = re.match(raw.trimmed());
        if (!m.hasMatch()) return false;
        UseDep dep;
        const QString prefix = m.captured(1);
        const QString suffix = m.captured(4);
        dep.flag = m.captured(2);
        if (!m.captured(3).isEmpty()) dep.missingDefault = m.captured(3).at(0);
        if (suffix == QLatin1String("=")) {
            if (prefix == QLatin1String("-")) return false;
            dep.kind = prefix == QLatin1String("!") ? UseDep::NotEqual : UseDep::Equal;
        } else if (suffix == QLatin1String("?")) {
            if (prefix == QLatin1String("-")) return false;
            dep.kind = prefix == QLatin1String("!") ? UseDep::IfDisabled : UseDep::IfEnabled;
        } else {
            if (prefix == QLatin1String("!")) return false;
            dep.kind = prefix == QLatin1String("-") ? UseDep::Disabled : UseDep::Enabled;
        }
        out->append(dep);
    }
    return true;
}

} // namespace

bool Atom::isValidKey(const QString &key)
{
    const int slash = key.indexOf(QLatin1Char('/'));
    if (slash <= 0 || key.indexOf(QLatin1Char('/'), slash + 1) >= 0) return false;
    return categoryPattern().match(key.left(slash)).hasMatch()
        && namePattern().match(key.mid(slash + 1)).hasMatch();
}

Atom Atom::parse(const QString &input)
{
    Atom a;
    a.m_text = input.trimmed();
    QString s = a.m_text;
    auto fail = [&a](const QString &why) {
        a.m_error = why;
        a.m_valid = false;
        return a;
    };
    if (s.isEmpty()) return fail(QStringLiteral("empty atom"));

    if (s.startsWith(QStringLiteral("!!"))) {
        a.m_blocker = StrongBlock;
        s.remove(0, 2);
    } else if (s.startsWith(QLatin1Char('!'))) {
        a.m_blocker = WeakBlock;
        s.remove(0, 1);
    }

    // USE deps.
    if (s.endsWith(QLatin1Char(']'))) {
        const int open = s.lastIndexOf(QLatin1Char('['));
        if (open < 0) return fail(QStringLiteral("unbalanced USE dependency"));
        if (!parseUseDeps(s.mid(open + 1, s.size() - open - 2), &a.m_useDeps)) {
            return fail(QStringLiteral("bad USE dependency"));
        }
        s.truncate(open);
    }
    // Repository.
    if (const int repo = s.indexOf(QStringLiteral("::")); repo >= 0) {
        a.m_repository = s.mid(repo + 2);
        s.truncate(repo);
        if (a.m_repository.isEmpty()) return fail(QStringLiteral("empty repository"));
    }
    // Slot.
    if (const int colon = s.indexOf(QLatin1Char(':')); colon >= 0) {
        QString slot = s.mid(colon + 1);
        s.truncate(colon);
        if (slot.endsWith(QLatin1Char('=')) || slot == QLatin1String("*")) {
            a.m_slotOperator = slot.back();
            slot.chop(1);
        }
        const int sub = slot.indexOf(QLatin1Char('/'));
        a.m_slot = sub >= 0 ? slot.left(sub) : slot;
        a.m_subSlot = sub >= 0 ? slot.mid(sub + 1) : QString();
    }

    // Operator.
    static const struct { const char *text; Operator op; } ops[] = {
        {"<=", LessEqual}, {">=", GreaterEqual}, {"<", Less}, {">", Greater},
        {"=", Equal}, {"~", Approx},
    };
    for (const auto &candidate : ops) {
        const QString t = QString::fromLatin1(candidate.text);
        if (s.startsWith(t)) {
            a.m_op = candidate.op;
            s.remove(0, t.size());
            break;
        }
    }
    if (a.m_op == Equal && s.endsWith(QLatin1Char('*'))) {
        a.m_op = EqualGlob;
        s.chop(1);
    }

    const int slash = s.indexOf(QLatin1Char('/'));
    if (slash <= 0) return fail(QStringLiteral("missing category"));
    a.m_category = s.left(slash);
    QString rest = s.mid(slash + 1);
    if (!categoryPattern().match(a.m_category).hasMatch()) {
        return fail(QStringLiteral("bad category"));
    }
    if (a.m_op != None) {
        QString version;
        if (!Version::splitNameVersion(rest, &a.m_name, &version)) {
            return fail(QStringLiteral("operator without version"));
        }
        a.m_version = Version::parse(version);
        if (a.m_op == Approx && a.m_version.revision() != 0) {
            return fail(QStringLiteral("~ with a revision"));
        }
    } else {
        a.m_name = rest;
    }
    if (!namePattern().match(a.m_name).hasMatch()) return fail(QStringLiteral("bad package name"));
    a.m_valid = true;
    return a;
}

bool Atom::matches(const QString &category, const QString &name, const Version &version,
                   const QString &slot, const QString &repo) const
{
    if (!m_valid || category != m_category || name != m_name) return false;
    if (!m_repository.isEmpty() && !repo.isEmpty() && repo != m_repository) return false;
    if (!m_slot.isEmpty() && !slot.isEmpty()) {
        const QString mainSlot = slot.section(QLatin1Char('/'), 0, 0);
        if (mainSlot != m_slot) return false;
        // A caller passing only the main slot skips the sub-slot check.
        if (!m_subSlot.isEmpty() && slot.contains(QLatin1Char('/'))
            && slot.section(QLatin1Char('/'), 1, 1) != m_subSlot) {
            return false;
        }
    }
    const int c = Version::compare(version, m_version);
    switch (m_op) {
    case None: return true;
    case Less: return c < 0;
    case LessEqual: return c <= 0;
    case Equal: return c == 0;
    case GreaterEqual: return c >= 0;
    case Greater: return c > 0;
    case EqualGlob: return version.matchesGlob(m_version);
    case Approx: return Version::compare(version.withoutRevision(), m_version) == 0;
    }
    return false;
}

bool Atom::matchesUse(const QSet<QString> &candidateUse, const QSet<QString> &candidateIuse,
                      const QSet<QString> &parentUse) const
{
    for (const UseDep &dep : m_useDeps) {
        bool enabled = candidateUse.contains(dep.flag);
        if (!candidateIuse.contains(dep.flag)) {
            if (dep.missingDefault.isNull()) continue; // cannot judge; treat as satisfied
            enabled = dep.missingDefault == QLatin1Char('+');
        }
        const bool parent = parentUse.contains(dep.flag);
        switch (dep.kind) {
        case UseDep::Enabled: if (!enabled) return false; break;
        case UseDep::Disabled: if (enabled) return false; break;
        case UseDep::Equal: if (enabled != parent) return false; break;
        case UseDep::NotEqual: if (enabled == parent) return false; break;
        case UseDep::IfEnabled: if (parent && !enabled) return false; break;
        case UseDep::IfDisabled: if (!parent && enabled) return false; break;
        }
    }
    return true;
}

} // namespace QindaPortage::Core
