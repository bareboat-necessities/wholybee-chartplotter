#include "nmea0183_debug_window.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFontDatabase>

Nmea0183DebugWindow::Nmea0183DebugWindow(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("NMEA 0183 Debug"));
    resize(620, 420);
    // A real top-level window (own min/max/close), modeless via show().
    setWindowFlag(Qt::Window, true);

    auto* col = new QVBoxLayout(this);

    auto* row = new QHBoxLayout;
    pauseBtn_ = new QPushButton(QStringLiteral("Pause"));
    auto* clearBtn = new QPushButton(QStringLiteral("Clear"));
    for (QPushButton* b : {pauseBtn_, clearBtn}) b->setMinimumHeight(36);
    pauseBtn_->setCheckable(true);
    row->addWidget(pauseBtn_);
    row->addWidget(clearBtn);
    row->addStretch(1);
    col->addLayout(row);

    view_ = new QPlainTextEdit;
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(50);                 // ~50-line scrollback
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    col->addWidget(view_, 1);

    connect(pauseBtn_, &QPushButton::toggled, this, [this](bool on) {
        paused_ = on;
        pauseBtn_->setText(on ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    });
    connect(clearBtn, &QPushButton::clicked, view_, &QPlainTextEdit::clear);
}

void Nmea0183DebugWindow::appendLine(const QString& line) {
    if (paused_) return;                             // freeze the scroll to read
    view_->appendPlainText(line);
}
