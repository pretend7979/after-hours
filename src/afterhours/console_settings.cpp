// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QListWidget>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

#include "afterhours/console_settings.h"
#include "afterhours/emulator_downloader.h"
#include "afterhours/game_library.h"
#include "afterhours/theme.h"

namespace AfterHours {

namespace {
/// "owner/name" from a GitHub link (the repository, its releases page or one release) or from
/// "owner/name" itself; empty when it isn't one.
QString GitHubRepo(const QString& link) {
    const QString text = link.trimmed();
    static const QRegularExpression url{QStringLiteral(
        "^(?:https?://)?(?:www\\.)?github\\.com/([A-Za-z0-9-]+)/([A-Za-z0-9._-]+?)(?:\\.git)?"
        "(?:[/?#].*)?$")};
    static const QRegularExpression short_form{
        QStringLiteral("^([A-Za-z0-9-]+)/([A-Za-z0-9._-]+?)(?:\\.git)?$")};
    QRegularExpressionMatch match = url.match(text);
    if (!match.hasMatch()) {
        match = short_form.match(text);
    }
    return match.hasMatch() ? match.captured(1) + QLatin1Char('/') + match.captured(2)
                            : QString{};
}
} // Anonymous namespace

ConsoleSettings::ConsoleSettings(const QString& system_id_, Library::GameLibrary& library_,
                                 Library::EmulatorDownloader& downloader_, QWidget* parent)
    : QDialog{parent}, system_id{system_id_}, library{library_}, downloader{downloader_} {
    const Library::SystemDef* system = library.System(system_id);
    setWindowTitle(tr("%1 settings").arg(system ? system->name : system_id));
    setMinimumWidth(640);

    // The console's logo and name.
    auto* logo = new QLabel;
    const QString logo_file = library.LogoFile(system_id);
    if (!logo_file.isEmpty()) {
        logo->setPixmap(
            QPixmap::fromImage(Theme::ReadLogo(logo_file, QSize{180, 52}, devicePixelRatioF())));
    }
    auto* name = new QLabel(system ? system->name : system_id);
    name->setFont(Theme::Font(22, QFont::DemiBold, true));
    auto* subtitle = new QLabel(system ? tr("%1 · %2").arg(system->maker).arg(system->year)
                                       : QString{});
    subtitle->setObjectName(QStringLiteral("Muted"));
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    titles->addWidget(name);
    titles->addWidget(subtitle);
    auto* header = new QHBoxLayout;
    header->setSpacing(18);
    if (!logo_file.isEmpty()) {
        header->addWidget(logo);
    }
    header->addLayout(titles, 1);

    // The emulator.
    emulator = new QComboBox;
    program = new QLineEdit;
    program->setReadOnly(true);
    program->setPlaceholderText(tr("Not installed"));
    browse = new QPushButton(tr("Choose program..."));
    download = new QPushButton(tr("Download"));
    clear_program = new QPushButton(tr("Find automatically"));
    add_copy = new QPushButton(tr("Add another copy..."));
    add_copy->setToolTip(tr("Use another build or version of this emulator, for some games or "
                            "for the whole console"));
    add_github = new QPushButton(tr("Add from GitHub..."));
    add_github->setToolTip(tr("Track a fork or another build from its GitHub releases: it is "
                              "downloaded and updated like the others"));
    remove_copy = new QPushButton(tr("Remove copy"));
    progress = new QProgressBar;
    progress->setVisible(false);
    progress->setTextVisible(false);
    progress->setFixedHeight(6);
    status = new QLabel;
    status->setObjectName(QStringLiteral("Muted"));
    status->setWordWrap(true);
    status->setOpenExternalLinks(true);
    auto* program_buttons = new QHBoxLayout;
    program_buttons->addWidget(download);
    program_buttons->addWidget(browse);
    program_buttons->addWidget(clear_program);
    program_buttons->addStretch();
    auto* copy_buttons = new QHBoxLayout;
    copy_buttons->addWidget(add_copy);
    copy_buttons->addWidget(add_github);
    copy_buttons->addWidget(remove_copy);
    copy_buttons->addStretch();

    args = new QLineEdit(library.SystemArgs(system_id));
    default_args = new QLabel;
    default_args->setObjectName(QStringLiteral("Muted"));
    default_args->setWordWrap(true);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->addRow(tr("Emulator"), emulator);
    form->addRow(QString{}, copy_buttons);
    form->addRow(tr("Program"), program);
    form->addRow(QString{}, program_buttons);
    form->addRow(QString{}, progress);
    form->addRow(QString{}, status);
    form->addRow(tr("Launch options"), args);
    form->addRow(QString{}, default_args);

    // Folders that hold only this console's games.
    auto* folders_title = new QLabel(tr("Game folders"));
    folders_title->setFont(Theme::Font(15, QFont::DemiBold, true));
    auto* folders_help = new QLabel(
        tr("Folders with only %1 games, searched with their subfolders. Folders named after the "
           "console (like \"%2\") inside your library folders are found without adding them.")
            .arg(system ? system->short_name : system_id)
            .arg(system && !system->folders.isEmpty() ? system->folders.first() : system_id));
    folders_help->setObjectName(QStringLiteral("Muted"));
    folders_help->setWordWrap(true);
    folders = new QListWidget;
    folders->setMinimumHeight(110);
    folders->addItems(library.SystemFolders(system_id));
    auto* add_folder = new QPushButton(tr("Add folder..."));
    auto* remove_folder = new QPushButton(tr("Remove"));
    auto* folder_buttons = new QHBoxLayout;
    folder_buttons->addWidget(add_folder);
    folder_buttons->addWidget(remove_folder);
    folder_buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 24, 26, 20);
    layout->setSpacing(12);
    layout->addLayout(header);
    layout->addSpacing(10);
    layout->addLayout(form);
    layout->addSpacing(10);
    layout->addWidget(folders_title);
    layout->addWidget(folders_help);
    layout->addWidget(folders);
    layout->addLayout(folder_buttons);

    if (system && system->native) {
        // Graphics, controls, keys and firmware live in the emulator itself.
        auto* eglantine = new QPushButton(tr("Open PreTenDo Emulator settings..."));
        auto* eglantine_help = new QLabel(
            tr("Graphics, controls, keys, firmware and profiles are set in PreTenDo Emulator."));
        eglantine_help->setObjectName(QStringLiteral("Muted"));
        auto* row = new QHBoxLayout;
        row->addWidget(eglantine);
        row->addWidget(eglantine_help, 1);
        layout->addSpacing(6);
        layout->addLayout(row);
        connect(eglantine, &QPushButton::clicked, this, [this] {
            const QString exe = library.EmulatorExe(EmulatorId());
            if (exe.isEmpty() ||
                !QProcess::startDetached(exe, {}, QFileInfo{exe}.absolutePath())) {
                status->setText(tr("PreTenDo Emulator could not be started."));
            }
        });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addSpacing(6);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::finished, this,
            [this](int) { library.SetSystemArgs(system_id, args->text()); });
    connect(emulator, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!EmulatorId().isEmpty()) {
            library.SetChosenEmulator(system_id, EmulatorId());
        }
        ShowEmulator();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString exe = QFileDialog::getOpenFileName(
            this, tr("Choose %1").arg(emulator->currentText()), QString{},
            tr("Programs (*.exe);;All files (*)"));
        if (!exe.isEmpty()) {
            library.SetEmulatorExe(EmulatorId(), exe);
            ShowEmulator();
        }
    });
    connect(add_copy, &QPushButton::clicked, this, [this] {
        const Library::EmulatorDef* def = library.Emulator(EmulatorId());
        if (!def) {
            return;
        }
        const Library::EmulatorDef* original =
            def->base.isEmpty() ? def : library.Emulator(def->base);
        bool ok = false;
        const QString copy_name = QInputDialog::getText(
            this, tr("Another copy of %1").arg(original->name),
            tr("Name for this copy (for example \"%1 - OoT build\"):").arg(original->name),
            QLineEdit::Normal, tr("%1 (2)").arg(original->name), &ok);
        if (!ok) {
            return;
        }
        const QString exe = QFileDialog::getOpenFileName(
            this, tr("Choose the program of %1").arg(copy_name), QString{},
            tr("Programs (*.exe);;All files (*)"));
        if (exe.isEmpty()) {
            return;
        }
        const QString id = library.AddEmulatorCopy(original->id, copy_name, exe);
        FillEmulators();
        if (const int row = emulator->findData(id); row >= 0) {
            emulator->setCurrentIndex(row);
        }
    });
    connect(add_github, &QPushButton::clicked, this, [this] {
        const Library::EmulatorDef* def = library.Emulator(EmulatorId());
        if (!def) {
            return;
        }
        const Library::EmulatorDef* original =
            def->base.isEmpty() ? def : library.Emulator(def->base);
        bool ok = false;
        const QString link = QInputDialog::getText(
            this, tr("Add %1 from GitHub").arg(original->name),
            tr("The GitHub repository, or its releases page, of the build to track\n"
               "(for example https://github.com/owner/name/releases):"),
            QLineEdit::Normal, QString{}, &ok);
        if (!ok) {
            return;
        }
        const QString repo = GitHubRepo(link);
        if (repo.isEmpty()) {
            QMessageBox::warning(this, tr("Add from GitHub"),
                                 tr("\"%1\" isn't a GitHub repository link.").arg(link.trimmed()));
            return;
        }
        const QString copy_name = QInputDialog::getText(
            this, tr("Add %1 from GitHub").arg(original->name), tr("Name for this build:"),
            QLineEdit::Normal, repo.section(QLatin1Char('/'), 1), &ok);
        if (!ok) {
            return;
        }
        const QString id = library.AddEmulatorCopy(original->id, copy_name, {}, repo);
        FillEmulators();
        if (const int row = emulator->findData(id); row >= 0) {
            emulator->setCurrentIndex(row);
        }
    });
    connect(remove_copy, &QPushButton::clicked, this, [this] {
        const Library::EmulatorDef* def = library.Emulator(EmulatorId());
        if (!def || def->base.isEmpty()) {
            return;
        }
        if (QMessageBox::question(this, tr("Remove copy"),
                                  tr("Remove \"%1\" from After Hours? Its files stay where "
                                     "they are; games set to use it go back to the console's "
                                     "emulator.")
                                      .arg(def->name)) != QMessageBox::Yes) {
            return;
        }
        library.RemoveEmulatorCopy(def->id);
        FillEmulators();
    });
    connect(clear_program, &QPushButton::clicked, this, [this] {
        library.SetEmulatorExe(EmulatorId(), {});
        ShowEmulator();
    });
    connect(download, &QPushButton::clicked, this, [this] {
        const Library::EmulatorDef* def = library.Emulator(EmulatorId());
        if (!def || downloader.Busy()) {
            return;
        }
        if (def->repo.isEmpty()) {
            QDesktopServices::openUrl(QUrl{def->homepage});
            return;
        }
        progress->setRange(0, 0);
        progress->setVisible(true);
        download->setEnabled(false);
        const bool ready = !library.EmulatorExe(def->id).isEmpty();
        status->setText(ready ? tr("Updating %1 from GitHub...").arg(def->name)
                              : tr("Downloading the latest %1 release from GitHub...").arg(def->name));
        downloader.Download(*def, library.DownloadDir());
    });
    connect(&downloader, &Library::EmulatorDownloader::Progress, this,
            [this](const QString& id, qint64 received, qint64 total) {
                if (id != EmulatorId() || total <= 0) {
                    return;
                }
                progress->setRange(0, 1000);
                progress->setValue(static_cast<int>(received * 1000 / total));
            });
    connect(&downloader, &Library::EmulatorDownloader::LatestRelease, this,
            [this](const QString& id, const QString& tag) {
                const Library::EmulatorDef* def = library.Emulator(id);
                if (id != EmulatorId() || !def) {
                    return;
                }
                if (tag.isEmpty()) {
                    if (!def->base.isEmpty()) {
                        status->setText(tr("No Windows build was found in the recent releases "
                                           "of github.com/%1.")
                                            .arg(def->repo));
                    }
                    return;
                }
                if (library.EmulatorExe(id).isEmpty()) {
                    status->setText(tr("Download installs %1, the latest release of "
                                       "github.com/%2.")
                                        .arg(tag, def->repo));
                    return;
                }
                if (tag == installed_release) {
                    status->setText(tr("Up to date (%1).").arg(tag));
                    download->setText(tr("Reinstall"));
                } else if (installed_release.isEmpty()) {
                    status->setText(tr("Ready to play. The latest release is %1; its version "
                                       "here is unknown (not downloaded by After Hours).")
                                        .arg(tag));
                    download->setText(tr("Install %1").arg(tag));
                } else {
                    status->setText(tr("<span style='color:#d8f077'>Update available: %1</span> "
                                       "(you have %2).")
                                        .arg(tag, installed_release));
                    download->setText(tr("Update to %1").arg(tag));
                }
            });
    connect(&downloader, &Library::EmulatorDownloader::Finished, this,
            [this](const QString& id, bool ok, const QString& message) {
                progress->setVisible(false);
                download->setEnabled(true);
                if (id == EmulatorId()) {
                    ShowEmulator();
                    if (!ok) {
                        status->setText(message);
                    }
                }
            });
    connect(add_folder, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a game folder"));
        if (!dir.isEmpty() && folders->findItems(QDir::toNativeSeparators(dir),
                                                 Qt::MatchFixedString).isEmpty()) {
            folders->addItem(QDir::toNativeSeparators(dir));
            SaveFolders();
        }
    });
    connect(remove_folder, &QPushButton::clicked, this, [this] {
        delete folders->currentItem();
        SaveFolders();
    });

    FillEmulators();
}

QString ConsoleSettings::EmulatorId() const {
    return emulator->currentData().toString();
}

void ConsoleSettings::FillEmulators() {
    const Library::SystemDef* system = library.System(system_id);
    const QString chosen = library.ChosenEmulator(system_id).id;
    QSignalBlocker block{emulator};
    emulator->clear();
    if (system) {
        for (const Library::SystemEmulator& choice : library.SystemEmulators(system_id)) {
            const Library::EmulatorDef* def = library.Emulator(choice.id);
            if (!def) {
                continue;
            }
            QString label = def->name;
            if (!choice.core.isEmpty()) {
                label += tr(" (%1 core)").arg(choice.core);
            }
            if (!library.EmulatorExe(def->id).isEmpty()) {
                label += tr("  ✓");
            }
            emulator->addItem(label, def->id);
            if (def->id == chosen) {
                emulator->setCurrentIndex(emulator->count() - 1);
            }
        }
    }
    ShowEmulator();
}

void ConsoleSettings::ShowEmulator() {
    installed_release.clear();
    const Library::EmulatorDef* def = library.Emulator(EmulatorId());
    const QString exe = def ? library.EmulatorExe(def->id) : QString{};
    program->setText(QDir::toNativeSeparators(exe));
    browse->setEnabled(def != nullptr);
    const bool copy = def && !def->base.isEmpty();
    const bool ready = !exe.isEmpty();
    clear_program->setVisible(def && !copy);
    remove_copy->setVisible(copy);
    add_copy->setEnabled(def != nullptr);
    add_github->setEnabled(def != nullptr);
    download->setVisible(def && (!def->repo.isEmpty() || (!copy && !def->homepage.isEmpty())));
    if (def && !def->repo.isEmpty()) {
        download->setText(ready ? tr("Update") : tr("Download"));
    } else {
        download->setText(tr("Get it..."));
    }
    if (!def) {
        status->setText(tr("This console has no emulator yet."));
    } else if (!exe.isEmpty()) {
        installed_release =
            Library::EmulatorDownloader::InstalledRelease(library.DownloadedFolder(def->id));
        status->setText(copy && def->repo.isEmpty()
                            ? tr("Ready to play. A separate copy of %1; pick it for single "
                                 "games in their details.")
                                  .arg(library.Emulator(def->base)->name)
                        : installed_release.isEmpty()
                            ? tr("Ready to play.")
                            : tr("Ready to play (%1).").arg(installed_release));
        if (!def->repo.isEmpty()) {
            downloader.CheckLatest(*def);
        }
    } else if (copy && !def->repo.isEmpty()) {
        status->setText(tr("Checking the releases of github.com/%1...").arg(def->repo));
        downloader.CheckLatest(*def);
    } else if (copy) {
        status->setText(tr("Choose the program of this copy."));
    } else if (def->bundled && def->repo.isEmpty()) {
        status->setText(tr("%1 should sit next to After Hours. Choose its program to use "
                           "another copy.")
                            .arg(def->name));
    } else if (def->repo.isEmpty() && def->homepage.isEmpty()) {
        status->setText(tr("%1 has no official download. If you have it, choose its program.")
                            .arg(def->name));
    } else if (def->repo.isEmpty()) {
        status->setText(tr("%1 has no automatic download. Get it from <a href=\"%2\">its "
                           "website</a>, then choose its program.")
                            .arg(def->name, def->homepage));
    } else {
        status->setText(tr("Download installs the latest official %1 release into %2.")
                            .arg(def->name, QDir::toNativeSeparators(library.DownloadDir())));
    }
    args->setPlaceholderText(def ? def->args : QString{});
    default_args->setText(
        tr("Leave empty for the default. %ROM% is the game, %ROMDIR% its folder, %ROMNAME% its "
           "name, %EMUDIR% the emulator's folder."));
}

void ConsoleSettings::SaveFolders() {
    QStringList list;
    for (int row = 0; row < folders->count(); ++row) {
        list.append(folders->item(row)->text());
    }
    library.SetSystemFolders(system_id, list);
}

} // namespace AfterHours
