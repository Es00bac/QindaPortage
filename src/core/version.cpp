// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/version.h"

#include <QRegularExpression>

namespace QindaPortage::Core {

namespace {

// AGENT-NOTE: suffix order from PMS §3.3 algorithm 3.6: _alpha < _beta <
// _pre < _rc < (no suffix) < _p. "No suffix" is represented by kind 0.
int suffixKind(const QString &name)
{
    if (name == QLatin1String("alpha")) return -4;
    if (name == QLatin1String("beta")) return -3;
    if (name == QLatin1String("pre")) return -2;
    if (name == QLatin1String("rc")) return -1;
    if (name == QLatin1String("p")) return 1;
    return 0;
}

const QRegularExpression &versionPattern()
{
    static const QRegularExpression re(QStringLiteral(
        "^(\\d+(?:\\.\\d+)*)([a-z])?((?:_(?:alpha|beta|pre|rc|p)\\d*)*)(?:-r(\\d+))?$"));
    return re;
}

int compareDecimal(const QString &a, const QString &b)
{
    // Numeric comparison of arbitrary-length digit strings.
    QString x = a, y = b;
    while (x.size() > 1 && x.startsWith(QLatin1Char('0'))) x.remove(0, 1);
    while (y.size() > 1 && y.startsWith(QLatin1Char('0'))) y.remove(0, 1);
    if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
    return QString::compare(x, y);
}

} // namespace

Version Version::parse(const QString &text)
{
    Version v;
    const auto match = versionPattern().match(text);
    if (!match.hasMatch()) {
        return v;
    }
    v.m_text = text;
    v.m_numbers = match.captured(1).split(QLatin1Char('.'));
    const QString letter = match.captured(2);
    if (!letter.isEmpty()) v.m_letter = letter.at(0);
    static const QRegularExpression suffixRe(QStringLiteral("_(alpha|beta|pre|rc|p)(\\d*)"));
    auto it = suffixRe.globalMatch(match.captured(3));
    while (it.hasNext()) {
        const auto s = it.next();
        v.m_suffixes.append({suffixKind(s.captured(1)), s.captured(2).toLongLong()});
    }
    v.m_revision = match.captured(4).toInt();
    v.m_valid = true;
    return v;
}

Version Version::withoutRevision() const
{
    if (!m_valid || m_revision == 0) return *this;
    Version v = *this;
    v.m_revision = 0;
    v.m_text = m_text.left(m_text.lastIndexOf(QStringLiteral("-r")));
    return v;
}

int Version::compare(const Version &a, const Version &b)
{
    if (!a.m_valid || !b.m_valid) {
        return int(a.m_valid) - int(b.m_valid);
    }
    // Algorithm 3.2: first component numerically.
    if (int c = compareDecimal(a.m_numbers.first(), b.m_numbers.first())) return c;
    // Algorithm 3.3: later components; a leading zero means string compare
    // with trailing zeros stripped.
    const int n = std::min(a.m_numbers.size(), b.m_numbers.size());
    for (int i = 1; i < n; ++i) {
        const QString &x = a.m_numbers.at(i);
        const QString &y = b.m_numbers.at(i);
        if (x.startsWith(QLatin1Char('0')) || y.startsWith(QLatin1Char('0'))) {
            QString xs = x, ys = y;
            while (xs.endsWith(QLatin1Char('0'))) xs.chop(1);
            while (ys.endsWith(QLatin1Char('0'))) ys.chop(1);
            if (int c = QString::compare(xs, ys)) return c < 0 ? -1 : 1;
        } else if (int c = compareDecimal(x, y)) {
            return c;
        }
    }
    if (a.m_numbers.size() != b.m_numbers.size()) {
        return a.m_numbers.size() < b.m_numbers.size() ? -1 : 1;
    }
    // Letter.
    if (a.m_letter != b.m_letter) {
        if (a.m_letter.isNull()) return -1;
        if (b.m_letter.isNull()) return 1;
        return a.m_letter < b.m_letter ? -1 : 1;
    }
    // Suffixes (algorithm 3.5/3.6).
    const int sn = std::max(a.m_suffixes.size(), b.m_suffixes.size());
    for (int i = 0; i < sn; ++i) {
        const Suffix sa = i < a.m_suffixes.size() ? a.m_suffixes.at(i) : Suffix{};
        const Suffix sb = i < b.m_suffixes.size() ? b.m_suffixes.at(i) : Suffix{};
        if (sa.kind != sb.kind) return sa.kind < sb.kind ? -1 : 1;
        if (sa.number != sb.number) return sa.number < sb.number ? -1 : 1;
    }
    if (a.m_revision != b.m_revision) return a.m_revision < b.m_revision ? -1 : 1;
    return 0;
}

bool Version::matchesGlob(const Version &prefix) const
{
    // PMS: "=ver*" matches when the version string, component-wise, starts
    // with the prefix. Compare the text on component boundaries.
    if (!m_valid || !prefix.m_valid) return false;
    const QString p = prefix.m_text;
    if (!m_text.startsWith(p)) return false;
    if (m_text.size() == p.size()) return true;
    // Next character must begin a new component unless the prefix ended in
    // a numeric component that the version continues (1.2* vs 1.20: no).
    const QChar next = m_text.at(p.size());
    if (next.isDigit() && p.back().isDigit()) return false;
    return true;
}

bool Version::splitNameVersion(const QString &pf, QString *pn, QString *version)
{
    // The version starts at the last "-<digit>" whose remainder parses as a
    // version, trying revision-bearing splits first.
    for (int i = pf.size() - 1; i > 0; --i) {
        if (pf.at(i - 1) != QLatin1Char('-') || !pf.at(i).isDigit()) continue;
        const QString candidate = pf.mid(i);
        if (Version::parse(candidate).isValid()) {
            // PMS forbids a PN ending in "-<version>", so the right-most
            // valid split is the only one.
            if (pn) *pn = pf.left(i - 1);
            if (version) *version = candidate;
            return true;
        }
    }
    return false;
}

} // namespace QindaPortage::Core
