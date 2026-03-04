# Session Management & Single Instance Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix the tab-close bug, add session restore of open files, and make Penelope a single-instance application.

**Architecture:** Fix tab close with a proper slot that cleans up sidebars and quits on last tab. Extend existing KConfig session save/restore to persist open files and per-tab ViewState. Use KDBusService on Linux (compile-time detected) and QLocalServer/QLocalSocket on Windows for single-instance behavior, with a shared `activateWithFiles()` entry point.

**Tech Stack:** Qt6, KF6 (KConfig, KDBusService/DBusAddons), QLocalServer/QLocalSocket

---

### Task 1: Fix tab close — proper cleanup slot

**Files:**
- Modify: `src/app/mainwindow.h:48-54`
- Modify: `src/app/mainwindow.cpp:93-94`

**Step 1: Add slot declaration to header**

In `mainwindow.h`, add `onTabCloseRequested` to the private slots section (after line 54, `onFileClose`):

```cpp
    void onTabCloseRequested(int index);
```

**Step 2: Replace direct connection with new slot**

In `mainwindow.cpp`, replace lines 93-94:

```cpp
    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            m_tabWidget, &QTabWidget::removeTab);
```

with:

```cpp
    connect(m_tabWidget, &QTabWidget::tabCloseRequested,
            this, &MainWindow::onTabCloseRequested);
```

**Step 3: Implement the slot**

Add the slot implementation (near the other `onFile*` methods, around line 275):

```cpp
void MainWindow::onTabCloseRequested(int index)
{
    m_tabWidget->removeTab(index);

    if (m_tabWidget->count() == 0) {
        // Last tab closed — quit (closeEvent will save session)
        close();
    }
    // Remaining tabs: currentChanged signal already fires and updates sidebars
}
```

The existing `currentChanged` lambda (lines 95-134) already handles sidebar updates (file browser path, file path label, view mode sync, zoom sync) when the current tab changes, so removing a tab automatically triggers the right updates for the now-current tab.

**Step 4: Build**

Run: `cmake --build build 2>&1 | tail -5`

**Step 5: Commit**

```
fix: proper tab close handling — clean up sidebars, quit on last tab
```

---

### Task 2: Save open files and per-tab view state in session

**Files:**
- Modify: `src/app/mainwindow.cpp:1279-1313` (saveSession)

**Step 1: Extend saveSession() to persist open files and view state**

In `saveSession()`, after the existing `SplitterSizes` write (line 1305) and before the type set/color scheme block (line 1307), add:

```cpp
    // Save open files and per-tab view state
    QStringList openFiles;
    QList<int> zoomLevels;
    QList<int> scrollPages;
    QList<double> scrollFractions;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
        if (!tab || tab->filePath().isEmpty())
            continue;
        openFiles << tab->filePath();
        ViewState vs = tab->documentView()->saveViewState();
        zoomLevels << vs.zoomPercent;
        scrollPages << vs.currentPage;
        scrollFractions << vs.scrollFraction;
    }
    group.writeEntry("OpenFiles", openFiles);
    group.writeEntry("ActiveTab", m_tabWidget->currentIndex());
    group.writeEntry("ZoomLevels", zoomLevels);
    group.writeEntry("ScrollPages", scrollPages);
    group.writeEntry("ScrollFractions", QVariant::fromValue(scrollFractions).toStringList());
```

Remove the comment on line 1284 that says `A3: no longer saving open files or active tab` since we now do.

Note: `scrollFractions` is a `QList<double>` which KConfig doesn't directly support via `writeEntry`, so we convert to `QStringList` via `QVariant`. On read-back, we parse strings to double.

**Step 2: Build**

Run: `cmake --build build 2>&1 | tail -5`

**Step 3: Commit**

```
feat: save open files and per-tab view state in session
```

---

### Task 3: Restore open files and view state on startup

**Files:**
- Modify: `src/app/mainwindow.h:43` (add new method)
- Modify: `src/app/mainwindow.cpp:1315-1393` (restoreSession)
- Modify: `src/app/main.cpp:43-55`

**Step 1: Add `restoreOpenFiles()` method to header**

In `mainwindow.h`, in the private section (after `restoreSession()` on line 72):

```cpp
    void restoreOpenFiles();
```

**Step 2: Extract file restoration into a separate method**

In `mainwindow.cpp`, add a new method `restoreOpenFiles()`:

```cpp
void MainWindow::restoreOpenFiles()
{
    KConfigGroup group(KSharedConfig::openConfig(),
                       QStringLiteral("Session"));

    QStringList openFiles = group.readEntry("OpenFiles", QStringList());
    if (openFiles.isEmpty())
        return;

    for (const QString &path : openFiles) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile())
            openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
    }

    // Restore active tab
    int activeTab = group.readEntry("ActiveTab", 0);
    if (activeTab >= 0 && activeTab < m_tabWidget->count())
        m_tabWidget->setCurrentIndex(activeTab);

    // Defer view state restoration until layouts are computed
    QList<int> zoomLevels = group.readEntry("ZoomLevels", QList<int>());
    QList<int> scrollPages = group.readEntry("ScrollPages", QList<int>());
    QStringList fracStrings = group.readEntry("ScrollFractions", QStringList());

    QTimer::singleShot(0, this, [this, zoomLevels, scrollPages, fracStrings]() {
        for (int i = 0; i < m_tabWidget->count() && i < zoomLevels.size(); ++i) {
            auto *tab = qobject_cast<DocumentTab *>(m_tabWidget->widget(i));
            if (!tab)
                continue;
            ViewState vs;
            vs.zoomPercent = zoomLevels.value(i, 100);
            vs.currentPage = scrollPages.value(i, 0);
            vs.scrollFraction = (i < fracStrings.size()) ? fracStrings[i].toDouble() : 0.0;
            vs.valid = true;
            tab->documentView()->restoreViewState(vs);
        }
    });
}
```

**Step 3: Update restoreSession()**

In `restoreSession()`, replace line 1320 (`// A3: No longer restoring open files or active tab`) — leave it removed. The file restoration is handled separately in `restoreOpenFiles()`.

**Step 4: Update main.cpp startup flow**

Replace the command-line file opening block in `main.cpp` (lines 43-54):

```cpp
    MainWindow window;

    // Open files from command line
    const QStringList args = parser.positionalArguments();
    for (const QString &arg : args) {
        QFileInfo fi(arg);
        if (fi.exists() && fi.isFile()) {
            window.openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
        }
    }

    window.show();
```

with:

```cpp
    MainWindow window;

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        // No files on command line — restore previous session
        window.restoreOpenFiles();
    } else {
        // Files provided — open them fresh, skip session restore
        for (const QString &arg : args) {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile())
                window.openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
        }
    }

    window.show();
```

This requires making `restoreOpenFiles()` public in the header. Move it from the `private` section to `public` (next to `openFile`):

```cpp
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QUrl &url);
    void restoreOpenFiles();
```

**Step 5: Add QTimer include**

Add to mainwindow.cpp includes if not present:

```cpp
#include <QTimer>
```

**Step 6: Build**

Run: `cmake --build build 2>&1 | tail -5`

**Step 7: Manual test**

Run: `./build/bin/Penelope docs/glyphs.pdf` (or any markdown file).
- Open a second file. Zoom in on one tab. Scroll to a different position.
- Close Penelope.
- Launch `./build/bin/Penelope` with no arguments.
- Both files should reopen. Zoom and scroll should be restored.
- Launch `./build/bin/Penelope somefile.md` — only that file should open, no session restore.

**Step 8: Commit**

```
feat: restore open files and per-tab view state on session startup
```

---

### Task 4: Single instance — CMake setup and KDBusService (Linux)

**Files:**
- Modify: `CMakeLists.txt:19-28`
- Modify: `src/CMakeLists.txt:331-358` (link libraries)
- Modify: `src/app/main.cpp`
- Modify: `src/app/mainwindow.h`

**Step 1: Add optional KDBusAddons dependency to root CMakeLists.txt**

After line 28 (`find_package(KF6SyntaxHighlighting REQUIRED)`), add:

```cmake
# Optional: single-instance via DBus (Linux/KDE)
find_package(KF6DBusAddons QUIET)
if(KF6DBusAddons_FOUND)
    add_compile_definitions(HAVE_KDBUSSERVICE)
endif()
```

**Step 2: Conditionally link KF6::DBusAddons**

In `src/CMakeLists.txt`, after the `target_link_libraries(Penelope PRIVATE PenelopeCore)` block (line 373-376), add:

```cmake
if(KF6DBusAddons_FOUND)
    target_link_libraries(Penelope PRIVATE KF6::DBusAddons)
endif()
```

**Step 3: Add activateWithFiles() to MainWindow**

In `mainwindow.h`, add to the public section:

```cpp
    void activateWithFiles(const QStringList &paths);
```

In `mainwindow.cpp`, implement:

```cpp
void MainWindow::activateWithFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile())
            openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
    }
    raise();
    activateWindow();
}
```

**Step 4: Wire KDBusService in main.cpp**

Add conditional includes at top of `main.cpp`:

```cpp
#ifdef HAVE_KDBUSSERVICE
#include <KDBusService>
#endif
```

In main(), after creating the MainWindow but before the argument handling block, add:

```cpp
#ifdef HAVE_KDBUSSERVICE
    KDBusService service(KDBusService::Unique);
    QObject::connect(&service, &KDBusService::activateRequested,
                     &window, [&window](const QStringList &args, const QString &workingDir) {
        // First arg is the executable name, skip it
        QStringList files;
        for (int i = 1; i < args.size(); ++i) {
            QString path = args[i];
            if (QFileInfo(path).isRelative())
                path = workingDir + QLatin1Char('/') + path;
            files << path;
        }
        if (!files.isEmpty())
            window.activateWithFiles(files);
        else {
            window.raise();
            window.activateWindow();
        }
    });
#endif
```

**Step 5: Build**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build 2>&1 | tail -5`

(Full reconfigure needed since CMakeLists.txt changed.)

**Step 6: Manual test (Linux)**

- Run `./build/bin/Penelope docs/glyphs.pdf`
- In another terminal, run `./build/bin/Penelope docs/hershey.pdf`
- Second process should exit immediately; first window should gain a new tab with hershey.pdf.

**Step 7: Commit**

```
feat: single-instance via KDBusService on Linux
```

---

### Task 5: Single instance — QLocalServer fallback (non-KDE/Windows)

**Files:**
- Create: `src/app/singleinstanceguard.h`
- Create: `src/app/singleinstanceguard.cpp`
- Modify: `src/CMakeLists.txt:361-364` (add source files)
- Modify: `src/app/main.cpp`

**Step 1: Create SingleInstanceGuard header**

Create `src/app/singleinstanceguard.h`:

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PENELOPE_SINGLEINSTANCEGUARD_H
#define PENELOPE_SINGLEINSTANCEGUARD_H

#include <QLocalServer>
#include <QObject>

class MainWindow;

class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(MainWindow *window, QObject *parent = nullptr);

    /// Try to become the primary instance. Returns true if we are primary.
    /// If another instance is running, sends files to it and returns false.
    bool tryAcquire(const QStringList &filePaths);

private Q_SLOTS:
    void onNewConnection();

private:
    static QString serverName();
    MainWindow *m_window;
    QLocalServer *m_server = nullptr;
};

#endif // PENELOPE_SINGLEINSTANCEGUARD_H
```

**Step 2: Create SingleInstanceGuard implementation**

Create `src/app/singleinstanceguard.cpp`:

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later

#include "singleinstanceguard.h"
#include "mainwindow.h"

#include <QDataStream>
#include <QLocalSocket>

SingleInstanceGuard::SingleInstanceGuard(MainWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

QString SingleInstanceGuard::serverName()
{
    return QStringLiteral("Penelope-%1").arg(qEnvironmentVariable("USER", QStringLiteral("default")));
}

bool SingleInstanceGuard::tryAcquire(const QStringList &filePaths)
{
    // Try to connect to an existing instance
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (socket.waitForConnected(500)) {
        // Another instance is running — send file paths and exit
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        out << filePaths;
        socket.write(data);
        socket.waitForBytesWritten(1000);
        socket.disconnectFromServer();
        return false;
    }

    // We are the primary instance — start listening
    QLocalServer::removeServer(serverName()); // clean up stale socket
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection,
            this, &SingleInstanceGuard::onNewConnection);
    m_server->listen(serverName());
    return true;
}

void SingleInstanceGuard::onNewConnection()
{
    while (QLocalSocket *conn = m_server->nextPendingConnection()) {
        connect(conn, &QLocalSocket::readyRead, this, [this, conn]() {
            QByteArray data = conn->readAll();
            QDataStream in(data);
            QStringList paths;
            in >> paths;
            if (!paths.isEmpty())
                m_window->activateWithFiles(paths);
            else {
                m_window->raise();
                m_window->activateWindow();
            }
            conn->deleteLater();
        });
        // Handle disconnect without data
        connect(conn, &QLocalSocket::disconnected, conn, &QObject::deleteLater);
    }
}
```

**Step 3: Add sources to CMakeLists**

In `src/CMakeLists.txt`, change the Penelope executable definition (lines 361-364) to include the new files conditionally:

```cmake
qt_add_executable(Penelope
    WIN32 MACOSX_BUNDLE
    app/main.cpp
    $<$<NOT:$<BOOL:${KF6DBusAddons_FOUND}>>:
        app/singleinstanceguard.h
        app/singleinstanceguard.cpp
    >
)
```

This only compiles the fallback when KDBusService is NOT available. On Linux with KDE, the DBus path is used instead.

**Step 4: Wire into main.cpp**

Add conditional include at top of `main.cpp`:

```cpp
#ifndef HAVE_KDBUSSERVICE
#include "singleinstanceguard.h"
#endif
```

In main(), add the fallback path. The full argument handling block becomes:

```cpp
    MainWindow window;

#ifdef HAVE_KDBUSSERVICE
    KDBusService service(KDBusService::Unique);
    QObject::connect(&service, &KDBusService::activateRequested,
                     &window, [&window](const QStringList &args, const QString &workingDir) {
        QStringList files;
        for (int i = 1; i < args.size(); ++i) {
            QString path = args[i];
            if (QFileInfo(path).isRelative())
                path = workingDir + QLatin1Char('/') + path;
            files << path;
        }
        if (!files.isEmpty())
            window.activateWithFiles(files);
        else {
            window.raise();
            window.activateWindow();
        }
    });
#else
    SingleInstanceGuard guard(&window);
    // Collect file paths from args before tryAcquire
    QStringList filePaths;
    for (const QString &arg : parser.positionalArguments()) {
        QFileInfo fi(arg);
        if (fi.exists() && fi.isFile())
            filePaths << fi.absoluteFilePath();
    }
    if (!guard.tryAcquire(filePaths)) {
        // Another instance is running, files were sent to it
        return 0;
    }
#endif

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        window.restoreOpenFiles();
    } else {
#ifndef HAVE_KDBUSSERVICE
        // Files already opened by guard.tryAcquire check — open them now
        // (tryAcquire only sends to remote, doesn't open locally)
#endif
        for (const QString &arg : args) {
            QFileInfo fi(arg);
            if (fi.exists() && fi.isFile())
                window.openFile(QUrl::fromLocalFile(fi.absoluteFilePath()));
        }
    }

    window.show();
    return app.exec();
```

**Step 5: Build**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build 2>&1 | tail -5`

**Step 6: Manual test**

To test the fallback path on Linux (even though KDBusService is available), temporarily add `-DCMAKE_DISABLE_FIND_PACKAGE_KF6DBusAddons=ON` to the cmake configure command. Then:

- Run `./build/bin/Penelope docs/glyphs.pdf`
- In another terminal: `./build/bin/Penelope docs/hershey.pdf`
- Second process should exit immediately; first window gains a new tab.

**Step 7: Commit**

```
feat: QLocalServer single-instance fallback for non-KDE/Windows builds
```
