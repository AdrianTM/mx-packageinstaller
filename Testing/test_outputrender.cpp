#include <QPlainTextEdit>
#include <QtTest/QtTest>

#include "src/outputrender.h"

// Verifies the Output-tab rendering logic, in particular how carriage returns from
// terminal-style tools (flatpak, snap) are collapsed instead of stacking.
class TestOutputRender : public QObject
{
    Q_OBJECT

private:
    // Feed each chunk through the renderer the way the live process signal does, then
    // return the resulting plain text.
    static QString render(const QStringList &chunks)
    {
        QPlainTextEdit box;
        for (const QString &chunk : chunks) {
            OutputRender::appendProcessOutput(&box, chunk);
        }
        return box.toPlainText();
    }

private slots:
    void plainCrlfLines()
    {
        // CRLF line endings (as produced through a pseudo-terminal) must render as
        // ordinary separate lines, not blank-and-overwrite.
        QCOMPARE(render({"one\r\ntwo\r\nthree\r\n"}), QString("one\ntwo\nthree\n"));
    }

    void plainLfLines()
    {
        QCOMPARE(render({"alpha\nbeta\n"}), QString("alpha\nbeta\n"));
    }

    void snapProgressCollapsesToSingleLine()
    {
        // snap redraws its progress bar with a bare carriage return and the line does
        // NOT start with Installing/Uninstalling/Updating. Each redraw must overwrite
        // the previous one rather than adding a new line.
        const QString stream = "\rcore 10% 1MB/s\rcore 45% 1MB/s\rcore 99% 1MB/s\r"
                               "core 16-2.61 from Canonical installed\n";
        const QString result = render({stream});
        QCOMPARE(result, QString("core 16-2.61 from Canonical installed\n"));
        // Only a single visible progress line ever existed.
        QVERIFY(!result.contains("10%"));
        QVERIFY(!result.contains("45%"));
    }

    void snapProgressWithAnsiAndChunking()
    {
        // Real snap output is wrapped in ANSI cursor/color codes and arrives split
        // across read chunks; the result should still be one collapsed line.
        const QStringList chunks {
            "\x1B[?25l\rDownloading core 12%\x1B[K",
            "\rDownloading core 58%\x1B[K",
            "\rDownloading core 100%\x1B[K",
            "\rcore installed\x1B[K\r\n\x1B[?25h",
        };
        QCOMPARE(render(chunks), QString("core installed\n"));
    }

    void flatpakProgressStillCollapses()
    {
        // flatpak's progress lines start with "Installing" and are redrawn with a bare
        // carriage return; they must collapse onto a single line (existing behavior).
        const QString stream = "\rInstalling 1/2… 8%\rInstalling 1/2… 53%\rInstalling 1/2… 100%\n";
        QCOMPARE(render({stream}), QString("Installing 1/2… 100%\n"));
    }

    void flatpakProgressAfterNormalLines()
    {
        // A normal CRLF line followed by collapsing flatpak progress.
        const QStringList chunks {
            "Required runtime for org.gnome.Calculator\r\n",
            "\rInstalling 1/2… 10%\rInstalling 1/2… 90%\rInstalling 1/2… 100%\r\n",
        };
        QCOMPARE(render(chunks),
                 QString("Required runtime for org.gnome.Calculator\nInstalling 1/2… 100%\n"));
    }

    void mixedSnapStatusAndProgress()
    {
        // A leading status line (own line) followed by a collapsing snap progress bar.
        const QStringList chunks {
            "Setting up core snap\r\n",
            "\rmounting 20%\rmounting 70%\rmounting done\r\n",
        };
        QCOMPARE(render(chunks), QString("Setting up core snap\nmounting done\n"));
    }
};

QTEST_MAIN(TestOutputRender)
#include "test_outputrender.moc"
