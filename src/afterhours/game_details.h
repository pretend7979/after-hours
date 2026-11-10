// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QPersistentModelIndex>
#include <QWidget>

#include "afterhours/types.h"

class GameMetadataStore;
class QAbstractButton;
class QComboBox;
class QGridLayout;
class QLabel;
class TrailerView;

namespace AfterHours {

class CoverView;

/// An emulator a game can be set to start in.
struct EmulatorOption {
    QString id;
    QString name;
    bool installed{};
};

/// What the details panel shows about how a game starts.
struct LaunchInfo {
    QString console;  ///< "PlayStation"
    QString emulator; ///< "DuckStation"
    QString emulator_id;
    bool installed{};
    /// How to get the emulator when it isn't installed: a GitHub release, or its website.
    bool downloadable{};
    bool has_website{};
    bool has_game_settings{};            ///< emulator supports per-game settings
    QString console_emulator;            ///< the console's own choice
    std::vector<EmulatorOption> options; ///< everything the console can use
    QString game_choice;                 ///< the game's own choice, empty for the console's
};

/// A game's details over the library: art, trailer, description and Play.
class GameDetails : public QWidget {
    Q_OBJECT

public:
    GameDetails(GameMetadataStore& store, QWidget* parent = nullptr);

    void Open(const QModelIndex& index, const LaunchInfo& launch);
    /// New launch details for the open game (another emulator was chosen).
    void SetLaunch(const LaunchInfo& launch);
    void Close();
    /// Shows new details for the open game (after a lookup or an edit).
    void Refresh(u64 key);
    [[nodiscard]] bool IsOpen() const {
        return isVisible();
    }

signals:
    void PlayRequested(const QModelIndex& index);
    void GameSettingsRequested(const QModelIndex& index);
    void EditRequested(const QModelIndex& index);
    void SettingsRequested(const QString& system_id);
    /// The emulator the game plays with should be downloaded (or its website opened).
    void DownloadRequested(const QString& emulator_id);
    /// The game should start in `emulator_id` (empty: its console's emulator).
    void EmulatorChosen(const QModelIndex& index, const QString& emulator_id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void Fill();
    void LayoutPanel();

    GameMetadataStore& store;
    QPersistentModelIndex index;
    LaunchInfo launch;

    QWidget* panel;
    QLabel* eyebrow;
    QLabel* title;
    QLabel* subtitle;
    CoverView* cover;
    QLabel* video_label;
    TrailerView* trailer;
    QAbstractButton* play;
    QAbstractButton* game_settings;
    QAbstractButton* edit;
    QAbstractButton* folder;
    QLabel* status;
    QComboBox* plays_with;
    QAbstractButton* settings;
    QAbstractButton* get_emulator;
    QString emulator_id;
    QLabel* summary;
    QGridLayout* facts;
    QString video_id;
};

} // namespace AfterHours
