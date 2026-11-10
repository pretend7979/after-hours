// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <vector>

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>

#include "afterhours/types.h"

namespace Library {

/// An emulator the library can launch, and where to download it.
struct EmulatorDef {
    QString id;
    QString name;
    QStringList exe_names; ///< File names or wildcards, in order of preference
    QString args;          ///< %ROM% %ROMDIR% %ROMNAME% %EMUDIR% %CORE%
    QString game_settings_args; ///< %ROM% %ROMDIR% %ROMNAME% %EMUDIR% %CORE%
    QString repo;          ///< GitHub "owner/name" with Windows release builds, or empty
    QString asset_pattern; ///< Regular expression for the release asset
    QString homepage;      ///< Where to get it when there is no repo
    bool bundled{};        ///< Ships next to After Hours (PreTenDo Emulator)
    QString base;          ///< For the user's extra copies: the emulator they are a copy of
};

/// An emulator a system can use; libretro cores name theirs.
struct SystemEmulator {
    QString id;
    QString core;
};

struct SystemDef {
    QString id;
    QString name;
    QString short_name;
    QString maker;
    int year{};
    QColor color;
    std::vector<int> igdb_platforms;
    QStringList folders;    ///< Folder names under a ROM root (RetroBat's, among others)
    QStringList extensions; ///< Without the dot, lowercase
    QStringList file_names; ///< Exact file names that are games (eboot.bin)
    std::vector<SystemEmulator> emulators;
    QStringList logo_names; ///< Other names for its logo file besides the id
    bool native{}; ///< Plays in our own emulator (PreTenDo Emulator)
    bool extract{}; ///< zip/7z games are unpacked before launch (disc emulators can't read them)
};

struct RomEntry {
    QString system;
    QString path;
    QString title;
    u64 key{};
};

/// The systems catalogue, the user's ROM and emulator folders, the scanned ROMs, and launching.
/// Lives on the GUI thread; scans run on a worker and report through ScanFinished.
class GameLibrary : public QObject {
    Q_OBJECT

public:
    explicit GameLibrary(QObject* parent = nullptr);

    [[nodiscard]] const std::vector<SystemDef>& Systems() const {
        return systems;
    }
    [[nodiscard]] const SystemDef* System(const QString& id) const;
    [[nodiscard]] const EmulatorDef* Emulator(const QString& id) const;
    [[nodiscard]] const std::vector<EmulatorDef>& Emulators() const {
        return emulators;
    }

    [[nodiscard]] QStringList RomRoots() const {
        return rom_roots;
    }
    void SetRomRoots(const QStringList& roots);
    [[nodiscard]] QStringList EmulatorDirs() const {
        return emulator_dirs;
    }
    void SetEmulatorDirs(const QStringList& dirs);
    /// Where downloads go (the first emulator folder).
    [[nodiscard]] QString DownloadDir() const;
    /// The folder After Hours downloaded `emulator_id` into, when that is the copy in use.
    [[nodiscard]] QString DownloadedFolder(const QString& emulator_id) const;

    /// The emulator's executable: a chosen path, else found in the emulator folders.
    [[nodiscard]] QString EmulatorExe(const QString& emulator_id) const;
    void SetEmulatorExe(const QString& emulator_id, const QString& path);

    /// Whether the first-run setup was finished or skipped.
    [[nodiscard]] bool SetupDone() const {
        return setup_done;
    }
    void SetSetupDone(bool done);

    /// The name shown at the top left.
    [[nodiscard]] QString UserName() const {
        return user_name;
    }
    void SetUserName(const QString& name);

    /// Folders searched for console logos (<id>.svg or .png), before the bundled "logos" folder.
    [[nodiscard]] QStringList LogoDirs() const {
        return logo_dirs;
    }
    void SetLogoDirs(const QStringList& dirs);
    [[nodiscard]] QString LogoFile(const QString& system_id) const;

    /// Folders holding only this console's games, scanned whatever their names.
    [[nodiscard]] QStringList SystemFolders(const QString& system_id) const;
    void SetSystemFolders(const QString& system_id, const QStringList& folders);
    /// Launch arguments for this console; empty uses its emulator's.
    [[nodiscard]] QString SystemArgs(const QString& system_id) const;
    void SetSystemArgs(const QString& system_id, const QString& args);

    /// The emulators a system can use: its catalogue entries, each followed by the user's
    /// copies of it.
    [[nodiscard]] std::vector<SystemEmulator> SystemEmulators(const QString& system_id) const;

    /// Adds a copy of `base_id` (another build, another version) and returns its id: one the user
    /// installed (`exe`), or one downloaded and updated from the GitHub repository `repo`
    /// ("owner/name"), such as a fork.
    QString AddEmulatorCopy(const QString& base_id, const QString& name, const QString& exe,
                            const QString& repo = {});
    void RemoveEmulatorCopy(const QString& emulator_id);

    /// The emulator a system launches with: the user's choice, else the first one installed,
    /// else the first listed.
    [[nodiscard]] SystemEmulator ChosenEmulator(const QString& system_id) const;
    void SetChosenEmulator(const QString& system_id, const QString& emulator_id);

    /// A game's own emulator, empty when it uses its console's.
    [[nodiscard]] QString GameEmulatorChoice(u64 key) const;
    void SetGameEmulator(u64 key, const QString& emulator_id);
    /// The emulator `rom` starts in: its own choice, else its console's.
    [[nodiscard]] SystemEmulator GameEmulator(const RomEntry& rom) const;

    [[nodiscard]] const std::vector<RomEntry>& Roms() const {
        return roms;
    }
    [[nodiscard]] int RomCount(const QString& system_id) const;
    [[nodiscard]] const RomEntry* Rom(u64 key) const;

    /// Rescans the ROM roots in the background.
    void Rescan();
    [[nodiscard]] bool Scanning() const {
        return scanning;
    }

    /// Starts `rom` with its system's emulator, unpacking it first when its system needs that.
    /// Ends with LaunchStarted or LaunchFailed.
    void Launch(const RomEntry& rom);

    /// Whether an emulator supports opening a per-game settings dialog.
    [[nodiscard]] bool SupportsGameSettings(const QString& emulator_id) const;
    [[nodiscard]] bool SupportsGameSettings(const RomEntry& rom) const;

    /// Opens the per-game settings dialog for `rom` in its emulator.
    void OpenGameSettings(const RomEntry& rom);

signals:
    void ScanStarted();
    void ScanFinished();
    void SettingsChanged();
    /// An archive is being unpacked for `title`.
    void Preparing(const QString& title);
    void LaunchStarted(const QString& title);
    void LaunchFailed(const QString& error);

private:
    void LoadCatalogue();
    void LoadSettings();
    /// The Switch 2 emulator's game folders become the Switch 2 and Switch consoles' on first run.
    void ImportEglantineFolders();
    void SaveSettings() const;
    void LoadCache();
    void SaveCache() const;
    void StartEmulator(const RomEntry& rom, const QString& file);
    /// The playable file in an unpacked archive (a playlist or cue sheet before its tracks).
    [[nodiscard]] static QString MainFile(const QString& folder);
    /// Keeps the few most recently unpacked games, besides `keep`.
    void PruneUnpacked(const QString& keep) const;

    std::vector<SystemDef> systems;
    std::vector<EmulatorDef> emulators;
    QStringList rom_roots;
    QStringList emulator_dirs;
    std::map<QString, QString> emulator_paths;
    std::map<QString, QString> chosen_emulators;
    std::map<QString, QStringList> system_folders;
    std::map<QString, QString> system_args;
    std::map<u64, QString> game_emulators;
    QStringList logo_dirs;
    QString user_name;
    bool setup_done{};
    std::vector<RomEntry> roms;
    bool scanning{};
    bool rescan_pending{};
    QString directory;
    QString unpack_directory;
    bool unpacking{};
};

/// A stable metadata key for a ROM (high bit set, so it never equals a Switch title id).
[[nodiscard]] u64 RomKey(const QString& system_id, const QString& path);
/// A readable title from a ROM file name: tags like "(USA)" and "[!]" removed.
[[nodiscard]] QString TitleFromFileName(const QString& path);

} // namespace Library
