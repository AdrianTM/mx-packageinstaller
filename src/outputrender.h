/**********************************************************************
 *  outputrender.h
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

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>

// Rendering helpers for the Output tab. Kept header-only so the carriage-return /
// progress-line handling can be exercised directly by unit tests without pulling in
// the whole MainWindow.
namespace OutputRender
{

// Strip the ANSI escape and query sequences that command-line tools emit when they
// believe they are writing to a terminal (colors, cursor moves, status reports).
inline QString sanitizeOutputForDisplay(const QString &output)
{
    static const QRegularExpression ansiEscape {R"(\x1B\[[0-9;?]*[A-Za-z])"};
    static const QRegularExpression ansiQuery {R"(\x1B\[[0-9;?]*n)"};
    QString cleanOutput = output;
    cleanOutput.remove(ansiEscape);
    cleanOutput.remove(ansiQuery);
    return cleanOutput;
}

// Append a chunk of process output to outputBox, interpreting carriage returns the way
// a terminal would so in-place progress redraws don't pile up as separate lines:
//  * "\r\n" is a normal line ending.
//  * flatpak's progress lines start with Installing/Uninstalling/Updating and are
//    collapsed onto the previous matching line.
//  * a bare "\r" on any other line (e.g. snap's download/progress bar) overwrites the
//    current line in place.
inline void appendProcessOutput(QPlainTextEdit *outputBox, const QString &output)
{
    if (!outputBox) {
        return;
    }

    static const QRegularExpression statusKey {
        R"(^\s*(Installing|Uninstalling|Updating)(?:\s+\d+/\d+(?:…|\.\.\.)?|(?:…|\.\.\.)))"};

    const QString cleanOutput = sanitizeOutputForDisplay(output);

    auto replaceLastStatusLine = [&](const QString &key, const QString &line) {
        QTextBlock block = outputBox->document()->lastBlock();
        while (block.isValid()) {
            const QString text = block.text();
            if (text.trimmed().isEmpty()) {
                block = block.previous();
                continue;
            }
            const QRegularExpressionMatch match = statusKey.match(text);
            if (match.hasMatch() && match.captured(1) == key) {
                QTextCursor cursor(block);
                cursor.select(QTextCursor::LineUnderCursor);
                cursor.removeSelectedText();
                cursor.insertText(line);
                return true;
            }
            break;
        }
        return false;
    };

    bool shouldScrollToBottom = false;

    auto insertLine = [&](const QString &line, bool addNewline, bool overwriteCurrentLine) {
        QTextCursor cursor = outputBox->textCursor();
        cursor.movePosition(QTextCursor::End);
        const bool shouldOverwrite = overwriteCurrentLine && !line.isEmpty();
        if (shouldOverwrite) {
            cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        cursor.insertText(line);
        if (addNewline) {
            cursor.insertText("\n");
        }
        outputBox->setTextCursor(cursor);
        if (addNewline || !shouldOverwrite) {
            shouldScrollToBottom = true;
        }
    };

    bool overwriteCurrentLine = false;
    bool skipNextLineFeed = false;
    QString buffer;
    auto flushBuffer = [&](bool addNewline) {
        if (buffer.isEmpty() && !addNewline) {
            return;
        }
        const QString line = buffer;
        buffer.clear();
        const QRegularExpressionMatch match = statusKey.match(line);
        const QString key = match.hasMatch() ? match.captured(1) : QString();
        const bool replaced = !key.isEmpty() && !overwriteCurrentLine && replaceLastStatusLine(key, line);
        if (!replaced) {
            insertLine(line, addNewline, overwriteCurrentLine);
        }
    };

    for (int i = 0; i < cleanOutput.size(); ++i) {
        const QChar ch = cleanOutput.at(i);
        if (ch == QLatin1Char('\r')) {
            if (buffer.isEmpty()) {
                overwriteCurrentLine = true;
                skipNextLineFeed = false;
                continue;
            }

            const bool isProgressLine = statusKey.match(buffer).hasMatch();
            // A bare carriage return (not part of a "\r\n" line ending) means the tool
            // is redrawing the current line in place. flatpak's progress lines match
            // statusKey and are collapsed below; snap's progress bar does not, so without
            // this it would stack one line per redraw. Overwrite the line instead.
            const bool isCrlf = (i + 1 < cleanOutput.size()) && cleanOutput.at(i + 1) == QLatin1Char('\n');
            if (!isProgressLine && !isCrlf) {
                flushBuffer(false);
                overwriteCurrentLine = true;
                skipNextLineFeed = false;
            } else {
                flushBuffer(!isProgressLine);
                overwriteCurrentLine = isProgressLine;
                skipNextLineFeed = !isProgressLine;
            }
        } else if (ch == QLatin1Char('\n')) {
            if (skipNextLineFeed) {
                skipNextLineFeed = false;
                overwriteCurrentLine = false;
                continue;
            }
            flushBuffer(true);
            overwriteCurrentLine = false;
        } else {
            skipNextLineFeed = false;
            buffer.append(ch);
        }
    }
    flushBuffer(false);

    if (shouldScrollToBottom) {
        outputBox->verticalScrollBar()->setValue(outputBox->verticalScrollBar()->maximum());
    }
}

} // namespace OutputRender
