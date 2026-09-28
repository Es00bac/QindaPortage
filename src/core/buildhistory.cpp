// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/buildhistory.h"

#include "core/version.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace QindaPortage::Core {

namespace {

QString keyOf(const QString &cpv)
{
    const int slash = cpv.indexOf(QLatin1Char('/'));
    QString pn, pv;
    if (slash < 0 || !Version::splitNameVersion(cpv.mid(slash + 1), &pn, &pv)) return cpv;
    return cpv.left(slash + 1) + pn;
}

} // namespace

bool BuildHistory::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("cannot read %1: %2").arg(path, f.errorString());
        return false;
    }
    m_error.clear();
    parse(f.readAll());
    return true;
}

void BuildHistory::parse(const QByteArray &log)
{
    m_records.clear();
    m_byKey.clear();
    m_syncs.clear();
    m_lastSync = 0;

    static const QRegularExpression startRe(QStringLiteral("^>>> emerge \\(\\d+ of \\d+\\) (\\S+) to "));
    static const QRegularExpression doneRe(QStringLiteral("^::: completed emerge \\(\\d+ of \\d+\\) (\\S+) to "));
    static const QRegularExpression binaryRe(QStringLiteral("^=== \\(\\d+ of \\d+\\) Merging Binary \\(([^:]+)::"));
    static const QRegularExpression syncRe(QStringLiteral("^=== Sync completed for (\\S+)"));

    QHash<QString, int> open;  // cpv -> record index, current session
    qint64 lastTime = 0;
    auto closeSession = [&](qint64 when) {
        for (auto it = open.cbegin(); it != open.cend(); ++it) {
            MergeRecord &r = m_records[it.value()];
            r.ok = false;
            r.seconds = std::max<qint64>(0, when - r.start);
        }
        open.clear();
    };

    for (const QByteArray &raw : log.split('\n')) {
        const int colon = raw.indexOf(':');
        if (colon <= 0) continue;
        bool isNumber = false;
        const qint64 when = raw.left(colon).toLongLong(&isNumber);
        if (!isNumber) continue;
        lastTime = when;
        const QString text = QString::fromUtf8(raw.mid(colon + 1)).trimmed();

        if (text.startsWith(QLatin1String("Started emerge on:"))
            || text.startsWith(QLatin1String("*** terminating."))) {
            closeSession(when);
            continue;
        }
        if (text.startsWith(QLatin1String(">>> "))) {
            if (const auto m = startRe.match(text); m.hasMatch()) {
                MergeRecord r;
                r.cpv = m.captured(1);
                r.key = keyOf(r.cpv);
                r.start = when;
                open.insert(r.cpv, m_records.size());
                m_byKey[r.key].append(m_records.size());
                m_records.append(r);
            }
            continue;
        }
        if (text.startsWith(QLatin1String(":::"))) {
            if (const auto m = doneRe.match(text); m.hasMatch()) {
                const auto it = open.constFind(m.captured(1));
                if (it != open.cend()) {
                    MergeRecord &r = m_records[it.value()];
                    r.ok = true;
                    r.seconds = when - r.start;
                    open.erase(it);
                }
            }
            continue;
        }
        if (text.startsWith(QLatin1String("==="))) {
            if (const auto m = binaryRe.match(text); m.hasMatch()) {
                const auto it = open.constFind(m.captured(1));
                if (it != open.cend()) m_records[it.value()].binary = true;
            } else if (const auto s = syncRe.match(text); s.hasMatch()) {
                m_syncs.insert(s.captured(1), when);
                m_lastSync = std::max(m_lastSync, when);
            }
        }
    }
    // A merge still open at the end of the log is either running now or was
    // killed; report it as not completed with the time observed so far.
    closeSession(lastTime);
}

QVector<MergeRecord> BuildHistory::history(const QString &key) const
{
    QVector<MergeRecord> out;
    for (int i : m_byKey.value(key)) out.append(m_records.at(i));
    return out;
}

qint64 BuildHistory::estimateSeconds(const QString &key) const
{
    QVector<qint64> times;
    const QVector<int> list = m_byKey.value(key);
    for (int i = list.size() - 1; i >= 0 && times.size() < 5; --i) {
        const MergeRecord &r = m_records.at(list.at(i));
        if (r.ok && !r.binary && r.seconds >= 0) times.append(r.seconds);
    }
    if (times.isEmpty()) return -1;
    std::sort(times.begin(), times.end());
    return times.at(times.size() / 2);
}

QVector<MergeRecord> BuildHistory::recent(int n) const
{
    QVector<MergeRecord> out;
    for (int i = m_records.size() - 1; i >= 0 && out.size() < n; --i) out.append(m_records.at(i));
    return out;
}

QVector<MergeRecord> BuildHistory::failures(int n) const
{
    QVector<MergeRecord> out;
    for (int i = m_records.size() - 1; i >= 0 && out.size() < n; --i) {
        if (!m_records.at(i).ok) out.append(m_records.at(i));
    }
    return out;
}

} // namespace QindaPortage::Core
