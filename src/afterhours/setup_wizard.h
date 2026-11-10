// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QAbstractButton;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QVBoxLayout;

namespace Library {
class EmulatorDownloader;
class GameLibrary;
} // namespace Library

namespace AfterHours {

/// First-run setup, also reachable later: the player's name, the game folders, then an emulator
/// for every console the scan found (download, website or the program's location).
class SetupWizard : public QDialog {
    Q_OBJECT

public:
    enum class Page { Welcome, Games, Emulators, Done };

    SetupWizard(Library::GameLibrary& library, Library::EmulatorDownloader& downloader,
                Page start = Page::Welcome, QWidget* parent = nullptr);

private:
    void ShowPage(Page page);
    void SaveFolders();
    void FillEmulators();
    void UpdateButtons();

    Library::GameLibrary& library;
    Library::EmulatorDownloader& downloader;
    Page page{};
    QStackedWidget* pages;
    QLineEdit* name;
    QListWidget* folders;
    QLabel* scan_status;
    QWidget* emulator_rows;
    QVBoxLayout* emulator_layout;
    QLabel* summary;
    QAbstractButton* back;
    QAbstractButton* next;
    QAbstractButton* skip;
    bool folders_changed{};
};

} // namespace AfterHours
