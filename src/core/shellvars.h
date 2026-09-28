// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMap>
#include <QString>

namespace QindaPortage::Core {

// Reads the bash-assignment subset used by make.conf and make.defaults:
// VAR="value" / VAR='value' / VAR=value, multi-line quotes, backslash
// continuations, ${VAR} and $VAR expansion against earlier assignments,
// "source"/"export" ignored. Enough to know what the file says; the resolved
// values Portage uses come from `portageq envvar` (PortageConfig).
class ShellVars
{
public:
    static ShellVars readFile(const QString &path);
    static ShellVars parse(const QString &text);

    QString value(const QString &name) const { return m_values.value(name); }
    const QMap<QString, QString> &values() const { return m_values; }
    // 1-based line of the last assignment to `name`, 0 if none.
    int lineOf(const QString &name) const { return m_lines.value(name); }

private:
    QMap<QString, QString> m_values;
    QMap<QString, int> m_lines;
};

} // namespace QindaPortage::Core
