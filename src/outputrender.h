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
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>

// Rendering helper for the Output tab. Kept header-only so the carriage-return /
// progress handling can be exercised directly by unit tests without pulling in the
// whole MainWindow.
namespace OutputRender
{

// Minimal terminal-style renderer for a QPlainTextEdit. Tools like snap and flatpak
// redraw a progress line in place with a bare carriage return (no newline), once per
// poll. That output arrives split across many readyRead chunks, so the cursor state
// (which column the next character overwrites) has to persist across calls -- otherwise
// every redraw is appended as a new line and the box fills with hundreds of copies of
// the same line.
//
//   '\n'  finalizes the current line and starts a new one
//   '\r'  moves the cursor back to column 0 of the current line (overwrite in place)
//   ESC[  ANSI control sequence; CSI K (erase in line) is honored, the rest are skipped
//   else  writes at the cursor, overwriting existing characters or extending the line
class OutputRenderer
{
public:
    OutputRenderer() = default;
    explicit OutputRenderer(QPlainTextEdit *box)
        : outputBox(box)
    {
    }

    void setOutputBox(QPlainTextEdit *box)
    {
        outputBox = box;
        reset();
    }

    // Forget the in-progress line. Call after clearing the box or editing it directly
    // (the next append() also re-syncs from the document, so this is belt-and-braces).
    void reset()
    {
        currentLine.clear();
        cursorColumn = 0;
        pendingEscape.clear();
    }

    void append(const QString &output)
    {
        if (!outputBox || output.isEmpty()) {
            return;
        }

        // Re-sync with the document if something else edited the box since the last
        // append (status messages, a clear(), the core-install retry truncation). During
        // normal streaming the last block already equals currentLine, so this is a no-op
        // and the cursor column persists across chunks.
        const QString lastBlockText = outputBox->document()->lastBlock().text();
        if (lastBlockText != currentLine) {
            currentLine = lastBlockText;
            cursorColumn = currentLine.size();
        }

        QString input = pendingEscape + output;
        pendingEscape.clear();

        QStringList completedLines;
        const int size = input.size();
        for (int i = 0; i < size; ++i) {
            const QChar ch = input.at(i);
            if (ch == QChar(0x1B)) { // ESC: consume an ANSI control sequence
                i = consumeEscape(input, i);
                continue;
            }
            if (ch == QLatin1Char('\n')) {
                completedLines.append(currentLine);
                currentLine.clear();
                cursorColumn = 0;
            } else if (ch == QLatin1Char('\r')) {
                cursorColumn = 0;
            } else {
                if (cursorColumn < currentLine.size()) {
                    currentLine[cursorColumn] = ch;
                } else {
                    currentLine.append(ch);
                }
                ++cursorColumn;
            }
        }

        // The document's last block still holds the previous currentLine. Replace it
        // with the lines completed in this chunk followed by the live current line.
        QTextCursor cursor(outputBox->document());
        cursor.movePosition(QTextCursor::End);
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        QString replacement;
        for (const QString &line : completedLines) {
            replacement += line;
            replacement += QLatin1Char('\n');
        }
        replacement += currentLine;
        cursor.insertText(replacement);

        outputBox->verticalScrollBar()->setValue(outputBox->verticalScrollBar()->maximum());
    }

private:
    // Handle the ANSI escape sequence starting at escIndex (output[escIndex] == ESC).
    // Honors "erase in line" (CSI K), which tools use to clear a progress line before
    // printing a shorter final line; everything else (colors, cursor show/hide, status
    // reports, cursor-style) is skipped. Returns the index of the last consumed character.
    int consumeEscape(const QString &output, int escIndex)
    {
        const int size = output.size();
        if (escIndex + 1 >= size || output.at(escIndex + 1) != QLatin1Char('[')) {
            if (escIndex + 1 >= size) {
                pendingEscape = output.mid(escIndex);
                return size - 1;
            }
            if (output.at(escIndex + 1) == QLatin1Char(']')) {
                return consumeOsc(output, escIndex);
            }
            return escIndex; // lone ESC or non-CSI escape: drop just the ESC
        }
        // CSI: ESC '[' parameter-bytes (0x30-0x3F) intermediate-bytes (0x20-0x2F) final (0x40-0x7E)
        int j = escIndex + 2;
        const int paramStart = j;
        while (j < size) {
            const ushort u = output.at(j).unicode();
            if (u >= 0x30 && u <= 0x3F) {
                ++j;
            } else {
                break;
            }
        }
        const int paramEnd = j;
        while (j < size) {
            const ushort u = output.at(j).unicode();
            if (u >= 0x20 && u <= 0x2F) {
                ++j;
            } else {
                break;
            }
        }
        if (j >= size) {
            pendingEscape = output.mid(escIndex);
            return size - 1; // sequence split across chunks: drop what we have
        }
        const QChar finalByte = output.at(j);
        const QString params = output.mid(paramStart, paramEnd - paramStart);
        if (finalByte == QLatin1Char('K')) {
            if (params.isEmpty() || params == QLatin1String("0")) {
                currentLine.truncate(cursorColumn); // erase from cursor to end of line
            } else if (params == QLatin1String("2")) {
                currentLine.clear(); // erase the whole line
                cursorColumn = 0;
            }
        }
        return j;
    }

    int consumeOsc(const QString &output, int escIndex)
    {
        const int size = output.size();
        for (int j = escIndex + 2; j < size; ++j) {
            if (output.at(j) == QChar(0x07)) {
                return j;
            }
            if (output.at(j) == QChar(0x1B) && j + 1 < size && output.at(j + 1) == QLatin1Char('\\')) {
                return j + 1;
            }
        }
        pendingEscape = output.mid(escIndex);
        return size - 1;
    }

    QPlainTextEdit *outputBox = nullptr;
    QString currentLine;
    QString pendingEscape;
    int cursorColumn = 0;
};

} // namespace OutputRender
