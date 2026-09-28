// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/shellvars.h"

#include <QFile>

namespace QindaPortage::Core {

namespace {

bool isNameChar(QChar c, bool first)
{
    return c == QLatin1Char('_') || (c.isLetter() && c.unicode() < 128) || (!first && c.isDigit());
}

QString expand(const QString &text, const QMap<QString, QString> &vars)
{
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c != QLatin1Char('$') || i + 1 >= text.size()) {
            out.append(c);
            continue;
        }
        if (text.at(i + 1) == QLatin1Char('{')) {
            const int close = text.indexOf(QLatin1Char('}'), i + 2);
            if (close < 0) { out.append(c); continue; }
            QString name = text.mid(i + 2, close - i - 2);
            // ${VAR:-default} and friends: keep only the name.
            name = name.section(QLatin1Char(':'), 0, 0);
            out.append(vars.value(name));
            i = close;
        } else if (isNameChar(text.at(i + 1), true)) {
            int j = i + 1;
            while (j < text.size() && isNameChar(text.at(j), false)) ++j;
            out.append(vars.value(text.mid(i + 1, j - i - 1)));
            i = j - 1;
        } else {
            out.append(c);
        }
    }
    return out;
}

} // namespace

ShellVars ShellVars::readFile(const QString &path)
{
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    return parse(QString::fromUtf8(f.readAll()));
}

ShellVars ShellVars::parse(const QString &text)
{
    ShellVars vars;
    int i = 0;
    int line = 1;
    const int n = text.size();
    auto skipToEol = [&] {
        while (i < n && text.at(i) != QLatin1Char('\n')) ++i;
    };
    while (i < n) {
        // Start of a logical line.
        while (i < n && (text.at(i) == QLatin1Char(' ') || text.at(i) == QLatin1Char('\t'))) ++i;
        if (i >= n) break;
        if (text.at(i) == QLatin1Char('\n')) { ++i; ++line; continue; }
        if (text.at(i) == QLatin1Char('#')) { skipToEol(); continue; }
        if (text.mid(i, 7) == QLatin1String("export ")) i += 7;
        int j = i;
        while (j < n && isNameChar(text.at(j), j == i)) ++j;
        if (j == i || j >= n || text.at(j) != QLatin1Char('=')) { skipToEol(); continue; }
        const QString name = text.mid(i, j - i);
        const int startLine = line;
        i = j + 1;
        QString raw;
        bool expandIt = true;
        // Value: concatenation of quoted/unquoted words until whitespace/EOL.
        while (i < n) {
            const QChar c = text.at(i);
            if (c == QLatin1Char('"')) {
                ++i;
                while (i < n && text.at(i) != QLatin1Char('"')) {
                    if (text.at(i) == QLatin1Char('\\') && i + 1 < n) {
                        if (text.at(i + 1) == QLatin1Char('\n')) { i += 2; ++line; continue; }
                        raw.append(text.at(i + 1));
                        i += 2;
                        continue;
                    }
                    if (text.at(i) == QLatin1Char('\n')) ++line;
                    raw.append(text.at(i++));
                }
                ++i;
            } else if (c == QLatin1Char('\'')) {
                ++i;
                QString literal;
                while (i < n && text.at(i) != QLatin1Char('\'')) {
                    if (text.at(i) == QLatin1Char('\n')) ++line;
                    literal.append(text.at(i++));
                }
                ++i;
                // Single-quoted text is not expanded; protect its '$'.
                raw.append(literal);
                if (literal.contains(QLatin1Char('$'))) expandIt = false;
            } else if (c == QLatin1Char('\\') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n')) {
                i += 2;
                ++line;
            } else if (c.isSpace() || c == QLatin1Char('#')) {
                break;
            } else {
                raw.append(c);
                ++i;
            }
        }
        const QString value = expandIt ? expand(raw, vars.m_values) : raw;
        vars.m_values.insert(name, value.simplified());
        vars.m_lines.insert(name, startLine);
        skipToEol();
    }
    return vars;
}

} // namespace QindaPortage::Core
