// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <set>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>

#include "afterhours/emulator_downloader.h"
#include "afterhours/paths.h"
#include "afterhours/game_library.h"

namespace Library {

namespace {
QJsonObject ReadJson(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void WriteJson(const QString& path, const QJsonObject& object) {
    QFile file{path};
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument{object}.toJson());
    }
}

QStringList ToStrings(const QJsonValue& value) {
    QStringList result;
    for (const auto& item : value.toArray()) {
        result.append(item.toString());
    }
    return result;
}

QJsonArray FromStrings(const QStringList& strings) {
    QJsonArray array;
    for (const QString& string : strings) {
        array.append(string);
    }
    return array;
}

EmulatorDef ParseEmulator(const QJsonObject& object) {
    EmulatorDef def{
        .id = object.value(QStringLiteral("id")).toString(),
        .name = object.value(QStringLiteral("name")).toString(),
        .exe_names = ToStrings(object.value(QStringLiteral("exe"))),
        .args = object.value(QStringLiteral("args")).toString(),
        .game_settings_args = object.value(QStringLiteral("game_settings_args")).toString(),
        .repo = object.value(QStringLiteral("repo")).toString(),
        .asset_pattern = object.value(QStringLiteral("asset")).toString(),
        .homepage = object.value(QStringLiteral("homepage")).toString(),
        .bundled = object.value(QStringLiteral("bundled")).toBool(),
    };
    if (def.game_settings_args.isEmpty() &&
        (def.id == QStringLiteral("pretendo") || def.id == QStringLiteral("eglantine") ||
         def.base == QStringLiteral("pretendo") || def.base == QStringLiteral("eglantine"))) {
        def.game_settings_args = QStringLiteral("--game-settings \"%ROM%\"");
    }
    return def;
}

SystemDef ParseSystem(const QJsonObject& object) {
    SystemDef system{
        .id = object.value(QStringLiteral("id")).toString(),
        .name = object.value(QStringLiteral("name")).toString(),
        .short_name = object.value(QStringLiteral("short")).toString(),
        .maker = object.value(QStringLiteral("maker")).toString(),
        .year = object.value(QStringLiteral("year")).toInt(),
        .color = QColor{object.value(QStringLiteral("color")).toString()},
        .folders = ToStrings(object.value(QStringLiteral("folders"))),
        .extensions = ToStrings(object.value(QStringLiteral("ext"))),
        .file_names = ToStrings(object.value(QStringLiteral("names"))),
        .native = object.value(QStringLiteral("native")).toBool(),
        .extract = object.value(QStringLiteral("extract")).toBool(),
    };
    for (const auto& platform : object.value(QStringLiteral("igdb")).toArray()) {
        system.igdb_platforms.push_back(platform.toInt());
    }
    for (const auto& value : object.value(QStringLiteral("emulators")).toArray()) {
        if (value.isString()) {
            system.emulators.push_back(SystemEmulator{value.toString(), {}});
        } else {
            const QJsonObject emulator = value.toObject();
            system.emulators.push_back(
                SystemEmulator{emulator.value(QStringLiteral("id")).toString(),
                               emulator.value(QStringLiteral("core")).toString()});
        }
    }
    const QJsonValue logo = object.value(QStringLiteral("logo"));
    system.logo_names = logo.isString() ? QStringList{logo.toString()} : ToStrings(logo);
    if (system.short_name.isEmpty()) {
        system.short_name = system.name;
    }
    return system;
}

/// Every game of `system` under `path`. Runs on a worker thread.
void ScanFolder(const SystemDef& system, const QString& path, std::vector<RomEntry>& found) {
    if (!QFileInfo{path}.isDir()) {
        return;
    }
    QStringList filters;
    for (const QString& extension : system.extensions) {
        filters.append(QStringLiteral("*.") + extension);
    }
    // Folders with playlists list their discs through them.
    std::set<QString> playlist_dirs;
    QDirIterator playlists{path, {QStringLiteral("*.m3u")}, QDir::Files,
                           QDirIterator::Subdirectories};
    while (playlists.hasNext()) {
        playlist_dirs.insert(QFileInfo{playlists.next()}.absolutePath());
    }
    QDirIterator it{path, filters, QDir::Files, QDirIterator::Subdirectories};
    while (it.hasNext()) {
        const QString file = it.next();
        const QFileInfo info{file};
        const QString suffix = info.suffix().toLower();
        if (suffix != QStringLiteral("m3u") &&
            playlist_dirs.contains(info.absolutePath())) {
            continue;
        }
        if (!system.file_names.isEmpty() &&
            !system.file_names.contains(info.fileName().toLower()) &&
            system.extensions.size() == 1) {
            continue;
        }
        QString title = TitleFromFileName(file);
        if (system.file_names.contains(info.fileName().toLower())) {
            // eboot.bin: the game's folder names it.
            QDir dir = info.dir();
            while (dir.dirName().compare(QStringLiteral("USRDIR"),
                                         Qt::CaseInsensitive) == 0 ||
                   dir.dirName().startsWith(QStringLiteral("PS3_GAME"),
                                            Qt::CaseInsensitive)) {
                dir.cdUp();
            }
            title = TitleFromFileName(dir.dirName());
        }
        found.push_back(RomEntry{system.id, file, title, RomKey(system.id, file)});
    }
}

/// The ROM roots, by system folder name, and each system's own folders.
std::vector<RomEntry> ScanRoots(const QStringList& roots, const std::vector<SystemDef>& systems,
                                const std::map<QString, QStringList>& system_folders) {
    std::vector<RomEntry> found;
    for (const SystemDef& system : systems) {
        for (const QString& root : roots) {
            for (const QString& folder : system.folders) {
                ScanFolder(system, QDir{root}.filePath(folder), found);
            }
        }
        if (const auto it = system_folders.find(system.id); it != system_folders.end()) {
            for (const QString& folder : it->second) {
                ScanFolder(system, folder, found);
            }
        }
    }
    // File types only one console uses (.nup, .nsp, .gba...) are its games wherever they are
    // under a root; shared ones (.iso, .bin, archives) need the console's folder.
    std::map<QString, int> users;
    for (const SystemDef& system : systems) {
        for (const QString& extension : system.extensions) {
            ++users[extension.toLower()];
        }
    }
    std::map<QString, const SystemDef*> owner;
    QStringList filters;
    for (const SystemDef& system : systems) {
        for (const QString& extension : system.extensions) {
            const QString lower = extension.toLower();
            if (users[lower] == 1 && lower != QStringLiteral("zip") &&
                lower != QStringLiteral("7z") && lower != QStringLiteral("m3u")) {
                owner[lower] = &system;
                filters.append(QStringLiteral("*.") + lower);
            }
        }
    }
    for (const QString& root : roots) {
        QDirIterator it{root, filters, QDir::Files, QDirIterator::Subdirectories};
        while (it.hasNext()) {
            const QString file = it.next();
            const auto system = owner.find(QFileInfo{file}.suffix().toLower());
            if (system != owner.end()) {
                found.push_back(RomEntry{system->second->id, file, TitleFromFileName(file),
                                         RomKey(system->second->id, file)});
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const RomEntry& a, const RomEntry& b) {
        const int order = a.title.compare(b.title, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : a.key < b.key;
    });
    // The same game reached through two roots once.
    found.erase(std::unique(found.begin(), found.end(),
                            [](const RomEntry& a, const RomEntry& b) { return a.key == b.key; }),
                found.end());
    return found;
}

QString FindExe(const QString& folder, const QStringList& names, int depth = 3) {
    if (!QFileInfo{folder}.isDir() || depth < 0) {
        return {};
    }
    for (const QString& name : names) {
        const QStringList matches = QDir{folder}.entryList({name}, QDir::Files);
        if (!matches.isEmpty()) {
            return QDir{folder}.filePath(matches.first());
        }
    }
    for (const QString& sub : QDir{folder}.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QString exe = FindExe(QDir{folder}.filePath(sub), names, depth - 1); !exe.isEmpty()) {
            return exe;
        }
    }
    return {};
}
} // Anonymous namespace

u64 RomKey(const QString& system_id, const QString& path) {
    // FNV-1a over the system and the normalized path.
    u64 hash = 0xcbf29ce484222325ULL;
    const QByteArray bytes =
        (system_id + QLatin1Char(':') + QDir::cleanPath(path).toLower()).toUtf8();
    for (const char c : bytes) {
        hash ^= static_cast<u8>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash | (u64{1} << 63);
}

QString TitleFromFileName(const QString& path) {
    QString name = QFileInfo{path}.completeBaseName();
    if (name.isEmpty()) {
        name = QFileInfo{path}.fileName();
    }
    static const QRegularExpression tags{QStringLiteral(R"(\s*[\(\[][^\)\]]*[\)\]])")};
    name.remove(tags);
    name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return name.simplified();
}

GameLibrary::GameLibrary(QObject* parent) : QObject{parent} {
    directory = AfterHours::Paths::ConfigDir() + QStringLiteral("/library");
    QDir{}.mkpath(directory);
    unpack_directory = AfterHours::Paths::CacheDir() + QStringLiteral("/unpacked_games");
    LoadCatalogue();
    LoadSettings();
    LoadCache();
}

void GameLibrary::LoadCatalogue() {
    QJsonObject catalogue = ReadJson(QStringLiteral(":/library/systems.json"));
    // The user's file overrides or adds entries by id.
    const QJsonObject user = ReadJson(directory + QStringLiteral("/systems.json"));
    for (const auto& value : catalogue.value(QStringLiteral("emulators")).toArray()) {
        emulators.push_back(ParseEmulator(value.toObject()));
    }
    for (const auto& value : catalogue.value(QStringLiteral("systems")).toArray()) {
        systems.push_back(ParseSystem(value.toObject()));
    }
    for (const auto& value : user.value(QStringLiteral("emulators")).toArray()) {
        EmulatorDef emulator = ParseEmulator(value.toObject());
        const auto it = std::ranges::find(emulators, emulator.id, &EmulatorDef::id);
        if (it != emulators.end()) {
            *it = std::move(emulator);
        } else {
            emulators.push_back(std::move(emulator));
        }
    }
    for (const auto& value : user.value(QStringLiteral("systems")).toArray()) {
        SystemDef system = ParseSystem(value.toObject());
        const auto it = std::ranges::find(systems, system.id, &SystemDef::id);
        if (it != systems.end()) {
            *it = std::move(system);
        } else {
            systems.push_back(std::move(system));
        }
    }
}

void GameLibrary::LoadSettings() {
    const QJsonObject object = ReadJson(directory + QStringLiteral("/library.json"));
    rom_roots = ToStrings(object.value(QStringLiteral("rom_roots")));
    emulator_dirs = ToStrings(object.value(QStringLiteral("emulator_dirs")));
    if (emulator_dirs.isEmpty()) {
        emulator_dirs.append(directory + QStringLiteral("/emulators"));
    }
    const QJsonObject paths = object.value(QStringLiteral("emulator_paths")).toObject();
    for (auto it = paths.begin(); it != paths.end(); ++it) {
        emulator_paths[it.key()] = it.value().toString();
    }
    const QJsonObject chosen = object.value(QStringLiteral("system_emulators")).toObject();
    for (auto it = chosen.begin(); it != chosen.end(); ++it) {
        chosen_emulators[it.key()] = it.value().toString();
    }
    if (auto it = chosen_emulators.find(QStringLiteral("switch2"));
        it != chosen_emulators.end() && it->second == QStringLiteral("eglantine")) {
        it->second = QStringLiteral("pretendo");
    }
    const QJsonObject folders = object.value(QStringLiteral("system_folders")).toObject();
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        system_folders[it.key()] = ToStrings(it.value());
    }
    const QJsonObject args = object.value(QStringLiteral("system_args")).toObject();
    for (auto it = args.begin(); it != args.end(); ++it) {
        system_args[it.key()] = it.value().toString();
    }
    for (const auto& value : object.value(QStringLiteral("emulator_copies")).toArray()) {
        const QJsonObject copy = value.toObject();
        const EmulatorDef* base = Emulator(copy.value(QStringLiteral("base")).toString());
        if (!base) {
            continue;
        }
        EmulatorDef def = *base;
        def.id = copy.value(QStringLiteral("id")).toString();
        def.name = copy.value(QStringLiteral("name")).toString();
        def.base = base->id;
        def.repo = copy.value(QStringLiteral("repo")).toString();
        def.homepage.clear();
        def.bundled = false;
        emulators.push_back(std::move(def));
    }
    const QJsonObject per_game = object.value(QStringLiteral("game_emulators")).toObject();
    for (auto it = per_game.begin(); it != per_game.end(); ++it) {
        game_emulators[it.key().toULongLong(nullptr, 16)] = it.value().toString();
    }
    logo_dirs = ToStrings(object.value(QStringLiteral("logo_dirs")));
    if (logo_dirs.isEmpty()) {
        logo_dirs.append(directory + QStringLiteral("/logos"));
    }
    // Libraries set up before the setup existed count as set up.
    setup_done = object.value(QStringLiteral("setup_done")).toBool(!object.isEmpty());
    user_name = object.value(QStringLiteral("user_name")).toString();
    if (user_name.isEmpty()) {
        user_name = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", tr("Player 1")));
    }
    if (!object.contains(QStringLiteral("system_folders"))) {
        ImportEglantineFolders();
    }
}

void GameLibrary::ImportEglantineFolders() {
    const QString file = AfterHours::Paths::EmulatorConfigFile();
    if (file.isEmpty()) {
        SaveSettings();
        return;
    }
    const QSettings config{file, QSettings::IniFormat};
    QStringList folders;
    const int count = config.value(QStringLiteral("Paths/gamedirs/size")).toInt();
    for (int i = 1; i <= count; ++i) {
        const QString path =
            config.value(QStringLiteral("Paths/gamedirs/%1/path").arg(i)).toString();
        // SDMC and the NANDs hold installed content, not game files.
        if (QFileInfo{path}.isDir()) {
            folders.append(QDir::toNativeSeparators(path));
        }
    }
    // One folder can hold both: each console keeps only its own file types.
    if (!folders.isEmpty()) {
        system_folders[QStringLiteral("switch2")] = folders;
        system_folders[QStringLiteral("switch")] = folders;
    }
    SaveSettings();
}

void GameLibrary::SaveSettings() const {
    QJsonObject paths;
    for (const auto& [id, path] : emulator_paths) {
        paths.insert(id, path);
    }
    QJsonObject chosen;
    for (const auto& [system, emulator] : chosen_emulators) {
        chosen.insert(system, emulator);
    }
    QJsonObject folders;
    for (const auto& [system, list] : system_folders) {
        folders.insert(system, FromStrings(list));
    }
    QJsonObject args;
    for (const auto& [system, value] : system_args) {
        args.insert(system, value);
    }
    QJsonArray copies;
    for (const EmulatorDef& emulator : emulators) {
        if (!emulator.base.isEmpty()) {
            QJsonObject copy{{QStringLiteral("id"), emulator.id},
                             {QStringLiteral("name"), emulator.name},
                             {QStringLiteral("base"), emulator.base}};
            if (!emulator.repo.isEmpty()) {
                copy.insert(QStringLiteral("repo"), emulator.repo);
            }
            copies.append(copy);
        }
    }
    QJsonObject per_game;
    for (const auto& [key, emulator] : game_emulators) {
        per_game.insert(QString::number(key, 16), emulator);
    }
    WriteJson(directory + QStringLiteral("/library.json"),
              QJsonObject{
                  {QStringLiteral("rom_roots"), FromStrings(rom_roots)},
                  {QStringLiteral("emulator_dirs"), FromStrings(emulator_dirs)},
                  {QStringLiteral("emulator_paths"), paths},
                  {QStringLiteral("system_emulators"), chosen},
                  {QStringLiteral("system_folders"), folders},
                  {QStringLiteral("system_args"), args},
                  {QStringLiteral("emulator_copies"), copies},
                  {QStringLiteral("game_emulators"), per_game},
                  {QStringLiteral("logo_dirs"), FromStrings(logo_dirs)},
                  {QStringLiteral("user_name"), user_name},
                  {QStringLiteral("setup_done"), setup_done},
              });
}

void GameLibrary::LoadCache() {
    const QJsonObject object = ReadJson(directory + QStringLiteral("/roms.json"));
    for (const auto& value : object.value(QStringLiteral("roms")).toArray()) {
        const QJsonObject rom = value.toObject();
        const QString system = rom.value(QStringLiteral("system")).toString();
        const QString path = rom.value(QStringLiteral("path")).toString();
        roms.push_back(RomEntry{system, path, rom.value(QStringLiteral("title")).toString(),
                                RomKey(system, path)});
    }
}

void GameLibrary::SaveCache() const {
    QJsonArray array;
    for (const RomEntry& rom : roms) {
        array.append(QJsonObject{{QStringLiteral("system"), rom.system},
                                 {QStringLiteral("path"), rom.path},
                                 {QStringLiteral("title"), rom.title}});
    }
    WriteJson(directory + QStringLiteral("/roms.json"),
              QJsonObject{{QStringLiteral("roms"), array}});
}

const SystemDef* GameLibrary::System(const QString& id) const {
    const auto it = std::ranges::find(systems, id, &SystemDef::id);
    return it != systems.end() ? &*it : nullptr;
}

const EmulatorDef* GameLibrary::Emulator(const QString& id) const {
    const auto it = std::ranges::find(emulators, id, &EmulatorDef::id);
    return it != emulators.end() ? &*it : nullptr;
}

void GameLibrary::SetRomRoots(const QStringList& roots) {
    rom_roots = roots;
    SaveSettings();
    emit SettingsChanged();
    Rescan();
}

void GameLibrary::SetEmulatorDirs(const QStringList& dirs) {
    emulator_dirs = dirs;
    if (emulator_dirs.isEmpty()) {
        emulator_dirs.append(directory + QStringLiteral("/emulators"));
    }
    SaveSettings();
    emit SettingsChanged();
}

void GameLibrary::SetSetupDone(bool done) {
    setup_done = done;
    SaveSettings();
}

void GameLibrary::SetUserName(const QString& name) {
    user_name = name.trimmed();
    SaveSettings();
    emit SettingsChanged();
}

void GameLibrary::SetLogoDirs(const QStringList& dirs) {
    logo_dirs = dirs;
    if (logo_dirs.isEmpty()) {
        logo_dirs.append(directory + QStringLiteral("/logos"));
    }
    SaveSettings();
    emit SettingsChanged();
}

QString GameLibrary::LogoFile(const QString& system_id) const {
    const SystemDef* system = System(system_id);
    if (!system) {
        return {};
    }
    const QStringList names = QStringList{system->id} + system->logo_names;
    // The user's folders first, then the logos shipped next to After Hours.
    const QStringList dirs =
        logo_dirs + QStringList{QCoreApplication::applicationDirPath() + QStringLiteral("/logos")};
    for (const QString& dir : dirs) {
        for (const QString& name : names) {
            for (const char* extension : {".svg", ".png", ".webp", ".jpg"}) {
                const QString path = QDir{dir}.filePath(name + QLatin1String(extension));
                if (QFileInfo{path}.isFile()) {
                    return path;
                }
            }
        }
    }
    return {};
}

QStringList GameLibrary::SystemFolders(const QString& system_id) const {
    const auto it = system_folders.find(system_id);
    return it != system_folders.end() ? it->second : QStringList{};
}

void GameLibrary::SetSystemFolders(const QString& system_id, const QStringList& folders) {
    system_folders[system_id] = folders;
    SaveSettings();
    emit SettingsChanged();
    Rescan();
}

QString GameLibrary::SystemArgs(const QString& system_id) const {
    const auto it = system_args.find(system_id);
    return it != system_args.end() ? it->second : QString{};
}

void GameLibrary::SetSystemArgs(const QString& system_id, const QString& args) {
    if (args.trimmed().isEmpty()) {
        system_args.erase(system_id);
    } else {
        system_args[system_id] = args.trimmed();
    }
    SaveSettings();
    emit SettingsChanged();
}

QString GameLibrary::DownloadedFolder(const QString& emulator_id) const {
    const QString exe = QDir::cleanPath(EmulatorExe(emulator_id));
    for (const QString& dir : emulator_dirs) {
        const QString folder = QDir::cleanPath(QDir{dir}.filePath(emulator_id));
        if (!exe.isEmpty() && exe.startsWith(folder + QLatin1Char('/'), Qt::CaseInsensitive) &&
            !EmulatorDownloader::ReleaseMarker(folder).isEmpty()) {
            return folder;
        }
    }
    return {};
}

QString GameLibrary::DownloadDir() const {
    return emulator_dirs.first();
}

QString GameLibrary::EmulatorExe(const QString& emulator_id) const {
    if (const auto it = emulator_paths.find(emulator_id);
        it != emulator_paths.end() && QFileInfo{it->second}.isFile()) {
        return it->second;
    }
    const EmulatorDef* emulator = Emulator(emulator_id);
    if (!emulator) {
        return {};
    }
    if (!emulator->base.isEmpty() && emulator->repo.isEmpty()) {
        // A copy the user installed is only ever where they put it.
        return {};
    }
    if (emulator->bundled) {
        if (QString exe = FindExe(QCoreApplication::applicationDirPath(), emulator->exe_names, 0);
            !exe.isEmpty()) {
            return exe;
        }
    }
    // <emulator folder>/<id> first (downloads and RetroBat use the id), then by display name.
    for (const QString& dir : emulator_dirs) {
        for (const QString& name : {emulator->id, emulator->name, emulator->name.toLower()}) {
            if (QString exe = FindExe(QDir{dir}.filePath(name), emulator->exe_names);
                !exe.isEmpty()) {
                return exe;
            }
        }
    }
    return {};
}

void GameLibrary::SetEmulatorExe(const QString& emulator_id, const QString& path) {
    if (path.isEmpty()) {
        emulator_paths.erase(emulator_id);
    } else {
        emulator_paths[emulator_id] = path;
    }
    SaveSettings();
    emit SettingsChanged();
}

SystemEmulator GameLibrary::ChosenEmulator(const QString& system_id) const {
    const SystemDef* system = System(system_id);
    if (!system || system->emulators.empty()) {
        return {};
    }
    if (const auto it = chosen_emulators.find(system_id); it != chosen_emulators.end()) {
        for (const SystemEmulator& emulator : SystemEmulators(system_id)) {
            if (emulator.id == it->second) {
                return emulator;
            }
        }
    }
    const std::vector<SystemEmulator> choices = SystemEmulators(system_id);
    for (const SystemEmulator& emulator : choices) {
        if (!EmulatorExe(emulator.id).isEmpty()) {
            return emulator;
        }
    }
    return choices.front();
}

std::vector<SystemEmulator> GameLibrary::SystemEmulators(const QString& system_id) const {
    std::vector<SystemEmulator> result;
    const SystemDef* system = System(system_id);
    if (!system) {
        return result;
    }
    for (const SystemEmulator& entry : system->emulators) {
        result.push_back(entry);
        for (const EmulatorDef& emulator : emulators) {
            if (emulator.base == entry.id) {
                result.push_back(SystemEmulator{emulator.id, entry.core});
            }
        }
    }
    return result;
}

QString GameLibrary::AddEmulatorCopy(const QString& base_id, const QString& name,
                                     const QString& exe, const QString& repo) {
    const EmulatorDef* base = Emulator(base_id);
    if (!base) {
        return {};
    }
    // Copies of copies are copies of the original.
    const EmulatorDef original = base->base.isEmpty() ? *base : *Emulator(base->base);
    QString id;
    for (int n = 2; id.isEmpty() || Emulator(id); ++n) {
        id = QStringLiteral("%1-copy%2").arg(original.id).arg(n);
    }
    EmulatorDef def = original;
    def.id = id;
    def.name = name.trimmed().isEmpty() ? original.name : name.trimmed();
    def.base = original.id;
    def.repo = repo;
    def.homepage.clear();
    def.bundled = false;
    emulators.push_back(std::move(def));
    if (!exe.isEmpty()) {
        emulator_paths[id] = exe;
    }
    SaveSettings();
    emit SettingsChanged();
    return id;
}

void GameLibrary::RemoveEmulatorCopy(const QString& emulator_id) {
    const auto it = std::ranges::find(emulators, emulator_id, &EmulatorDef::id);
    if (it == emulators.end() || it->base.isEmpty()) {
        return;
    }
    emulators.erase(it);
    emulator_paths.erase(emulator_id);
    std::erase_if(chosen_emulators, [&](const auto& entry) { return entry.second == emulator_id; });
    std::erase_if(game_emulators, [&](const auto& entry) { return entry.second == emulator_id; });
    SaveSettings();
    emit SettingsChanged();
}

QString GameLibrary::GameEmulatorChoice(u64 key) const {
    const auto it = game_emulators.find(key);
    return it != game_emulators.end() ? it->second : QString{};
}

void GameLibrary::SetGameEmulator(u64 key, const QString& emulator_id) {
    if (emulator_id.isEmpty()) {
        game_emulators.erase(key);
    } else {
        game_emulators[key] = emulator_id;
    }
    SaveSettings();
    emit SettingsChanged();
}

SystemEmulator GameLibrary::GameEmulator(const RomEntry& rom) const {
    if (const QString own = GameEmulatorChoice(rom.key); !own.isEmpty()) {
        for (const SystemEmulator& emulator : SystemEmulators(rom.system)) {
            if (emulator.id == own) {
                return emulator;
            }
        }
    }
    return ChosenEmulator(rom.system);
}

void GameLibrary::SetChosenEmulator(const QString& system_id, const QString& emulator_id) {
    chosen_emulators[system_id] = emulator_id;
    SaveSettings();
    emit SettingsChanged();
}

int GameLibrary::RomCount(const QString& system_id) const {
    return static_cast<int>(
        std::ranges::count(roms, system_id, &RomEntry::system));
}

const RomEntry* GameLibrary::Rom(u64 key) const {
    const auto it = std::ranges::find(roms, key, &RomEntry::key);
    return it != roms.end() ? &*it : nullptr;
}

void GameLibrary::Rescan() {
    if (scanning) {
        rescan_pending = true;
        return;
    }
    scanning = true;
    auto* watcher = new QFutureWatcher<std::vector<RomEntry>>(this);
    connect(watcher, &QFutureWatcher<std::vector<RomEntry>>::finished, this, [this, watcher] {
        roms = watcher->result();
        watcher->deleteLater();
        scanning = false;
        SaveCache();
        emit ScanFinished();
        if (rescan_pending) {
            rescan_pending = false;
            Rescan();
        }
    });
    watcher->setFuture(QtConcurrent::run(ScanRoots, rom_roots, systems, system_folders));
    emit ScanStarted();
}

QString GameLibrary::MainFile(const QString& folder) {
    static const QStringList preference{
        QStringLiteral("m3u"), QStringLiteral("cue"),  QStringLiteral("gdi"),
        QStringLiteral("chd"), QStringLiteral("pbp"),  QStringLiteral("iso"),
        QStringLiteral("cso"), QStringLiteral("cdi"),  QStringLiteral("rvz"),
        QStringLiteral("wbfs"), QStringLiteral("gcz"), QStringLiteral("ciso"),
        QStringLiteral("gcm"), QStringLiteral("img"),  QStringLiteral("bin"),
    };
    QStringList files;
    QDirIterator it{folder, QDir::Files, QDirIterator::Subdirectories};
    while (it.hasNext()) {
        files.append(it.next());
    }
    for (const QString& extension : preference) {
        for (const QString& file : files) {
            if (QFileInfo{file}.suffix().compare(extension, Qt::CaseInsensitive) == 0) {
                return file;
            }
        }
    }
    // Otherwise the largest file.
    QString largest;
    qint64 size = -1;
    for (const QString& file : files) {
        if (const qint64 length = QFileInfo{file}.size(); length > size) {
            largest = file;
            size = length;
        }
    }
    return largest;
}

void GameLibrary::PruneUnpacked(const QString& keep) const {
    constexpr int Kept = 4;
    QFileInfoList folders = QDir{unpack_directory}.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    int kept = 0;
    for (const QFileInfo& folder : folders) {
        if (folder.absoluteFilePath() == QFileInfo{keep}.absoluteFilePath() || kept++ < Kept) {
            continue;
        }
        QDir{folder.absoluteFilePath()}.removeRecursively();
    }
}

void GameLibrary::Launch(const RomEntry& rom) {
    const SystemDef* system = System(rom.system);
    const QString suffix = QFileInfo{rom.path}.suffix().toLower();
    if (!system || !system->extract ||
        (suffix != QStringLiteral("zip") && suffix != QStringLiteral("7z"))) {
        StartEmulator(rom, rom.path);
        return;
    }
    // Unpacked once, then reused while among the last few played.
    const QString folder =
        QDir{unpack_directory}.filePath(QString::number(rom.key, 16));
    if (const QString file = MainFile(folder); !file.isEmpty()) {
        StartEmulator(rom, file);
        return;
    }
    if (unpacking) {
        emit LaunchFailed(tr("Another game is still being unpacked."));
        return;
    }
    unpacking = true;
    QDir{folder}.removeRecursively();
    QDir{}.mkpath(folder);
    emit Preparing(rom.title);
#ifdef _WIN32
    const QString tar = QStringLiteral("C:/Windows/System32/tar.exe");
#else
    const QString tar = QStringLiteral("tar");
#endif
    auto* process = new QProcess(this);
    connect(process, &QProcess::finished, this,
            [this, process, rom, folder](int code, QProcess::ExitStatus status) {
                process->deleteLater();
                unpacking = false;
                const QString file = MainFile(folder);
                if (status != QProcess::NormalExit || code != 0 || file.isEmpty()) {
                    QDir{folder}.removeRecursively();
                    emit LaunchFailed(tr("Could not unpack %1.").arg(rom.title));
                    return;
                }
                PruneUnpacked(folder);
                StartEmulator(rom, file);
            });
    process->start(tar, {QStringLiteral("-xf"), rom.path, QStringLiteral("-C"), folder});
}

void GameLibrary::StartEmulator(const RomEntry& rom, const QString& file) {
    const SystemEmulator choice = GameEmulator(rom);
    const EmulatorDef* emulator = Emulator(choice.id);
    if (!emulator) {
        emit LaunchFailed(tr("No emulator is set for this system."));
        return;
    }
    const QString exe = EmulatorExe(emulator->id);
    if (exe.isEmpty()) {
        emit LaunchFailed(
            tr("%1 is not installed. Download it or choose its program in the console's settings.")
                .arg(emulator->name));
        return;
    }
    const QFileInfo rom_info{file};
    const QString emulator_dir = QFileInfo{exe}.absolutePath();
    QString args = SystemArgs(rom.system);
    if (args.isEmpty()) {
        args = emulator->args;
    }
    args.replace(QStringLiteral("%ROMDIR%"), QDir::toNativeSeparators(rom_info.absolutePath()));
    args.replace(QStringLiteral("%ROMNAME%"), rom_info.completeBaseName());
    args.replace(QStringLiteral("%ROM%"), QDir::toNativeSeparators(file));
    args.replace(QStringLiteral("%EMUDIR%"), emulator_dir);
    args.replace(QStringLiteral("%CORE%"), choice.core);
    QProcess process;
    process.setProgram(exe);
    process.setWorkingDirectory(emulator_dir);
#ifdef _WIN32
    // Quoted arguments survive as written (Windows command line).
    process.setNativeArguments(args);
#else
    process.setArguments(QProcess::splitCommand(args));
#endif
    if (!process.startDetached()) {
        emit LaunchFailed(tr("Could not start %1.").arg(emulator->name));
        return;
    }
    emit LaunchStarted(rom.title);
}

bool GameLibrary::SupportsGameSettings(const QString& emulator_id) const {
    const EmulatorDef* emulator = Emulator(emulator_id);
    if (!emulator) {
        return false;
    }
    return !emulator->game_settings_args.isEmpty();
}

bool GameLibrary::SupportsGameSettings(const RomEntry& rom) const {
    const SystemEmulator choice = GameEmulator(rom);
    return SupportsGameSettings(choice.id);
}

void GameLibrary::OpenGameSettings(const RomEntry& rom) {
    const SystemEmulator choice = GameEmulator(rom);
    const EmulatorDef* emulator = Emulator(choice.id);
    if (!emulator) {
        emit LaunchFailed(tr("No emulator is set for this system."));
        return;
    }
    const QString exe = EmulatorExe(emulator->id);
    if (exe.isEmpty()) {
        emit LaunchFailed(
            tr("%1 is not installed. Download it or choose its program in the console's settings.")
                .arg(emulator->name));
        return;
    }
    QString args = emulator->game_settings_args;
    if (args.isEmpty()) {
        emit LaunchFailed(tr("%1 does not support per-game settings.").arg(emulator->name));
        return;
    }
    const QFileInfo rom_info{rom.path};
    const QString emulator_dir = QFileInfo{exe}.absolutePath();
    args.replace(QStringLiteral("%ROMDIR%"), QDir::toNativeSeparators(rom_info.absolutePath()));
    args.replace(QStringLiteral("%ROMNAME%"), rom_info.completeBaseName());
    args.replace(QStringLiteral("%ROM%"), QDir::toNativeSeparators(rom.path));
    args.replace(QStringLiteral("%EMUDIR%"), emulator_dir);
    args.replace(QStringLiteral("%CORE%"), choice.core);
    QProcess process;
    process.setProgram(exe);
    process.setWorkingDirectory(emulator_dir);
#ifdef _WIN32
    process.setNativeArguments(args);
#else
    process.setArguments(QProcess::splitCommand(args));
#endif
    if (!process.startDetached()) {
        emit LaunchFailed(tr("Could not start %1.").arg(emulator->name));
        return;
    }
}

} // namespace Library
