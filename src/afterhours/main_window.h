// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "afterhours/types.h"

class GameMetadataStore;
class IgdbClient;
class QAbstractButton;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QStandardItemModel;

namespace Library {
class EmulatorDownloader;
class GameLibrary;
} // namespace Library

namespace AfterHours {

class ConsoleList;
class GameDetails;
class GameFilter;
class GameGrid;
class UserChip;
struct LaunchInfo;

/// After Hours Arcade Manager: every console's games in one library, started in their emulators (Switch 2
/// games in PreTenDo Emulator).
class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void BuildModel();
    void RefreshConsoles();
    void ShowConsole(const QString& system_id);
    void UpdateIntro();
    void UpdateCount();
    void LookUp(int first, int last);
    void Play(const QModelIndex& index);
    void OpenGameSettings(const QModelIndex& index);
    void OpenDetails(const QModelIndex& index);
    /// Downloads an emulator from its GitHub releases, or opens its website.
    void DownloadEmulator(const QString& emulator_id);
    void EditInfo(const QModelIndex& index);
    void OpenConsoleSettings(const QString& system_id);
    void OpenLibrarySettings();
    void AddFolder();
    void RenameUser();
    /// Asks GitHub about the emulators After Hours downloaded and notes available updates.
    void CheckForUpdates();
    /// Asks GitHub whether a newer After Hours is out (shows the header's update button).
    /// Looks for a newer After Hours release; `manual` also says when there is none.
    void CheckForAppUpdate(bool manual = false);
    /// Downloads the newer After Hours, then replaces this one once it has closed.
    void InstallAppUpdate();
    /// The setup wizard from its page `start` (SetupWizard::Page).
    void RunSetup(int start);
    void ShowToast(const QString& text, int milliseconds);
    /// How a console's games start, or one game's when `key` is given.
    [[nodiscard]] LaunchInfo Launch(const QString& system_id, u64 key = 0) const;

    Library::GameLibrary* library;
    Library::EmulatorDownloader* downloader;
    GameMetadataStore* store;
    IgdbClient* igdb;
    QStandardItemModel* games;
    GameFilter* filter;

    QWidget* header;
    UserChip* user;
    QLineEdit* search;
    ConsoleList* consoles;
    QLabel* scan_status;
    QLabel* eyebrow;
    QLabel* heading;
    QLabel* intro;
    QLabel* console_logo;
    QAbstractButton* console_settings;
    QAbstractButton* get_emulator;
    QLabel* result_count;
    QStackedWidget* results;
    GameGrid* grid;
    QLabel* empty_title;
    QLabel* empty_text;
    QAbstractButton* empty_action;
    GameDetails* details;
    QLabel* toast;
    QAbstractButton* app_update;
    QString app_update_tag;
    QString current_system;
};

} // namespace AfterHours
