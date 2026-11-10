// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "afterhours/emulator_downloader.h"
#include "afterhours/game_library.h"
#include "afterhours/setup_wizard.h"
#include "afterhours/theme.h"

namespace AfterHours {

namespace {
QLabel* Title(const QString& text) {
    auto* label = new QLabel(text);
    label->setFont(Theme::Font(26, QFont::DemiBold, true));
    label->setWordWrap(true);
    return label;
}

QLabel* Body(const QString& text) {
    auto* label = new QLabel(text);
    label->setFont(Theme::Font(14));
    label->setObjectName(QStringLiteral("Muted"));
    label->setWordWrap(true);
    label->setTextFormat(Qt::RichText);
    return label;
}

QLabel* Step(const QString& text) {
    auto* label = new QLabel(text);
    label->setFont(Theme::EyebrowFont(11));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, Theme::Primary);
    label->setPalette(palette);
    return label;
}

QString Count(int count, const QString& one, const QString& many) {
    return count == 1 ? one : many.arg(QLocale{}.toString(count));
}
} // Anonymous namespace

SetupWizard::SetupWizard(Library::GameLibrary& library_, Library::EmulatorDownloader& downloader_,
                         Page start, QWidget* parent)
    : QDialog{parent}, library{library_}, downloader{downloader_} {
    setWindowTitle(tr("Set up After Hours Arcade Manager"));
    setMinimumSize(860, 620);
    pages = new QStackedWidget;

    // Welcome: who is playing.
    auto* welcome = new QWidget;
    name = new QLineEdit(library.UserName());
    name->setMaxLength(32);
    name->setPlaceholderText(tr("Your name"));
    auto* welcome_layout = new QVBoxLayout(welcome);
    welcome_layout->addWidget(Step(tr("Step 1 of 3")));
    welcome_layout->addWidget(Title(tr("Welcome to After Hours Arcade Manager")));
    welcome_layout->addWidget(Body(
        tr("All your games in one library, from every console, each started in its own "
           "emulator. Switch 2 games play in PreTenDo Emulator, which comes with it.<br><br>"
           "Three quick steps: your name, where your games are, and an emulator for each "
           "console. You can change all of it later.")));
    welcome_layout->addSpacing(18);
    welcome_layout->addWidget(Body(tr("What should we call you?")));
    welcome_layout->addWidget(name);
    welcome_layout->addStretch();

    // Games: the folders to scan.
    auto* games = new QWidget;
    folders = new QListWidget;
    folders->addItems(library.RomRoots());
    auto* add_folder = new QPushButton(tr("Add folder..."));
    auto* remove_folder = new QPushButton(tr("Remove"));
    auto* folder_buttons = new QHBoxLayout;
    folder_buttons->addWidget(add_folder);
    folder_buttons->addWidget(remove_folder);
    folder_buttons->addStretch();
    auto* games_layout = new QVBoxLayout(games);
    games_layout->addWidget(Step(tr("Step 2 of 3")));
    games_layout->addWidget(Title(tr("Where are your games?")));
    games_layout->addWidget(Body(
        tr("Pick the folder that holds your games. Each console's games are found in a "
           "subfolder named after it, like <b>snes</b>, <b>psx</b>, <b>switch</b> or "
           "<b>switch2</b>, and folders inside those are searched too. A RetroBat or "
           "EmulationStation <b>roms</b> folder works as it is.<br>A folder with one console's "
           "games only can be added in that console's settings later.")));
    games_layout->addSpacing(8);
    games_layout->addWidget(folders, 1);
    games_layout->addLayout(folder_buttons);

    // Emulators: one row per console found.
    auto* emulators = new QWidget;
    scan_status = Body(QString{});
    emulator_rows = new QWidget;
    emulator_layout = new QVBoxLayout(emulator_rows);
    emulator_layout->setContentsMargins(0, 0, 8, 0);
    emulator_layout->setSpacing(8);
    auto* scroll = new QScrollArea;
    scroll->setWidget(emulator_rows);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;}"));
    auto* add_emulator_dir = new QPushButton(tr("Add emulator folder..."));
    add_emulator_dir->setToolTip(
        tr("A folder that already holds emulators, each in its own subfolder (RetroBat's "
           "emulators folder works)"));
    auto* emulator_buttons = new QHBoxLayout;
    emulator_buttons->addWidget(add_emulator_dir);
    emulator_buttons->addStretch();
    auto* emulators_layout = new QVBoxLayout(emulators);
    emulators_layout->addWidget(Step(tr("Step 3 of 3")));
    emulators_layout->addWidget(Title(tr("Emulators for your consoles")));
    emulators_layout->addWidget(Body(
        tr("Download an emulator, or show After Hours where yours is. Already have a folder "
           "of emulators? Add it and they are found by themselves. After Hours provides no "
           "BIOS, keys or firmware; some emulators need them from your own console.")));
    emulators_layout->addWidget(scan_status);
    emulators_layout->addWidget(scroll, 1);
    emulators_layout->addLayout(emulator_buttons);

    // Done.
    auto* done = new QWidget;
    summary = Body(QString{});
    auto* done_layout = new QVBoxLayout(done);
    done_layout->addWidget(Step(tr("All set")));
    done_layout->addWidget(Title(tr("Ready to play")));
    done_layout->addWidget(summary);
    done_layout->addStretch();

    pages->addWidget(welcome);
    pages->addWidget(games);
    pages->addWidget(emulators);
    pages->addWidget(done);

    skip = new QPushButton(tr("Skip setup"));
    back = new QPushButton(tr("Back"));
    next = new QPushButton(tr("Next"));
    static_cast<QPushButton*>(next)->setDefault(true);
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(skip);
    buttons->addStretch();
    buttons->addWidget(back);
    buttons->addWidget(next);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(36, 30, 36, 24);
    layout->addWidget(pages, 1);
    layout->addSpacing(12);
    layout->addLayout(buttons);

    connect(add_folder, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a games folder"));
        if (!dir.isEmpty() &&
            folders->findItems(QDir::toNativeSeparators(dir), Qt::MatchFixedString).isEmpty()) {
            folders->addItem(QDir::toNativeSeparators(dir));
            folders_changed = true;
            UpdateButtons();
        }
    });
    connect(remove_folder, &QPushButton::clicked, this, [this] {
        delete folders->currentItem();
        folders_changed = true;
        UpdateButtons();
    });
    connect(add_emulator_dir, &QPushButton::clicked, this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, tr("Choose a folder with emulators"));
        if (dir.isEmpty()) {
            return;
        }
        QStringList dirs = library.EmulatorDirs();
        if (!dirs.contains(QDir::toNativeSeparators(dir))) {
            dirs.append(QDir::toNativeSeparators(dir));
            library.SetEmulatorDirs(dirs);
        }
        FillEmulators();
    });
    connect(skip, &QPushButton::clicked, this, [this] {
        library.SetSetupDone(true);
        accept();
    });
    connect(back, &QPushButton::clicked, this,
            [this] { ShowPage(static_cast<Page>(static_cast<int>(page) - 1)); });
    connect(next, &QPushButton::clicked, this, [this] {
        switch (page) {
        case Page::Welcome:
            if (!name->text().trimmed().isEmpty()) {
                library.SetUserName(name->text());
            }
            ShowPage(Page::Games);
            break;
        case Page::Games:
            SaveFolders();
            ShowPage(Page::Emulators);
            break;
        case Page::Emulators:
            ShowPage(Page::Done);
            break;
        case Page::Done:
            library.SetSetupDone(true);
            accept();
            break;
        }
    });
    connect(&library, &Library::GameLibrary::ScanFinished, this, [this] {
        if (page == Page::Emulators) {
            FillEmulators();
        }
    });
    connect(&downloader, &Library::EmulatorDownloader::Finished, this,
            [this](const QString&, bool ok, const QString& message) {
                if (!ok) {
                    scan_status->setText(message);
                }
                // Rebuilt outside the signal: a row's own button may be what started it.
                QTimer::singleShot(0, this, &SetupWizard::FillEmulators);
            });
    ShowPage(start);
}

void SetupWizard::SaveFolders() {
    QStringList roots;
    for (int row = 0; row < folders->count(); ++row) {
        roots.append(folders->item(row)->text());
    }
    if (folders_changed || roots != library.RomRoots()) {
        library.SetRomRoots(roots);
        folders_changed = false;
    }
}

void SetupWizard::ShowPage(Page page_) {
    page = page_;
    pages->setCurrentIndex(static_cast<int>(page));
    if (page == Page::Emulators) {
        FillEmulators();
    }
    if (page == Page::Done) {
        int consoles = 0;
        int ready = 0;
        for (const Library::SystemDef& system : library.Systems()) {
            if (library.RomCount(system.id) == 0) {
                continue;
            }
            ++consoles;
            ready += library.EmulatorExe(library.ChosenEmulator(system.id).id).isEmpty() ? 0 : 1;
        }
        const int games = static_cast<int>(library.Roms().size());
        summary->setText(
            tr("%1 on %2, %3 ready to play.<br><br>Click a game for its details and trailer, or "
               "its Play button to start it. Each console's settings (the gear by its name) "
               "change its emulator, launch options and game folders. Run this setup again from "
               "<b>Emulators</b> at the top.")
                .arg(Count(games, tr("1 game"), tr("%1 games")),
                     Count(consoles, tr("1 console"), tr("%1 consoles")),
                     Count(ready, tr("1 console"), tr("%1 consoles"))));
    }
    UpdateButtons();
}

void SetupWizard::UpdateButtons() {
    back->setVisible(page != Page::Welcome);
    skip->setVisible(page != Page::Done);
    next->setText(page == Page::Done ? tr("Start playing") : tr("Next"));
}

void SetupWizard::FillEmulators() {
    while (QLayoutItem* item = emulator_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (library.Scanning()) {
        scan_status->setText(tr("Looking for games..."));
    } else {
        scan_status->clear();
    }
    int consoles = 0;
    for (const Library::SystemDef& system : library.Systems()) {
        const int count = library.RomCount(system.id);
        if (count == 0) {
            continue;
        }
        ++consoles;
        auto* row = new QFrame;
        row->setObjectName(QStringLiteral("ConsoleRow"));
        row->setStyleSheet(QStringLiteral(
            "QFrame#ConsoleRow{background:#202128;border:1px solid #33343d;border-radius:12px;}"));
        auto* logo = new QLabel;
        logo->setFixedSize(130, 34);
        if (const QString file = library.LogoFile(system.id); !file.isEmpty()) {
            logo->setPixmap(QPixmap::fromImage(
                Theme::ReadLogo(file, QSize{130, 30}, devicePixelRatioF())));
        }
        auto* title = new QLabel(system.name);
        title->setFont(Theme::Font(14, QFont::DemiBold));
        auto* games = new QLabel(Count(count, tr("1 game"), tr("%1 games")));
        games->setObjectName(QStringLiteral("Muted"));
        auto* names = new QVBoxLayout;
        names->setSpacing(0);
        names->addWidget(title);
        names->addWidget(games);

        auto* choice = new QComboBox;
        choice->setMinimumWidth(170);
        const QString chosen = library.ChosenEmulator(system.id).id;
        for (const Library::SystemEmulator& option : library.SystemEmulators(system.id)) {
            if (const Library::EmulatorDef* def = library.Emulator(option.id)) {
                choice->addItem(def->name, def->id);
                if (def->id == chosen) {
                    choice->setCurrentIndex(choice->count() - 1);
                }
            }
        }
        auto* status = new QLabel;
        status->setMinimumWidth(110);
        auto* get = new QPushButton;
        auto* locate = new QPushButton(tr("Choose program..."));

        const QString system_id = system.id;
        const auto refresh = [this, choice, status, get, locate] {
            const Library::EmulatorDef* def = library.Emulator(choice->currentData().toString());
            const bool ready = def && !library.EmulatorExe(def->id).isEmpty();
            status->setText(ready ? tr("<span style='color:#d8f077'>●</span> Ready")
                                  : tr("<span style='color:#ff8791'>●</span> Not installed"));
            get->setVisible(def && (!def->repo.isEmpty() || !def->homepage.isEmpty()));
            get->setText(ready && !def->repo.isEmpty() ? tr("Update")
                                                       : (!def->repo.isEmpty() ? tr("Download") : tr("Website")));
            get->setEnabled(!downloader.Busy());
            locate->setVisible(!ready && def != nullptr);
        };
        connect(choice, &QComboBox::currentIndexChanged, this, [this, choice, system_id, refresh] {
            library.SetChosenEmulator(system_id, choice->currentData().toString());
            refresh();
        });
        connect(get, &QPushButton::clicked, this, [this, choice, status, get] {
            const Library::EmulatorDef* def = library.Emulator(choice->currentData().toString());
            if (!def) {
                return;
            }
            if (def->repo.isEmpty()) {
                QDesktopServices::openUrl(QUrl{def->homepage});
                return;
            }
            if (downloader.Busy()) {
                return;
            }
            const bool ready = !library.EmulatorExe(def->id).isEmpty();
            status->setText(ready ? tr("Updating...") : tr("Downloading..."));
            get->setEnabled(false);
            downloader.Download(*def, library.DownloadDir());
        });
        connect(locate, &QPushButton::clicked, this, [this, choice, refresh] {
            const Library::EmulatorDef* def = library.Emulator(choice->currentData().toString());
            if (!def) {
                return;
            }
            const QString exe = QFileDialog::getOpenFileName(
                this, tr("Where is %1?").arg(def->name), QString{},
                tr("Programs (*.exe);;All files (*)"));
            if (!exe.isEmpty()) {
                library.SetEmulatorExe(def->id, exe);
                refresh();
            }
        });
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(16, 10, 16, 10);
        row_layout->setSpacing(14);
        row_layout->addWidget(logo);
        row_layout->addLayout(names, 1);
        row_layout->addWidget(choice);
        row_layout->addWidget(status);
        row_layout->addWidget(get);
        row_layout->addWidget(locate);
        emulator_layout->addWidget(row);
        // Only once the buttons are in the window: shown before that, each would flash up as a
        // window of its own.
        refresh();
    }
    if (consoles == 0 && !library.Scanning()) {
        scan_status->setText(tr("No games were found. Go back and pick the folder that holds "
                                "your consoles' folders."));
    }
    emulator_layout->addStretch();
}

} // namespace AfterHours
