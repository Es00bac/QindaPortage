// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/version.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaPortage::Core {

// A USE dependency inside an atom's brackets (PMS §8.3.4):
// "flag", "-flag", "flag=", "!flag=", "flag?", "!flag?", with an optional
// "(+)"/"(-)" default.
struct UseDep
{
    enum Kind { Enabled, Disabled, Equal, NotEqual, IfEnabled, IfDisabled };
    Kind kind = Enabled;
    QString flag;
    QChar missingDefault; // '+', '-' or null
};

// A package dependency specification atom (PMS §8.3):
// [!|!!][op]category/package[-version][*][:slot[/subslot][=|*]][::repo][use]
class Atom
{
public:
    enum Operator { None, Less, LessEqual, Equal, EqualGlob, Approx, GreaterEqual, Greater };
    enum Blocker { NoBlock, WeakBlock, StrongBlock };

    static Atom parse(const QString &text);
    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }

    QString text() const { return m_text; }
    QString category() const { return m_category; }
    QString name() const { return m_name; }
    // "category/name", the key packages are grouped by.
    QString key() const { return m_category + QLatin1Char('/') + m_name; }
    Operator op() const { return m_op; }
    Version version() const { return m_version; }
    Blocker blocker() const { return m_blocker; }
    QString slot() const { return m_slot; }
    QString subSlot() const { return m_subSlot; }
    // ":=" or ":slot=" (rebuild on sub-slot change), ":*" (any slot).
    QChar slotOperator() const { return m_slotOperator; }
    QString repository() const { return m_repository; }
    const QVector<UseDep> &useDeps() const { return m_useDeps; }

    // Does an installed/available package `cpv` (version text) in `slot`
    // from `repo` match? USE deps are not checked here (see matchesUse).
    bool matches(const QString &category, const QString &name, const Version &version,
                 const QString &slot = QString(), const QString &repo = QString()) const;
    // USE-dependency check against the candidate's enabled flags and IUSE.
    // `parentUse` resolves conditional forms ("flag?", "flag=").
    bool matchesUse(const QSet<QString> &candidateUse, const QSet<QString> &candidateIuse,
                    const QSet<QString> &parentUse) const;

    // Validates "category/name" syntax only (for user-typed input).
    static bool isValidKey(const QString &key);

private:
    QString m_text;
    QString m_error;
    QString m_category;
    QString m_name;
    QString m_slot;
    QString m_subSlot;
    QString m_repository;
    QVector<UseDep> m_useDeps;
    Version m_version;
    Operator m_op = None;
    Blocker m_blocker = NoBlock;
    QChar m_slotOperator;
    bool m_valid = false;
};

} // namespace QindaPortage::Core
