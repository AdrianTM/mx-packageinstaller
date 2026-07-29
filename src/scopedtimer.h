#pragma once

#include <QDebug>
#include <QElapsedTimer>

// Logs how long the enclosing scope took, once it goes out of scope. Meant to
// be constructed right after a function's "+++ ... +++" entry-trace qDebug()
// line, so its duration lands in the same debug log output.
class ScopedTimer
{
public:
    explicit ScopedTimer(const char *label)
        : label_(label)
    {
        timer_.start();
    }

    ~ScopedTimer() { qDebug() << label_ << "took" << timer_.elapsed() << "ms"; }

private:
    const char *label_;
    QElapsedTimer timer_;
};
