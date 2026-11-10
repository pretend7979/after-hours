// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <functional>

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "afterhours/emulator_downloader.h"
#include "afterhours/game_library.h"
#include "afterhours/library_dialog.h"

namespace Library {

namespace {
QStringList Items(const QListWidget* list) {
    QStringList items;
    for (int row = 0; row < list->count(); ++row) {
        items.append(list->item(row)->text());
    }
    return items;
}

/// A folder list with Add and Remove buttons; `changed` runs after either.
QGroupBox* FolderList(const QString& title, const QString& hint, QListWidget*& list,
                      const QStringList& items, QWidget* parent, std::function<void()> changed) {
    auto* box = new QGroupBox(title, parent);
    list = new QListWidget(box);
    list->addItems(items);
    list->setMinimumHeight(70);
    auto* add = new QPushButton(QObject::tr("Add..."), box);
    auto* remove = new QPushButton(QObject::tr("Remove"), box);
    QListWidget* target = list;
    QObject::connect(add, &QPushButton::clicked, box, [target, box, changed] {
        const QString dir = QFileDialog::getExistingDirectory(box, QObject::tr("Choose Folder"));
        if (!dir.isEmpty() && target->findItems(dir, Qt::MatchExactly).isEmpty()) {
            target->addItem(QDir::toNativeSeparators(dir));
            changed();
        }
    });
    QObject::connect(remove, &QPushButton::clicked, box, [target, changed] {
        delete target->currentItem();
        changed();
    });
    auto* buttons = new QVBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    auto* row = new QHBoxLayout;
    row->addWidget(list, 1);
    row->addLayout(buttons);
    auto* layout = new QVBoxLayout(box);
    auto* note = new QLabel(hint, box);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addLayout(row);
    return box;
}
} // Anonymous namespace

LibraryDialog::LibraryDialog(GameLibrary& library_, EmulatorDownloader& downloader_,
                             QWidget* parent)
    : QDialog{parent}, library{library_}, downloader{downloader_} {
    setWindowTitle(tr("Library Settings"));
    setMinimumSize(860, 680);

    auto* roms_box = FolderList(
        tr("Game folders"),
        tr("Folders whose subfolders are named after systems (nes, snes, psx, ps2, gba, "
           "dreamcast...), like RetroBat's roms folder. Subfolders inside those are scanned too."),
        rom_roots, library.RomRoots(), this,
        [this] { library.SetRomRoots(Items(rom_roots)); });
    auto* emulators_box = FolderList(
        tr("Emulator folders"),
        tr("Where emulators are found, each in a folder named after it. Downloads go to the "
           "first folder. An existing RetroBat emulators folder works here."),
        emulator_dirs, library.EmulatorDirs(), this, [this] {
            library.SetEmulatorDirs(Items(emulator_dirs));
            FillEmulators();
        });

    emulators = new QTreeWidget(this);
    emulators->setHeaderLabels({tr("Emulator"), tr("Systems"), tr("Installed")});
    emulators->setRootIsDecorated(false);
    emulators->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    download = new QPushButton(tr("Download"), this);
    choose = new QPushButton(tr("Choose EXE..."), this);
    progress = new QProgressBar(this);
    progress->setVisible(false);
    status = new QLabel(this);
    status->setWordWrap(true);
    connect(emulators, &QTreeWidget::currentItemChanged, this, [this] {
        const EmulatorDef* emulator = library.Emulator(SelectedEmulator());
        download->setEnabled(emulator && !emulator->repo.isEmpty() && !downloader.Busy());
        choose->setEnabled(emulator != nullptr);
        if (emulator && emulator->repo.isEmpty()) {
            status->setText(tr("%1 is not downloaded automatically; get it from %2")
                                .arg(emulator->name, emulator->homepage));
        }
    });
    connect(download, &QPushButton::clicked, this, [this] {
        const EmulatorDef* emulator = library.Emulator(SelectedEmulator());
        if (!emulator) {
            return;
        }
        download->setEnabled(false);
        progress->setVisible(true);
        progress->setValue(0);
        status->setText(tr("Downloading %1 from github.com/%2...").arg(emulator->name,
                                                                        emulator->repo));
        downloader.Download(*emulator, library.DownloadDir());
    });
    connect(&downloader, &EmulatorDownloader::Progress, this,
            [this](const QString&, qint64 received, qint64 total) {
                progress->setMaximum(total > 0 ? static_cast<int>(total / 1024) : 0);
                progress->setValue(static_cast<int>(received / 1024));
            });
    connect(&downloader, &EmulatorDownloader::Finished, this,
            [this](const QString&, bool, const QString& message) {
                progress->setVisible(false);
                status->setText(message);
                FillEmulators();
            });
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString id = SelectedEmulator();
        const QString exe = QFileDialog::getOpenFileName(this, tr("Choose Emulator"), {},
                                                         tr("Programs (*.exe)"));
        if (!exe.isEmpty()) {
            library.SetEmulatorExe(id, exe);
            FillEmulators();
        }
    });
    auto* emulator_buttons = new QHBoxLayout;
    emulator_buttons->addWidget(download);
    emulator_buttons->addWidget(choose);
    emulator_buttons->addWidget(progress, 1);
    auto* emulators_group = new QGroupBox(tr("Emulators"), this);
    auto* emulators_layout = new QVBoxLayout(emulators_group);
    auto* legal = new QLabel(
        tr("Downloads come from each emulator's official GitHub releases. After Hours never "
           "provides BIOS files, keys, firmware, libretro cores or games."),
        emulators_group);
    legal->setWordWrap(true);
    emulators_layout->addWidget(legal);
    emulators_layout->addWidget(emulators, 1);
    emulators_layout->addLayout(emulator_buttons);
    emulators_layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(roms_box);
    layout->addWidget(emulators_box);
    layout->addWidget(emulators_group, 1);
    layout->addWidget(buttons);
    FillEmulators();
}

void LibraryDialog::FillEmulators() {
    const QString selected = SelectedEmulator();
    emulators->clear();
    for (const EmulatorDef& emulator : library.Emulators()) {
        QStringList systems;
        for (const SystemDef& system : library.Systems()) {
            for (const SystemEmulator& option : system.emulators) {
                if (option.id == emulator.id) {
                    systems.append(system.short_name);
                    break;
                }
            }
        }
        const QString exe = library.EmulatorExe(emulator.id);
        auto* item = new QTreeWidgetItem(
            emulators, {emulator.name, systems.join(QStringLiteral(", ")),
                        exe.isEmpty() ? tr("No") : QDir::toNativeSeparators(exe)});
        item->setData(0, Qt::UserRole, emulator.id);
        if (emulator.id == selected) {
            emulators->setCurrentItem(item);
        }
    }
    emulators->resizeColumnToContents(0);
}

QString LibraryDialog::SelectedEmulator() const {
    const QTreeWidgetItem* item = emulators->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString{};
}

} // namespace Library
