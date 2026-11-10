// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTreeWidget;

namespace Library {

class EmulatorDownloader;
class GameLibrary;

/// Game folders, emulator folders, and each emulator's install state with Download / Choose.
class LibraryDialog : public QDialog {
    Q_OBJECT

public:
    LibraryDialog(GameLibrary& library, EmulatorDownloader& downloader, QWidget* parent = nullptr);

private:
    void FillEmulators();
    [[nodiscard]] QString SelectedEmulator() const;

    GameLibrary& library;
    EmulatorDownloader& downloader;
    QListWidget* rom_roots;
    QListWidget* emulator_dirs;
    QTreeWidget* emulators;
    QPushButton* download;
    QPushButton* choose;
    QProgressBar* progress;
    QLabel* status;
};

} // namespace Library
