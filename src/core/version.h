// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

// A package version as defined by PMS §3.2 ("1.2.3b_rc1_p2-r3"), with the
// PMS §3.3 comparison. Revisions are part of the value; `withoutRevision()`
// drops them for `~` matching.
class Version
{
public:
    Version() = default;
    // Returns an invalid Version (isValid() == false) when `text` is not a
    // PMS version.
    static Version parse(const QString &text);

    bool isValid() const { return m_valid; }
    QString text() const { return m_text; }
    int revision() const { return m_revision; }
    Version withoutRevision() const;

    // <0, 0, >0 like strcmp. Invalid versions sort before valid ones.
    static int compare(const Version &a, const Version &b);
    // `=ver*` prefix match (PMS §8.3.1): string prefix on the component
    // sequence, e.g. 1.2* matches 1.2, 1.2.5, 1.20 is NOT matched.
    bool matchesGlob(const Version &prefix) const;

    bool operator==(const Version &o) const { return compare(*this, o) == 0; }
    bool operator<(const Version &o) const { return compare(*this, o) < 0; }

    // Splits "cat/pn-ver-rN" style package names: "gtk+-3.24.5-r1" ->
    // ("gtk+", "3.24.5-r1"). Returns false when no version suffix exists.
    static bool splitNameVersion(const QString &pf, QString *pn, QString *version);

private:
    struct Suffix { int kind = 0; qint64 number = 0; };

    QString m_text;
    QStringList m_numbers;  // kept as text: PMS compares some as strings
    QChar m_letter;
    QVector<Suffix> m_suffixes;
    int m_revision = 0;
    bool m_valid = false;
};

} // namespace QindaPortage::Core
