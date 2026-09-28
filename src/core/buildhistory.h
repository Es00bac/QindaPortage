// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

// One merge attempt from /var/log/emerge.log.
struct MergeRecord
{
    QString key;           // category/name
    QString cpv;           // category/name-version
    qint64 start = 0;      // unix time of ">>> emerge (x of y) … to /"
    qint64 seconds = -1;   // until completion, or until the session ended
    bool ok = false;       // "::: completed emerge"
    bool binary = false;   // "=== (x of y) Merging Binary (…)"
};

// Merge durations, failures and syncs from emerge.log.
//
// AGENT-NOTE: with --jobs the log interleaves merges, so starts and
// completions are matched by cpv within one emerge session (a session begins
// at "Started emerge on:" / "*** emerge …" and ends at "*** terminating." or
// "*** exiting …"). A merge started but not completed when its session ends
// is recorded as failed. Binary merges are marked by the "Merging Binary"
// line and excluded from build-time estimates.
class BuildHistory
{
public:
    bool load(const QString &path = QStringLiteral("/var/log/emerge.log"));
    void parse(const QByteArray &log);
    QString error() const { return m_error; }

    const QVector<MergeRecord> &records() const { return m_records; }
    // Every attempt for `key`, oldest first.
    QVector<MergeRecord> history(const QString &key) const;
    // Median of the last five successful source builds, -1 when unknown.
    qint64 estimateSeconds(const QString &key) const;
    // Newest first.
    QVector<MergeRecord> recent(int n) const;
    QVector<MergeRecord> failures(int n) const;
    // Unix time of the last completed sync of any repository (0 if never),
    // and per repository.
    qint64 lastSync() const { return m_lastSync; }
    qint64 lastSync(const QString &repo) const { return m_syncs.value(repo); }

private:
    QVector<MergeRecord> m_records;
    QHash<QString, QVector<int>> m_byKey;
    QHash<QString, qint64> m_syncs;
    qint64 m_lastSync = 0;
    QString m_error;
};

} // namespace QindaPortage::Core
