// SPDX-FileCopyrightText: Copyright 2026 After Hours Arcade Manager Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

#include "afterhours/main_window.h"
#include "afterhours/theme.h"

namespace {
/// Dialogs and inputs in the library's colours.
QString StyleSheet() {
    return QStringLiteral(R"(
        QDialog { background: #191a20; }
        QLabel#Muted { color: #b9b9c6; }
        QLineEdit, QComboBox, QListWidget, QTreeWidget, QPlainTextEdit, QTextEdit, QSpinBox {
            background: #101115; border: 1px solid #3a3b46; border-radius: 9px;
            padding: 7px 10px; selection-background-color: #c5b0ff; selection-color: #241439;
        }
        QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
            border: 1px solid #c5b0ff;
        }
        QLineEdit#Search { background: #191a20; border-radius: 20px; padding: 0 16px; font-size: 16px; }
        QComboBox::drop-down { border: 0; width: 26px; }
        QComboBox QAbstractItemView { background: #202128; border: 1px solid #33343d; }
        QListWidget::item, QTreeWidget::item { padding: 6px 4px; border-radius: 6px; }
        QListWidget::item:selected, QTreeWidget::item:selected { background: #c5b0ff26; color: #f5f2e9; }
        QHeaderView::section { background: #191a20; color: #b9b9c6; border: 0; padding: 6px; }
        QPushButton {
            background: #292b32; border: 0; border-radius: 9px; padding: 8px 16px; font-weight: 600;
        }
        QPushButton:hover { background: #343640; }
        QPushButton:default { background: #c5b0ff; color: #241439; }
        QPushButton:disabled { color: #6c6c78; }
        QProgressBar { background: #292b32; border: 0; border-radius: 3px; }
        QProgressBar::chunk { background: #c5b0ff; border-radius: 3px; }
        QMenu { background: #202128; border: 1px solid #33343d; padding: 6px; }
        QMenu::item { padding: 7px 22px; border-radius: 6px; }
        QMenu::item:selected { background: #c5b0ff26; }
        QMenu::separator { height: 1px; background: #33343d; margin: 5px 8px; }
        QToolTip { background: #202128; color: #f5f2e9; border: 1px solid #33343d; padding: 5px; }
        QMessageBox, QInputDialog { background: #191a20; }
        QScrollBar:vertical { background: transparent; width: 8px; }
        QScrollBar::handle:vertical { background: #33343d; border-radius: 4px; min-height: 30px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )");
}
} // Anonymous namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName(QStringLiteral("After Hours"));
    QCoreApplication::setApplicationName(QStringLiteral("After Hours Arcade Manager"));
    QApplication app{argc, argv};
    QApplication::setWindowIcon(QIcon{QStringLiteral(":/img/afterhours.ico")});
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(0x10, 0x11, 0x15));
    palette.setColor(QPalette::WindowText, QColor(0xf5, 0xf2, 0xe9));
    palette.setColor(QPalette::Base, QColor(0x10, 0x11, 0x15));
    palette.setColor(QPalette::AlternateBase, QColor(0x19, 0x1a, 0x20));
    palette.setColor(QPalette::Text, QColor(0xf5, 0xf2, 0xe9));
    palette.setColor(QPalette::Button, QColor(0x29, 0x2b, 0x32));
    palette.setColor(QPalette::ButtonText, QColor(0xf5, 0xf2, 0xe9));
    palette.setColor(QPalette::Highlight, QColor(0xc5, 0xb0, 0xff));
    palette.setColor(QPalette::HighlightedText, QColor(0x24, 0x14, 0x39));
    palette.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x98));
    palette.setColor(QPalette::Link, QColor(0xc5, 0xb0, 0xff));
    QApplication::setPalette(palette);
    // Dialogs and every widget without its own font: the same larger text as the library.
    QApplication::setFont(Theme::Font(13));
    app.setStyleSheet(StyleSheet());

    AfterHours::MainWindow window;
    window.resize(1500, 920);
    // Development aid: AFTERHOURS_SNAPSHOT=<file.png> saves the window once it has loaded,
    // then quits (checking the layout without looking at the screen).
    if (const QString snapshot = qEnvironmentVariable("AFTERHOURS_SNAPSHOT");
        !snapshot.isEmpty()) {
        window.resize(1920, 1080);
        window.show();
        QTimer::singleShot(9000, &window, [&window, snapshot] {
            window.grab().save(snapshot);
            QApplication::quit();
        });
        return app.exec();
    }
    window.showMaximized();
    return app.exec();
}
