#include "frameless_info_dialog.hpp"

#include <QVBoxLayout>
#include <QFrame>
#include <QPushButton>
#include <QMouseEvent>

FramelessInfoDialog::FramelessInfoDialog(QWidget* parent) : QDialog(parent) {
    // Frameless so the dark panel — not the OS title bar — is what the user sees;
    // a translucent window lets the panel's rounded corners show through.
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    panel_ = new QFrame(this);
    panel_->setObjectName(QStringLiteral("InfoPanel"));
    panel_->setMinimumWidth(330);
    panel_->setStyleSheet(QStringLiteral(
        "#InfoPanel{ background: rgb(30,34,40);"
        " border:1px solid rgba(255,255,255,40); border-radius:8px; }"
        "QLabel{ color:#e6e9ee; background:transparent; }"));
    outer->addWidget(panel_);

    panelLayout_ = new QVBoxLayout(panel_);
    panelLayout_->setContentsMargins(14, 12, 14, 14);
    panelLayout_->setSpacing(10);
}

QWidget* FramelessInfoDialog::makeCloseButton() {
    auto* b = new QPushButton(QString(QChar(0x2715)), panel_);   // ✕
    b->setFixedSize(22, 22);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip(QStringLiteral("Close"));
    b->setStyleSheet(QStringLiteral(
        "QPushButton{ border:none; background:transparent; color:rgba(230,233,238,150);"
        " font-size:14px; font-weight:600; }"
        "QPushButton:hover{ color:#e6e9ee; background: rgba(255,255,255,28);"
        " border-radius:4px; }"));
    connect(b, &QPushButton::clicked, this, &QDialog::close);
    return b;
}

void FramelessInfoDialog::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        dragging_ = true;
        dragPos_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
        e->accept();
        return;
    }
    QDialog::mousePressEvent(e);
}

void FramelessInfoDialog::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_ && (e->buttons() & Qt::LeftButton)) {
        move(e->globalPosition().toPoint() - dragPos_);
        e->accept();
        return;
    }
    QDialog::mouseMoveEvent(e);
}

void FramelessInfoDialog::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        e->accept();
        return;
    }
    QDialog::mouseReleaseEvent(e);
}
