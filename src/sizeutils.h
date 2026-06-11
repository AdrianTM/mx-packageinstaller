/**********************************************************************
 *  sizeutils.h
 **********************************************************************
 * Copyright (C) 2026 MX Authors
 *
 * Authors: Adrian
 *          MX Linux <http://mxlinux.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#pragma once

#include <QRegularExpression>
#include <QString>
#include <QtGlobal>

namespace SizeUtils
{

namespace detail
{
inline QString normalizeNumber(QString number)
{
    const qsizetype lastComma = number.lastIndexOf(',');
    const qsizetype lastDot = number.lastIndexOf('.');
    const QChar decimalSeparator = lastComma > lastDot ? QChar(',') : QChar('.');

    if (lastComma >= 0 && lastDot >= 0) {
        const QChar thousandsSeparator = decimalSeparator == QChar(',') ? QChar('.') : QChar(',');
        number.remove(thousandsSeparator);
    }

    if (decimalSeparator == QChar(',')) {
        number.replace(',', '.');
    }

    return number;
}
} // namespace detail

inline quint64 sizeStringToBytes(const QString &size)
{
    QString normalized = size.trimmed();
    normalized.replace(QChar(0x00a0), QLatin1Char(' '));
    normalized.replace(QChar(0x202f), QLatin1Char(' '));

    static const QRegularExpression sizeRegex(
        QStringLiteral(R"(^\s*([0-9][0-9.,]*)\s*([A-Za-z]+)?\s*$)"));
    const QRegularExpressionMatch match = sizeRegex.match(normalized);
    if (!match.hasMatch()) {
        return 0;
    }

    bool ok = false;
    const double value = detail::normalizeNumber(match.captured(1)).toDouble(&ok);
    if (!ok || value < 0.0) {
        return 0;
    }

    // Flatpak formats sizes with SI decimal units (1 kB = 1000 bytes)
    const QString unit = match.captured(2).toUpper();
    quint64 multiplier = 1;
    if (unit == QLatin1String("KB")) {
        multiplier = 1000ULL;
    } else if (unit == QLatin1String("KIB")) {
        multiplier = 1024ULL;
    } else if (unit == QLatin1String("MB")) {
        multiplier = 1000ULL * 1000ULL;
    } else if (unit == QLatin1String("MIB")) {
        multiplier = 1024ULL * 1024ULL;
    } else if (unit == QLatin1String("GB")) {
        multiplier = 1000ULL * 1000ULL * 1000ULL;
    } else if (unit == QLatin1String("GIB")) {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
    } else if (unit == QLatin1String("TB")) {
        multiplier = 1000ULL * 1000ULL * 1000ULL * 1000ULL;
    } else if (unit == QLatin1String("TIB")) {
        multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    }

    return static_cast<quint64>(value * static_cast<double>(multiplier));
}

} // namespace SizeUtils
