// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

/// Where After Hours keeps its settings and caches.
namespace AfterHours::Paths {

/// Carries the settings folder of the app's former name (Pretendulator) over to `dir` when only
/// the old one exists: moved, or copied (leaving the old one) when it can't be moved.
inline void MigrateLegacyDir(const QString& legacy, const QString& dir) {
    if (QFileInfo::exists(dir) || !QFileInfo{legacy}.isDir() || QDir{}.rename(legacy, dir)) {
        return;
    }
    // Copied under a temporary name first, so an interrupted copy is retried next time.
    const QString partial = dir + QStringLiteral(".migrating");
    QDir{partial}.removeRecursively();
    const QDir from{legacy};
    QDirIterator it{legacy, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories};
    bool ok = QDir{}.mkpath(partial);
    while (ok && it.hasNext()) {
        const QString path = it.next();
        const QString to = partial + QLatin1Char('/') + from.relativeFilePath(path);
        ok = it.fileInfo().isDir() ? QDir{}.mkpath(to) : QFile::copy(path, to);
    }
    if (!ok || !QDir{}.rename(partial, dir)) {
        QDir{partial}.removeRecursively();
    }
}

/// A "user" folder next to the program makes it portable; otherwise %APPDATA%\After Hours
/// (the platform's data folder elsewhere).
inline QString UserDir() {
    static const QString dir = [] {
        const QString portable = QCoreApplication::applicationDirPath() + QStringLiteral("/user");
        if (QFileInfo{portable}.isDir()) {
            return portable;
        }
        QString base;
        QString name = QStringLiteral("After Hours");
        QString legacy_name = QStringLiteral("Pretendulator");
#ifdef _WIN32
        base = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
#endif
        if (base.isEmpty()) {
            base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
            name = QStringLiteral("afterhours");
            legacy_name = QStringLiteral("pretendulator");
        }
        const QString result = base + QLatin1Char('/') + name;
        MigrateLegacyDir(base + QLatin1Char('/') + legacy_name, result);
        return result;
    }();
    return dir;
}

/// Settings: the library, game metadata and art.
inline QString ConfigDir() {
    return UserDir() + QStringLiteral("/config");
}

/// Disposable files: unpacked games, the trailer player's data.
inline QString CacheDir() {
    return UserDir() + QStringLiteral("/cache");
}

/// PreTenDo Emulator's own settings file, to import its game folders on first run: in a shared
/// portable folder, else in its usual place. Empty when there is none.
inline QString EmulatorConfigFile() {
    QStringList candidates{ConfigDir() + QStringLiteral("/qt-config.ini")};
#ifdef _WIN32
    const QString appdata = qEnvironmentVariable("APPDATA");
    if (!appdata.isEmpty()) {
        candidates.append(QDir::fromNativeSeparators(appdata) +
                          QStringLiteral("/eden/config/qt-config.ini"));
    }
#endif
    for (const QString& file : candidates) {
        if (QFileInfo{file}.isFile()) {
            return file;
        }
    }
    return {};
}

} // namespace AfterHours::Paths
