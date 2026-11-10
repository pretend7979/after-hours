// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace Library {
class EmulatorDownloader;
class GameLibrary;
} // namespace Library

namespace AfterHours {

/// One console's settings: its emulator and where that is, launch options and game folders.
class ConsoleSettings : public QDialog {
    Q_OBJECT

public:
    ConsoleSettings(const QString& system_id, Library::GameLibrary& library,
                    Library::EmulatorDownloader& downloader, QWidget* parent = nullptr);

private:
    void FillEmulators();
    void ShowEmulator();
    [[nodiscard]] QString EmulatorId() const;
    void SaveFolders();

    QString system_id;
    Library::GameLibrary& library;
    Library::EmulatorDownloader& downloader;
    QComboBox* emulator;
    QLineEdit* program;
    QPushButton* browse;
    QPushButton* download;
    QPushButton* clear_program;
    QPushButton* add_copy;
    QPushButton* add_github;
    QPushButton* remove_copy;
    QProgressBar* progress;
    QLabel* status;
    QLineEdit* args;
    QLabel* default_args;
    QListWidget* folders;
    QString installed_release; ///< Of the emulator shown, when After Hours downloaded it
};

} // namespace AfterHours
