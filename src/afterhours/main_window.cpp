// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <map>
#include <memory>

#include <QAbstractButton>
#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFile>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QVBoxLayout>

#include "afterhours/console_list.h"
#include "afterhours/console_settings.h"
#include "afterhours/emulator_downloader.h"
#include "afterhours/game_details.h"
#include "afterhours/game_grid.h"
#include "afterhours/game_library.h"
#include "afterhours/igdb_client.h"
#include "afterhours/library_dialog.h"
#include "afterhours/main_window.h"
#include "afterhours/metadata_dialog.h"
#include "afterhours/metadata_store.h"
#include "afterhours/paths.h"
#include "afterhours/setup_wizard.h"
#include "afterhours/theme.h"

namespace AfterHours {

/// The games of one console (or all), whose titles contain the search text.
class GameFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void SetSystem(const QString& system_) {
        beginFilterChange();
        system = system_;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }
    void SetText(const QString& text_) {
        beginFilterChange();
        text = text_.simplified();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }
    [[nodiscard]] const QString& Text() const {
        return text;
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QModelIndex index = sourceModel()->index(row, 0, parent);
        if (!system.isEmpty() && index.data(GameRoles::System).toString() != system) {
            return false;
        }
        return text.isEmpty() ||
               index.data(GameRoles::Title).toString().contains(text, Qt::CaseInsensitive);
    }

private:
    QString system;
    QString text;
};

/// The player at the top left: an initial in a circle and their name. Click to rename.
class UserChip : public QAbstractButton {
public:
    explicit UserChip(QWidget* parent) : QAbstractButton{parent} {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::TabFocus);
        setToolTip(tr("Change your name"));
    }

    void SetName(const QString& name) {
        setText(name);
        setAccessibleName(name);
        updateGeometry();
        update();
    }

    QSize sizeHint() const override {
        return {QFontMetrics{Theme::Font(15, QFont::DemiBold)}.horizontalAdvance(text()) + 86, 48};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse() || hasFocus();
        if (hot) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::Alpha(Qt::white, 12));
            painter.drawRoundedRect(QRectF(rect()).adjusted(0, 2, 0, -2), 22, 22);
        }
        const QRectF avatar{6, (height() - 36) / 2.0, 36, 36};
        QLinearGradient fill{avatar.topLeft(), avatar.bottomRight()};
        fill.setColorAt(0.0, Theme::Primary);
        fill.setColorAt(1.0, Theme::Primary.darker(150));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawEllipse(avatar);
        painter.setPen(Theme::PrimaryInk);
        painter.setFont(Theme::Font(16, QFont::Bold, true));
        painter.drawText(avatar, Qt::AlignCenter, text().left(1).toUpper());

        painter.setPen(Theme::Foreground);
        painter.setFont(Theme::Font(15, QFont::DemiBold));
        const QRectF name_rect{avatar.right() + 12, 0, width() - avatar.right() - 40.0,
                               static_cast<qreal>(height())};
        painter.drawText(name_rect, Qt::AlignVCenter | Qt::AlignLeft, text());
        if (hot) {
            QFont icon = ConsoleWidgets::IconFamily().isEmpty()
                             ? Theme::Font(12)
                             : QFont{ConsoleWidgets::IconFamily()};
            icon.setPixelSize(12);
            painter.setFont(icon);
            painter.setPen(Theme::Muted);
            painter.drawText(QRectF(width() - 30.0, 0, 20, height()), Qt::AlignCenter,
                             Theme::Icons::Edit());
        }
    }
};

namespace {
QLabel* MakeLabel(const QFont& font, const QColor& color, QWidget* parent = nullptr) {
    auto* label = new QLabel(parent);
    label->setFont(font);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
    return label;
}

QString Count(int count, const QString& one, const QString& many) {
    return count == 1 ? one : many.arg(QLocale{}.toString(count));
}
} // Anonymous namespace

MainWindow::MainWindow(QWidget* parent)
    : QWidget{parent}, library{new Library::GameLibrary(this)},
      downloader{new Library::EmulatorDownloader(this)}, store{new GameMetadataStore(this)},
      igdb{new IgdbClient(*store, this)}, games{new QStandardItemModel(this)},
      filter{new GameFilter(this)} {
    setWindowTitle(QStringLiteral("After Hours Arcade Manager"));
    setMinimumSize(1000, 660);
    setAttribute(Qt::WA_OpaquePaintEvent);
    filter->setSourceModel(games);

    // The header: who is playing, search and the library's actions.
    header = new QWidget(this);
    header->setFixedHeight(78);
    user = new UserChip(header);
    user->SetName(library->UserName());
    auto* brand = MakeLabel(Theme::EyebrowFont(11), Theme::Alpha(Theme::Muted, 200));
    brand->setText(QStringLiteral("After Hours"));
    search = new QLineEdit(header);
    search->setObjectName(QStringLiteral("Search"));
    search->setPlaceholderText(tr("Search the collection..."));
    search->setClearButtonEnabled(true);
    search->setFixedSize(360, 44);
    auto* add_games = new Theme::Button(Theme::Button::Kind::Outline, Theme::Icons::Add(),
                                        tr("Add games"), header);
    app_update = new Theme::Button(Theme::Button::Kind::Primary, Theme::Icons::Download(),
                                   tr("Update"), header);
    app_update->hide();
    auto* emulators = new Theme::Button(Theme::Button::Kind::Ghost, Theme::Icons::Library(),
                                        tr("Emulators"), header);
    auto* library_settings = new Theme::Button(Theme::Button::Kind::Icon, Theme::Icons::Settings(),
                                               tr("Settings and updates"), header);
    auto* full_screen = new Theme::Button(Theme::Button::Kind::Icon, Theme::Icons::FullScreen(),
                                          tr("Full screen (F11)"), header);
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(22, 0, 28, 0);
    header_layout->setSpacing(10);
    header_layout->addWidget(user);
    header_layout->addSpacing(18);
    header_layout->addWidget(brand);
    header_layout->addStretch();
    header_layout->addWidget(search);
    header_layout->addSpacing(6);
    header_layout->addWidget(add_games);
    header_layout->addWidget(app_update);
    header_layout->addWidget(emulators);
    header_layout->addWidget(library_settings);
    header_layout->addWidget(full_screen);

    // The sidebar of consoles.
    auto* sidebar = new QWidget(this);
    sidebar->setFixedWidth(324);
    auto* sidebar_title = MakeLabel(Theme::Font(14, QFont::DemiBold), Theme::Foreground);
    sidebar_title->setText(tr("Your collection"));
    scan_status = MakeLabel(Theme::Font(12), Theme::Muted);
    consoles = new ConsoleList(sidebar);
    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(22, 26, 8, 18);
    sidebar_layout->setSpacing(4);
    auto* title_row = new QHBoxLayout;
    title_row->setContentsMargins(16, 0, 16, 0);
    title_row->addWidget(sidebar_title);
    title_row->addStretch();
    title_row->addWidget(scan_status);
    sidebar_layout->addLayout(title_row);
    sidebar_layout->addSpacing(12);
    sidebar_layout->addWidget(consoles, 1);

    // The chosen console: what it is, how its games start, and its games.
    eyebrow = MakeLabel(Theme::EyebrowFont(12), Theme::Primary);
    heading = MakeLabel(Theme::Font(40, QFont::DemiBold, true), Theme::Foreground);
    intro = MakeLabel(Theme::Font(15), Theme::Muted);
    intro->setTextFormat(Qt::RichText);
    console_logo = new QLabel;
    get_emulator = new Theme::Button(Theme::Button::Kind::Primary, Theme::Icons::Download(),
                                     tr("Get the emulator"));
    console_settings = new Theme::Button(Theme::Button::Kind::Outline, Theme::Icons::Settings(),
                                         tr("Console settings"));
    auto* intro_text = new QVBoxLayout;
    intro_text->setSpacing(8);
    intro_text->addWidget(eyebrow);
    intro_text->addWidget(heading);
    intro_text->addWidget(intro);
    auto* intro_actions = new QHBoxLayout;
    intro_actions->setSpacing(10);
    intro_actions->addStretch();
    intro_actions->addWidget(get_emulator);
    intro_actions->addWidget(console_settings);
    auto* intro_side = new QVBoxLayout;
    intro_side->setSpacing(14);
    intro_side->addWidget(console_logo, 0, Qt::AlignRight | Qt::AlignVCenter);
    intro_side->addLayout(intro_actions);
    auto* intro_row = new QHBoxLayout;
    intro_row->setContentsMargins(11, 0, 11, 0);
    intro_row->addLayout(intro_text, 1);
    intro_row->addLayout(intro_side);

    result_count = MakeLabel(Theme::Font(13), Theme::Muted);
    auto* result_row = new QHBoxLayout;
    result_row->setContentsMargins(11, 0, 11, 0);
    result_row->addWidget(result_count);
    result_row->addStretch();

    grid = new GameGrid(*store);
    grid->setModel(filter);
    auto* empty = new QWidget;
    empty_title = MakeLabel(Theme::Font(24, QFont::DemiBold, true), Theme::Foreground);
    empty_text = MakeLabel(Theme::Font(14), Theme::Muted);
    empty_text->setWordWrap(true);
    empty_text->setAlignment(Qt::AlignCenter);
    // A fixed width: a centred label in a layout otherwise gets one line's height and is cut off.
    empty_text->setFixedWidth(520);
    empty_action = new Theme::Button(Theme::Button::Kind::Primary, QString{}, QString{});
    static_cast<Theme::Button*>(empty_action)->SetLarge(true);
    auto* empty_layout = new QVBoxLayout(empty);
    empty_layout->addStretch(2);
    empty_layout->addWidget(empty_title, 0, Qt::AlignHCenter);
    empty_layout->addSpacing(8);
    empty_layout->addWidget(empty_text, 0, Qt::AlignHCenter);
    empty_layout->addSpacing(18);
    empty_layout->addWidget(empty_action, 0, Qt::AlignHCenter);
    empty_layout->addStretch(3);
    results = new QStackedWidget;
    results->addWidget(grid);
    results->addWidget(empty);

    auto* content = new QVBoxLayout;
    content->setContentsMargins(16, 30, 18, 0);
    content->setSpacing(0);
    content->addLayout(intro_row);
    content->addSpacing(26);
    content->addLayout(result_row);
    content->addSpacing(4);
    content->addWidget(results, 1);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(sidebar);
    body->addLayout(content, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addLayout(body, 1);

    details = new GameDetails(*store, this);
    toast = new QLabel(this);
    toast->setFont(Theme::Font(13, QFont::DemiBold));
    toast->setStyleSheet(QStringLiteral(
        "QLabel{color:#f5f2e9;background:#202128;border:1px solid #33343d;border-radius:12px;"
        "padding:12px 18px;}"));
    toast->hide();

    // Wiring.
    auto* search_timer = new QTimer(this);
    search_timer->setSingleShot(true);
    search_timer->setInterval(220);
    connect(search, &QLineEdit::textChanged, search_timer, qOverload<>(&QTimer::start));
    connect(search_timer, &QTimer::timeout, this, [this] {
        filter->SetText(search->text());
        grid->scrollToTop();
        UpdateIntro();
        UpdateCount();
    });
    connect(user, &QAbstractButton::clicked, this, &MainWindow::RenameUser);
    connect(add_games, &QAbstractButton::clicked, this, &MainWindow::AddFolder);
    connect(emulators, &QAbstractButton::clicked, this,
            [this] { RunSetup(static_cast<int>(SetupWizard::Page::Emulators)); });
    connect(library_settings, &QAbstractButton::clicked, this, [this, library_settings] {
        QMenu menu;
        QAction* settings = menu.addAction(tr("Library settings..."));
        QAction* check = menu.addAction(tr("Check for updates"));
        menu.addSeparator();
        menu.addAction(tr("After Hours Arcade Manager %1").arg(QStringLiteral(AFTERHOURS_VERSION)))
            ->setEnabled(false);
        QAction* chosen =
            menu.exec(library_settings->mapToGlobal(QPoint{0, library_settings->height()}));
        if (chosen == settings) {
            OpenLibrarySettings();
        } else if (chosen == check) {
            CheckForAppUpdate(true);
            CheckForUpdates();
        }
    });
    connect(full_screen, &QAbstractButton::clicked, this,
            [this] { setWindowState(windowState() ^ Qt::WindowFullScreen); });
    connect(console_settings, &QAbstractButton::clicked, this,
            [this] { OpenConsoleSettings(current_system); });
    connect(get_emulator, &QAbstractButton::clicked, this, [this] {
        DownloadEmulator(library->ChosenEmulator(current_system).id);
    });
    connect(consoles, &ConsoleList::ConsoleChosen, this, &MainWindow::ShowConsole);
    connect(grid, &GameGrid::PlayRequested, this, &MainWindow::Play);
    connect(grid, &GameGrid::DetailsRequested, this, &MainWindow::OpenDetails);
    connect(grid, &GameGrid::VisibleRowsChanged, this, &MainWindow::LookUp);
    connect(grid, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const QModelIndex index = grid->indexAt(point);
        if (!index.isValid()) {
            return;
        }
        const u64 key = index.data(GameRoles::Key).toULongLong();
        const Library::RomEntry* rom = library->Rom(key);
        const bool has_game_settings = rom && library->SupportsGameSettings(*rom);
        QMenu menu;
        QAction* play = menu.addAction(tr("Play"));
        QAction* open = menu.addAction(tr("Details"));
        QAction* game_settings = has_game_settings ? menu.addAction(tr("Game settings...")) : nullptr;
        QAction* edit = menu.addAction(tr("Edit info..."));
        menu.addSeparator();
        QAction* show = menu.addAction(tr("Show file"));
        QAction* settings = menu.addAction(tr("Console settings..."));
        QAction* chosen = menu.exec(grid->viewport()->mapToGlobal(point));
        if (chosen == play) {
            Play(index);
        } else if (chosen == open) {
            OpenDetails(index);
        } else if (chosen == game_settings) {
            OpenGameSettings(index);
        } else if (chosen == edit) {
            EditInfo(index);
        } else if (chosen == show) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(
                QFileInfo{index.data(GameRoles::Path).toString()}.absolutePath()));
        } else if (chosen == settings) {
            OpenConsoleSettings(index.data(GameRoles::System).toString());
        }
    });
    connect(details, &GameDetails::PlayRequested, this, &MainWindow::Play);
    connect(details, &GameDetails::GameSettingsRequested, this, &MainWindow::OpenGameSettings);
    connect(details, &GameDetails::EditRequested, this, &MainWindow::EditInfo);
    connect(details, &GameDetails::SettingsRequested, this, &MainWindow::OpenConsoleSettings);
    connect(details, &GameDetails::DownloadRequested, this, &MainWindow::DownloadEmulator);
    connect(details, &GameDetails::EmulatorChosen, this,
            [this](const QModelIndex& index, const QString& emulator_id) {
                const u64 key = index.data(GameRoles::Key).toULongLong();
                library->SetGameEmulator(key, emulator_id);
                details->SetLaunch(Launch(index.data(GameRoles::System).toString(), key));
            });
    connect(empty_action, &QAbstractButton::clicked, this, [this] {
        if (!filter->Text().isEmpty()) {
            search->clear();
        } else if (!current_system.isEmpty() && library->RomCount(current_system) == 0) {
            OpenConsoleSettings(current_system);
        } else {
            AddFolder();
        }
    });
    connect(store, &GameMetadataStore::Changed, this, [this](u64 key) {
        grid->Delegate()->Invalidate(key);
        grid->viewport()->update();
        details->Refresh(key);
    });
    connect(library, &Library::GameLibrary::ScanStarted, this, [this] {
        scan_status->setText(tr("Scanning..."));
        UpdateIntro();
        UpdateCount();
    });
    connect(library, &Library::GameLibrary::ScanFinished, this, [this] {
        scan_status->clear();
        BuildModel();
    });
    connect(library, &Library::GameLibrary::SettingsChanged, this,
            [this] { user->SetName(library->UserName()); });
    connect(library, &Library::GameLibrary::Preparing, this, [this](const QString& title) {
        ShowToast(tr("Unpacking %1...").arg(title), 0);
    });
    connect(library, &Library::GameLibrary::LaunchStarted, this, [this](const QString& title) {
        ShowToast(tr("Starting %1").arg(title), 3000);
    });
    connect(library, &Library::GameLibrary::LaunchFailed, this, [this](const QString& error) {
        toast->hide();
        QMessageBox box{QMessageBox::Warning, tr("Can't start the game"), error,
                        QMessageBox::Close, this};
        QPushButton* settings =
            box.addButton(tr("Console settings..."), QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == static_cast<QAbstractButton*>(settings)) {
            OpenConsoleSettings(property("launch_system").toString());
        }
    });

    BuildModel();
    if (!library->RomRoots().isEmpty() || !library->SystemFolders(QStringLiteral("switch")).isEmpty() ||
        !library->SystemFolders(QStringLiteral("switch2")).isEmpty()) {
        library->Rescan();
    }
    grid->setFocus();
    if (!library->SetupDone()) {
        // A new player: set up once the window is up.
        QTimer::singleShot(300, this, [this] { RunSetup(0); });
    } else {
        QTimer::singleShot(4000, this, &MainWindow::CheckForUpdates);
    }
    // A new install can be behind too (an old zip).
    QTimer::singleShot(2500, this, [this] { CheckForAppUpdate(); });
}

namespace {
/// After Hours' own releases, downloaded like an emulator's.
Library::EmulatorDef AppRelease() {
    Library::EmulatorDef def;
    def.id = QStringLiteral("afterhours-update");
    def.name = QStringLiteral("After Hours Arcade Manager");
    def.repo = QStringLiteral(AFTERHOURS_UPDATE_REPO);
    def.asset_pattern = QStringLiteral("^After-Hours-.*-windows-x64\\.zip$");
    return def;
}
} // Anonymous namespace

void MainWindow::CheckForAppUpdate(bool manual) {
    connect(app_update, &QAbstractButton::clicked, this, &MainWindow::InstallAppUpdate,
            Qt::UniqueConnection);
    if (manual) {
        ShowToast(tr("Checking for updates..."), 0);
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(
        downloader, &Library::EmulatorDownloader::LatestRelease, this,
        [this, connection, manual](const QString& id, const QString& tag) {
            if (id != AppRelease().id) {
                return;
            }
            disconnect(*connection);
            QString version = tag;
            if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
                version.remove(0, 1);
            }
            const QVersionNumber latest = QVersionNumber::fromString(version);
            const QVersionNumber current =
                QVersionNumber::fromString(QStringLiteral(AFTERHOURS_VERSION));
            if (!latest.isNull() && latest > current) {
                app_update_tag = tag;
                app_update->setText(tr("Update to %1").arg(tag));
                app_update->setToolTip(tr("After Hours %1 is out (this is %2)")
                                           .arg(tag, QStringLiteral(AFTERHOURS_VERSION)));
                app_update->show();
                if (manual) {
                    ShowToast(tr("After Hours %1 is out: click \"Update to %1\" to install it.")
                                  .arg(tag),
                              5000);
                }
            } else if (manual) {
                ShowToast(latest.isNull()
                              ? tr("Couldn't reach GitHub to check for updates.")
                              : tr("After Hours is up to date (%1).")
                                    .arg(QStringLiteral(AFTERHOURS_VERSION)),
                          4000);
            }
        });
    downloader->CheckLatest(AppRelease());
}

void MainWindow::InstallAppUpdate() {
    if (downloader->Busy()) {
        ShowToast(tr("Another download is running; try again when it is done."), 4000);
        return;
    }
    if (QMessageBox::question(
            this, tr("Update After Hours"),
            tr("Download After Hours %1 and restart into it? Your library and settings stay as "
               "they are.")
                .arg(app_update_tag)) != QMessageBox::Yes) {
        return;
    }
    const QString folder = AfterHours::Paths::CacheDir() + QStringLiteral("/self-update");
    QDir{folder}.removeRecursively();
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(
        downloader, &Library::EmulatorDownloader::Finished, this,
        [this, connection, folder](const QString& id, bool ok, const QString& message) {
            if (id != AppRelease().id) {
                return;
            }
            disconnect(*connection);
            if (!ok) {
                QMessageBox::warning(this, tr("Update After Hours"), message);
                return;
            }
            // Copied over this folder once After Hours has closed (its files are in use until
            // then), keeping the user folder, then started again.
            const QString source =
                QDir::toNativeSeparators(QDir{folder}.filePath(AppRelease().id));
            const QString target = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
            const QString script = QDir{folder}.filePath(QStringLiteral("finish-update.cmd"));
            QFile file{script};
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                QMessageBox::warning(this, tr("Update After Hours"),
                                     tr("Could not write %1.").arg(script));
                return;
            }
            file.write(QStringLiteral(
                           "@echo off\r\n"
                           ":wait\r\n"
                           "tasklist /FI \"PID eq %1\" | find \"%1\" >nul && "
                           "(timeout /t 1 /nobreak >nul & goto wait)\r\n"
                           "robocopy \"%2\" \"%3\" /E /XD user /NFL /NDL /NJH /NJS /NP >nul\r\n"
                           "start \"\" \"%3\\afterhours.exe\"\r\n")
                           .arg(QCoreApplication::applicationPid())
                           .arg(source, target)
                           .toLocal8Bit());
            file.close();
            if (!QProcess::startDetached(QStringLiteral("cmd.exe"),
                                         {QStringLiteral("/c"), QDir::toNativeSeparators(script)})) {
                QMessageBox::warning(this, tr("Update After Hours"),
                                     tr("Could not start the update."));
                return;
            }
            close();
        });
    ShowToast(tr("Downloading After Hours %1...").arg(app_update_tag), 0);
    downloader->Download(AppRelease(), folder);
}

void MainWindow::CheckForUpdates() {
    struct State {
        std::map<QString, QString> installed; ///< emulator id -> release tag
        QStringList updates;
        QMetaObject::Connection connection;
    };
    auto state = std::make_shared<State>();
    for (const Library::EmulatorDef& emulator : library->Emulators()) {
        const QString folder = library->DownloadedFolder(emulator.id);
        const QString tag = Library::EmulatorDownloader::InstalledRelease(folder);
        if (!emulator.repo.isEmpty() && !tag.isEmpty()) {
            state->installed.emplace(emulator.id, tag);
        }
    }
    if (state->installed.empty()) {
        return;
    }
    state->connection = connect(
        downloader, &Library::EmulatorDownloader::LatestRelease, this,
        [this, state](const QString& id, const QString& tag) {
            const auto it = state->installed.find(id);
            if (it == state->installed.end()) {
                return;
            }
            if (!tag.isEmpty() && tag != it->second) {
                if (const Library::EmulatorDef* emulator = library->Emulator(id)) {
                    state->updates.append(QStringLiteral("%1 %2").arg(emulator->name, tag));
                }
            }
            state->installed.erase(it);
            if (state->installed.empty()) {
                disconnect(state->connection);
                if (!state->updates.isEmpty()) {
                    ShowToast(tr("Updates available: %1. Update from the console's settings.")
                                  .arg(state->updates.join(QStringLiteral(", "))),
                              9000);
                }
            }
        });
    // Checked after connecting: a failed request answers at once.
    const auto ids = state->installed;
    for (const auto& [id, tag] : ids) {
        if (const Library::EmulatorDef* emulator = library->Emulator(id)) {
            downloader->CheckLatest(*emulator);
        }
    }
}

void MainWindow::RunSetup(int start) {
    SetupWizard wizard{*library, *downloader, static_cast<SetupWizard::Page>(start), this};
    wizard.exec();
    user->SetName(library->UserName());
    RefreshConsoles();
    UpdateIntro();
    UpdateCount();
}

void MainWindow::BuildModel() {
    const QModelIndex current = grid->currentIndex();
    const u64 current_key = current.data(GameRoles::Key).toULongLong();
    games->clear();
    QList<QStandardItem*> items;
    items.reserve(static_cast<qsizetype>(library->Roms().size()));
    for (const Library::RomEntry& rom : library->Roms()) {
        const Library::SystemDef* system = library->System(rom.system);
        auto* item = new QStandardItem(rom.title);
        item->setData(QVariant::fromValue<qulonglong>(rom.key), GameRoles::Key);
        item->setData(rom.system, GameRoles::System);
        item->setData(rom.path, GameRoles::Path);
        item->setData(rom.title, GameRoles::Title);
        item->setData(system ? system->short_name : rom.system, GameRoles::SystemName);
        item->setData(system ? system->color : QColor{}, GameRoles::Color);
        item->setEditable(false);
        items.append(item);
    }
    games->invisibleRootItem()->appendRows(items);
    RefreshConsoles();
    // Keep the selection on the same game across rescans.
    for (int row = 0; current_key != 0 && row < filter->rowCount(); ++row) {
        if (filter->index(row, 0).data(GameRoles::Key).toULongLong() == current_key) {
            grid->setCurrentIndex(filter->index(row, 0));
            break;
        }
    }
    UpdateIntro();
    UpdateCount();
}

void MainWindow::RefreshConsoles() {
    std::vector<ConsoleEntry> entries;
    for (const Library::SystemDef& system : library->Systems()) {
        const int count = library->RomCount(system.id);
        // A console shows once it has games.
        if (count == 0) {
            continue;
        }
        entries.push_back(ConsoleEntry{system.id, system.short_name, system.maker,
                                       library->LogoFile(system.id), count});
    }
    consoles->SetConsoles(entries, static_cast<int>(library->Roms().size()));
    if (consoles->Selected() != current_system) {
        ShowConsole(consoles->Selected());
    }
}

void MainWindow::ShowConsole(const QString& system_id) {
    current_system = system_id;
    details->Close();
    filter->SetSystem(system_id);
    grid->scrollToTop();
    if (filter->rowCount() > 0) {
        grid->setCurrentIndex(filter->index(0, 0));
    }
    UpdateIntro();
    UpdateCount();
}

void MainWindow::DownloadEmulator(const QString& emulator_id) {
    const Library::EmulatorDef* emulator = library->Emulator(emulator_id);
    if (!emulator) {
        return;
    }
    if (emulator->repo.isEmpty()) {
        if (!emulator->homepage.isEmpty()) {
            QDesktopServices::openUrl(QUrl{emulator->homepage});
        }
        return;
    }
    if (downloader->Busy()) {
        ShowToast(tr("Another download is running; try again when it is done."), 4000);
        return;
    }
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(
        downloader, &Library::EmulatorDownloader::Finished, this,
        [this, connection, emulator_id, name = emulator->name](const QString& id, bool ok,
                                                               const QString& message) {
            if (id != emulator_id) {
                return;
            }
            disconnect(*connection);
            ShowToast(ok ? tr("%1 is ready to play.").arg(name) : message, 5000);
            RefreshConsoles();
            UpdateIntro();
            if (details->IsOpen()) {
                OpenDetails(grid->currentIndex());
            }
        });
    ShowToast(tr("Downloading %1 from GitHub...").arg(emulator->name), 0);
    downloader->Download(*emulator, library->DownloadDir());
}

LaunchInfo MainWindow::Launch(const QString& system_id, u64 key) const {
    LaunchInfo launch;
    const Library::SystemDef* system = library->System(system_id);
    launch.console = system ? system->name : system_id;
    const Library::SystemEmulator console_choice = library->ChosenEmulator(system_id);
    if (const Library::EmulatorDef* emulator = library->Emulator(console_choice.id)) {
        launch.console_emulator = emulator->name;
    }
    Library::SystemEmulator choice = console_choice;
    if (const Library::RomEntry* rom = key != 0 ? library->Rom(key) : nullptr) {
        choice = library->GameEmulator(*rom);
        launch.game_choice = library->GameEmulatorChoice(key);
    }
    if (const Library::EmulatorDef* emulator = library->Emulator(choice.id)) {
        launch.emulator = emulator->name;
        launch.emulator_id = emulator->id;
        launch.installed = !library->EmulatorExe(emulator->id).isEmpty();
        launch.downloadable = !emulator->repo.isEmpty();
        launch.has_website = !emulator->homepage.isEmpty();
        launch.has_game_settings = library->SupportsGameSettings(choice.id);
    }
    for (const Library::SystemEmulator& option : library->SystemEmulators(system_id)) {
        if (const Library::EmulatorDef* emulator = library->Emulator(option.id)) {
            launch.options.push_back(EmulatorOption{
                emulator->id, emulator->name, !library->EmulatorExe(emulator->id).isEmpty()});
        }
    }
    return launch;
}

void MainWindow::UpdateIntro() {
    const Library::SystemDef* system = library->System(current_system);
    const QString text = filter->Text();
    console_logo->setVisible(system != nullptr);
    console_settings->setVisible(system != nullptr);
    get_emulator->setVisible(false);

    if (!system) {
        eyebrow->setText(tr("The collection"));
        heading->setText(text.isEmpty() ? tr("Your next good game.")
                                        : tr("Results for “%1”").arg(text));
        int console_count = 0;
        for (const Library::SystemDef& entry : library->Systems()) {
            console_count += library->RomCount(entry.id) > 0 ? 1 : 0;
        }
        intro->setText(
            library->Roms().empty()
                ? (library->Scanning() ? tr("Looking through your games folders...")
                                       : tr("Add the folder with your games to fill your library."))
                : tr("%1 across %2.")
                      .arg(Count(static_cast<int>(library->Roms().size()), tr("1 game"),
                                 tr("%1 games")),
                           Count(console_count, tr("1 console"), tr("%1 consoles"))));
        return;
    }

    eyebrow->setText(QStringLiteral("%1  ·  %2").arg(system->maker).arg(system->year));
    heading->setText(text.isEmpty() ? system->name : tr("Results for “%1”").arg(text));
    const LaunchInfo launch = Launch(system->id);
    const int count = library->RomCount(system->id);
    QString line = Count(count, tr("1 game"), tr("%1 games"));
    if (launch.emulator.isEmpty()) {
        line += tr("  ·  no emulator set");
    } else if (launch.installed) {
        line += tr("  ·  <span style='color:#d8f077'>●</span> Plays in %1")
                    .arg(launch.emulator.toHtmlEscaped());
    } else {
        line += tr("  ·  <span style='color:#ff8791'>●</span> %1 isn't installed")
                    .arg(launch.emulator.toHtmlEscaped());
        const Library::EmulatorDef* emulator =
            library->Emulator(library->ChosenEmulator(system->id).id);
        if (emulator) {
            get_emulator->setText(emulator->repo.isEmpty() ? tr("Get %1").arg(emulator->name)
                                                           : tr("Download %1").arg(emulator->name));
            get_emulator->setVisible(true);
        }
    }
    intro->setText(line);

    // The console's logo in its own colours.
    QPixmap logo;
    if (const QString file = library->LogoFile(system->id); !file.isEmpty()) {
        logo = QPixmap::fromImage(Theme::ReadLogo(file, QSize{230, 76}, devicePixelRatioF()));
    }
    console_logo->setPixmap(logo);
    console_logo->setVisible(!logo.isNull());
    update();
}

void MainWindow::UpdateCount() {
    const int shown = filter->rowCount();
    result_count->setText(Count(shown, tr("1 game"), tr("%1 games")));
    result_count->setVisible(shown > 0);
    if (shown > 0) {
        results->setCurrentIndex(0);
        return;
    }
    results->setCurrentIndex(1);
    const Library::SystemDef* system = library->System(current_system);
    // While a scan runs, an empty list means "not found yet", not "nothing set up".
    empty_action->setVisible(!library->Scanning() || !filter->Text().isEmpty());
    if (!filter->Text().isEmpty()) {
        empty_title->setText(tr("No games found."));
        empty_text->setText(tr("Try fewer words, a different title, or another console."));
        empty_action->setText(tr("Clear search"));
    } else if (library->Scanning()) {
        empty_title->setText(tr("Finding your games..."));
        empty_text->setText(tr("Looking through your games folders. Your games appear here as "
                               "soon as the scan is done."));
    } else if (system && library->Roms().size() > 0) {
        empty_title->setText(tr("No %1 games yet.").arg(system->short_name));
        empty_text->setText(
            tr("Add a folder with your %1 games in the console's settings, or put them in a "
               "folder named \"%2\" inside your games folder.")
                .arg(system->short_name,
                     system->folders.isEmpty() ? system->id : system->folders.first()));
        empty_action->setText(tr("Console settings"));
    } else {
        empty_title->setText(tr("Let's find your games."));
        empty_text->setText(
            tr("Choose the folder that holds your games. Each console's games are found in "
               "folders named after it, like snes, psx or switch, with their subfolders."));
        empty_action->setText(tr("Add games folder"));
    }
    empty_text->setFixedHeight(empty_text->heightForWidth(empty_text->width()));
    empty_action->updateGeometry();
}

void MainWindow::LookUp(int first, int last) {
    if (!IgdbClient::HasServer()) {
        return;
    }
    for (int row = first; row <= last && row < filter->rowCount(); ++row) {
        const QModelIndex index = filter->index(row, 0);
        const u64 key = index.data(GameRoles::Key).toULongLong();
        if (!IgdbClient::NeedsLookup(*store, key)) {
            continue;
        }
        const Library::SystemDef* system = library->System(index.data(GameRoles::System).toString());
        igdb->Request(key, index.data(GameRoles::Title).toString(),
                      system ? system->igdb_platforms : std::vector<int>{});
    }
}

void MainWindow::Play(const QModelIndex& index) {
    const Library::RomEntry* rom = library->Rom(index.data(GameRoles::Key).toULongLong());
    if (!rom) {
        return;
    }
    setProperty("launch_system", rom->system);
    library->Launch(*rom);
}

void MainWindow::OpenGameSettings(const QModelIndex& index) {
    const Library::RomEntry* rom = library->Rom(index.data(GameRoles::Key).toULongLong());
    if (!rom) {
        return;
    }
    setProperty("launch_system", rom->system);
    library->OpenGameSettings(*rom);
}

void MainWindow::OpenDetails(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    grid->setCurrentIndex(index);
    LookUp(index.row(), index.row());
    details->Open(index, Launch(index.data(GameRoles::System).toString(),
                                index.data(GameRoles::Key).toULongLong()));
}

void MainWindow::EditInfo(const QModelIndex& index) {
    const u64 key = index.data(GameRoles::Key).toULongLong();
    MetadataDialog dialog{key, index.data(GameRoles::Title).toString(), *store, *igdb, this};
    dialog.exec();
    grid->Delegate()->Invalidate(key);
    grid->viewport()->update();
    details->Refresh(key);
}

void MainWindow::OpenConsoleSettings(const QString& system_id) {
    if (!library->System(system_id)) {
        return;
    }
    ConsoleSettings dialog{system_id, *library, *downloader, this};
    dialog.exec();
    RefreshConsoles();
    UpdateIntro();
    if (details->IsOpen()) {
        OpenDetails(grid->currentIndex());
    }
}

void MainWindow::OpenLibrarySettings() {
    Library::LibraryDialog dialog{*library, *downloader, this};
    dialog.exec();
    RefreshConsoles();
    UpdateIntro();
}

void MainWindow::AddFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose the folder with your games (consoles in subfolders like snes, psx)"));
    if (dir.isEmpty()) {
        return;
    }
    QStringList roots = library->RomRoots();
    if (!roots.contains(QDir::toNativeSeparators(dir))) {
        roots.append(QDir::toNativeSeparators(dir));
        library->SetRomRoots(roots);
        ShowToast(tr("Looking for games in %1").arg(QDir::toNativeSeparators(dir)), 3500);
    }
}

void MainWindow::RenameUser() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Your name"), tr("Name shown at the top:"),
                                               QLineEdit::Normal, library->UserName(), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        library->SetUserName(name);
    }
}

void MainWindow::ShowToast(const QString& text, int milliseconds) {
    toast->setText(text);
    toast->adjustSize();
    toast->move(width() - toast->width() - 28, height() - toast->height() - 28);
    toast->show();
    toast->raise();
    if (milliseconds > 0) {
        QTimer::singleShot(milliseconds, toast, [this, text] {
            if (toast->text() == text) {
                toast->hide();
            }
        });
    }
}

void MainWindow::paintEvent(QPaintEvent*) {
    QPainter painter{this};
    painter.fillRect(rect(), Theme::Background);
    // A faint glow of the console's colour behind its name.
    const Library::SystemDef* system = library->System(current_system);
    const QColor color = system && system->color.isValid() ? system->color : Theme::Primary;
    QRadialGradient glow{QPointF(width() * 0.62, header->height()), width() * 0.5};
    glow.setColorAt(0.0, Theme::Alpha(color, 34));
    glow.setColorAt(1.0, Theme::Alpha(color, 0));
    painter.fillRect(rect(), glow);
    painter.fillRect(QRect(0, 0, width(), header->height()), Theme::Alpha(Theme::Background, 235));
    painter.fillRect(QRect(0, header->height() - 1, width(), 1), Theme::Alpha(Qt::white, 12));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (details->IsOpen()) {
        details->setGeometry(rect());
    }
    if (toast->isVisible()) {
        toast->move(width() - toast->width() - 28, height() - toast->height() - 28);
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Find) || event->key() == Qt::Key_Slash) {
        search->setFocus();
        search->selectAll();
        return;
    }
    if (event->key() == Qt::Key_F11) {
        setWindowState(windowState() ^ Qt::WindowFullScreen);
        return;
    }
    if (event->key() == Qt::Key_Escape && !search->text().isEmpty()) {
        search->clear();
        grid->setFocus();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    details->Close();
    QWidget::closeEvent(event);
}

} // namespace AfterHours
