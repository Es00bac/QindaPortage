// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/atom.h"

#include <QSet>
#include <QString>
#include <QVector>

#include <memory>

namespace QindaPortage::Core {

// A parsed dependency specification (PMS §8.2): atoms, "|| ( )" any-of
// groups, "( )" all-of groups and "flag? ( )" / "!flag? ( )" conditionals.
struct DepNode
{
    enum Kind { AllOf, AnyOf, UseConditional, Leaf };
    Kind kind = AllOf;
    QString flag;          // UseConditional
    bool negated = false;  // UseConditional "!flag?"
    Atom atom;             // Leaf
    QVector<DepNode> children;
};

// One requirement after USE conditionals are applied: the package needs one
// of `alternatives`, each an all-of list of atoms. A plain atom is a single
// alternative with a single atom.
struct DepRequirement
{
    QVector<QVector<Atom>> alternatives;
    bool isAnyOf() const { return alternatives.size() > 1; }
};

class DepSpec
{
public:
    // Parses a *DEPEND string. On error returns an AllOf root and sets
    // `error` (the partial tree up to the error is kept).
    static DepNode parse(const QString &text, QString *error = nullptr);

    // Applies USE conditionals with the package's enabled flags and flattens
    // the tree into requirements.
    static QVector<DepRequirement> evaluate(const DepNode &root, const QSet<QString> &use);

    // Every atom mentioned anywhere, ignoring conditionals (for "which flag
    // pulls this in" explanations): atom text -> controlling flags ("!x" for
    // negated conditions), outermost first.
    static QVector<QPair<Atom, QStringList>> atomsWithConditions(const DepNode &root);
};

} // namespace QindaPortage::Core
