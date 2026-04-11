# Simmer Trajectory — mx-packageinstaller src/

| Iter | Correctness | Quality | Robustness | Composite | Key Change |
|------|-------------|---------|------------|-----------|------------|
| 0    | 6           | 5       | 7          | 6.0       | seed       |
| 1    | 8           | 5       | 8          | 7.0       | fix data race, reply leaks, null derefs, bounds checks |

## Iteration 0 — Seed Judgment

### Judge A (Correctness & Safety): 6/10
- HIGH: Data race on `enabledList` written from QtConcurrent worker thread while GUI thread can read it
- HIGH: Unchecked null from `msgBox.findChild<QTextEdit*>()` in confirmActions (line 2028)
- MEDIUM: Out-of-bounds `detail_list.at(detail_list.size()-2)` without size check (line 3319)
- MEDIUM: Nested QEventLoop in cmd.cpp allows reentrant calls
- MEDIUM: QNetworkReply* leaks when `reply` member is overwritten
- MEDIUM: TOCTOU race in lockfile.cpp lock()
- LOW: Dead getVersion() — `"LANG=Cdpkg-query"` missing space (line 2707)

### Judge B (Code Quality): 5/10
- HIGH: mainwindow.cpp is 4700+ line god class with ~80 methods
- HIGH: Three-way branch duplication across APT tabs (install, confirmActions, hideLibs, forceUpdate, etc.)
- MEDIUM: Status enum defined in two separate places (packagemodel.h and flatpakmodel.cpp)
- MEDIUM: getCurrentModel/Proxy/List repeat same switch pattern 3x
- LOW: blockInterfaceFP(bool) ignores its parameter
- LOW: timer_test.cpp references nonexistent Timer.h
- LOW: checkedPackageNames() just delegates to checkedPackages()

### Judge C (Robustness): 7/10
- HIGH: Shared `QNetworkReply *reply` member across downloadFile/getScreenshotUrl/displayPopularInfo — reentrant calls leak and deadlock
- MEDIUM: Unchecked detail_list bounds in displayPackageInfo
- MEDIUM: No null check on findChild/qobject_cast results
- MEDIUM: Unquoted remote name in remotes.cpp shell command
- LOW: No overall timeout in helper.cpp runProcess
- LOW: lockf() blocking call with no timeout after TOCTOU window

### Synthesized ASI for Iteration 1:
Fix the concrete safety bugs that could crash or hang the application: (1) Eliminate the data race in the constructor's QtConcurrent::run by moving enabledList assignment into the QMetaObject::invokeMethod callback on the main thread. (2) Replace the shared `QNetworkReply *reply` class member with local variables scoped to each method to prevent leaks and deadlocks from reentrant calls. (3) Add null-check guards on msgBox.findChild and qobject_cast results in confirmActions. (4) Add bounds checking on detail_list access in displayPackageInfo. (5) Fix the dead getVersion() function's missing space.
