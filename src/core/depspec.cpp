// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/depspec.h"

#include <QRegularExpression>

namespace QindaPortage::Core {

namespace {

struct Parser
{
    QStringList tokens;
    int pos = 0;
    QString error;

    bool atEnd() const { return pos >= tokens.size(); }

    // Parses tokens into `parent` until ")" or end.
    void parseList(DepNode &parent, bool nested)
    {
        while (!atEnd()) {
            const QString tok = tokens.at(pos);
            if (tok == QLatin1String(")")) {
                if (!nested) error = QStringLiteral("unexpected )");
                ++pos;
                return;
            }
            ++pos;
            if (tok == QLatin1String("||")) {
                DepNode group;
                group.kind = DepNode::AnyOf;
                if (!expectOpen()) return;
                parseList(group, true);
                parent.children.append(std::move(group));
            } else if (tok == QLatin1String("(")) {
                DepNode group;
                group.kind = DepNode::AllOf;
                parseList(group, true);
                parent.children.append(std::move(group));
            } else if (tok.endsWith(QLatin1Char('?'))) {
                DepNode cond;
                cond.kind = DepNode::UseConditional;
                cond.negated = tok.startsWith(QLatin1Char('!'));
                cond.flag = tok.mid(cond.negated ? 1 : 0).chopped(1);
                if (!expectOpen()) return;
                parseList(cond, true);
                parent.children.append(std::move(cond));
            } else {
                DepNode leaf;
                leaf.kind = DepNode::Leaf;
                leaf.atom = Atom::parse(tok);
                if (!leaf.atom.isValid() && error.isEmpty()) {
                    error = QStringLiteral("bad atom '%1': %2").arg(tok, leaf.atom.error());
                }
                parent.children.append(std::move(leaf));
            }
            if (!error.isEmpty()) return;
        }
        if (nested) error = QStringLiteral("missing )");
    }

    bool expectOpen()
    {
        if (atEnd() || tokens.at(pos) != QLatin1String("(")) {
            error = QStringLiteral("expected (");
            return false;
        }
        ++pos;
        return true;
    }
};

// Cartesian product of all-of lists (for any-of alternatives that are
// themselves groups containing nested any-of groups).
QVector<QVector<Atom>> product(const QVector<QVector<QVector<Atom>>> &sets)
{
    QVector<QVector<Atom>> result{QVector<Atom>{}};
    for (const auto &choices : sets) {
        QVector<QVector<Atom>> next;
        for (const auto &prefix : result) {
            for (const auto &choice : choices) {
                QVector<Atom> combined = prefix;
                combined += choice;
                next.append(combined);
                if (next.size() > 64) return next; // AGENT-GUARD: bound pathological specs
            }
        }
        result = next;
    }
    return result;
}

// All ways to satisfy `node` as all-of atom lists.
QVector<QVector<Atom>> alternativesOf(const DepNode &node, const QSet<QString> &use)
{
    switch (node.kind) {
    case DepNode::Leaf:
        return {{node.atom}};
    case DepNode::UseConditional: {
        if (use.contains(node.flag) == node.negated) return {{}};
        [[fallthrough]];
    }
    case DepNode::AllOf: {
        QVector<QVector<QVector<Atom>>> sets;
        for (const DepNode &child : node.children) sets.append(alternativesOf(child, use));
        return product(sets);
    }
    case DepNode::AnyOf: {
        QVector<QVector<Atom>> all;
        for (const DepNode &child : node.children) {
            const auto alts = alternativesOf(child, use);
            for (const auto &alt : alts) {
                // PMS: an empty alternative (a false conditional) satisfies
                // the group only if it is the sole content; skip it here.
                if (!alt.isEmpty()) all.append(alt);
            }
        }
        if (all.isEmpty()) all.append(QVector<Atom>{});
        return all;
    }
    }
    return {{}};
}

void flatten(const DepNode &node, const QSet<QString> &use, QVector<DepRequirement> *out)
{
    switch (node.kind) {
    case DepNode::Leaf:
        out->append(DepRequirement{{{node.atom}}});
        break;
    case DepNode::UseConditional:
        if (use.contains(node.flag) == node.negated) break;
        [[fallthrough]];
    case DepNode::AllOf:
        for (const DepNode &child : node.children) flatten(child, use, out);
        break;
    case DepNode::AnyOf: {
        DepRequirement req;
        req.alternatives = alternativesOf(node, use);
        if (req.alternatives.size() == 1 && req.alternatives.first().isEmpty()) break;
        out->append(req);
        break;
    }
    }
}

void collect(const DepNode &node, QStringList conditions, QVector<QPair<Atom, QStringList>> *out)
{
    if (node.kind == DepNode::Leaf) {
        out->append({node.atom, conditions});
        return;
    }
    if (node.kind == DepNode::UseConditional) {
        conditions.append(node.negated ? QLatin1Char('!') + node.flag : node.flag);
    }
    for (const DepNode &child : node.children) collect(child, conditions, out);
}

} // namespace

DepNode DepSpec::parse(const QString &text, QString *error)
{
    Parser p;
    p.tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    DepNode root;
    root.kind = DepNode::AllOf;
    p.parseList(root, false);
    if (error) *error = p.error;
    return root;
}

QVector<DepRequirement> DepSpec::evaluate(const DepNode &root, const QSet<QString> &use)
{
    QVector<DepRequirement> out;
    flatten(root, use, &out);
    return out;
}

QVector<QPair<Atom, QStringList>> DepSpec::atomsWithConditions(const DepNode &root)
{
    QVector<QPair<Atom, QStringList>> out;
    collect(root, {}, &out);
    return out;
}

} // namespace QindaPortage::Core
