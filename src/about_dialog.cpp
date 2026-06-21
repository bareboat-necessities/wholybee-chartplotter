#include "about_dialog.hpp"
#include "app_info.hpp"
#include "theme.hpp"
#include "window_dragger.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QTabWidget>
#include <QTextBrowser>
#include <QFile>

namespace {
// Read a bundled text resource (README / LICENSE). Returns empty on failure.
QString readResource(const QString& path) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(f.readAll());
    return QString();
}
} // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("About %1").arg(appinfo::name()));
    // Frameless + side-menu palette, matching the other restyled dialogs.
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    resize(620, 640);

    const theme::MenuPalette& t = theme::menu();

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // Bordered panel so the frameless window has a visible edge.
    auto* panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("AboutPanel"));
    panel->setStyleSheet(QStringLiteral(
        "#AboutPanel{ background:%1; border:1px solid %2; }").arg(t.panelBg, t.panelBorder));
    col->addWidget(panel);

    auto* panelCol = new QVBoxLayout(panel);
    panelCol->setContentsMargins(0, 0, 0, 0);
    panelCol->setSpacing(0);

    // Title bar (brand-navy), draggable, with a close "✕".
    auto* titleBar = new QWidget(panel);
    titleBar->setStyleSheet(QStringLiteral("background:%1;").arg(t.titleBg));
    titleBar->setCursor(Qt::SizeAllCursor);
    titleBar->installEventFilter(new WindowDragger(this));
    auto* titleRow = new QHBoxLayout(titleBar);
    titleRow->setContentsMargins(16, 8, 8, 8);
    titleRow->setSpacing(6);
    auto* titleLbl = new QLabel(QStringLiteral("About"), titleBar);
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);   // clicks fall to the bar (drag)
    titleLbl->setStyleSheet(QStringLiteral(
        "font-size:18px; font-weight:600; background:transparent; color:%1;").arg(t.titleFg));
    titleRow->addWidget(titleLbl, 1);
    auto* closeBtn = new QPushButton(QString(QChar(0x2715)), titleBar);   // ✕
    closeBtn->setFlat(true);
    closeBtn->setFixedSize(44, 44);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton{ border:none; background:transparent; color:%1; font-size:18px; }"
        "QPushButton:pressed{ background:%2; border-radius:6px; }")
        .arg(t.titleFg, t.actionPressed));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    titleRow->addWidget(closeBtn);
    panelCol->addWidget(titleBar);

    // Identity block: application name + version.
    auto* idBox = new QWidget(panel);
    auto* idCol = new QVBoxLayout(idBox);
    idCol->setContentsMargins(16, 14, 16, 12);
    idCol->setSpacing(2);
    auto* nameLbl = new QLabel(appinfo::name(), idBox);
    nameLbl->setStyleSheet(QStringLiteral("font-size:22px; font-weight:700; color:%1;").arg(t.actionFg));
    auto* verLbl = new QLabel(QStringLiteral("Version %1").arg(appinfo::version()), idBox);
    verLbl->setStyleSheet(QStringLiteral("font-size:13px; color:%1;").arg(t.hint));
    idCol->addWidget(nameLbl);
    idCol->addWidget(verLbl);
    panelCol->addWidget(idBox);

    // README + LICENSE in tabs (embedded as Qt resources so they travel with the
    // app and need no on-disk lookup).
    auto* tabs = new QTabWidget(panel);
    tabs->setDocumentMode(true);
    tabs->setStyleSheet(QStringLiteral(
        "QTabWidget::pane{ border-top:1px solid %2; background:%1; }"
        "QTabBar::tab{ background:%3; color:%4; padding:6px 16px; border:none; font-size:13px; }"
        "QTabBar::tab:selected{ background:%1; color:%5; font-weight:600; }")
        .arg(t.panelBg, t.separator, t.headerBg, t.headerFg, t.actionFg));

    auto makeBrowser = [&]() {
        auto* b = new QTextBrowser(tabs);
        b->setOpenExternalLinks(true);
        b->setFrameShape(QFrame::NoFrame);
        b->setStyleSheet(QStringLiteral(
            "QTextBrowser{ background:%1; color:%2; border:none; padding:6px 10px; }")
            .arg(t.panelBg, t.actionFg));
        return b;
    };

    auto* readme = makeBrowser();
    const QString readmeText = readResource(QStringLiteral(":/README.md"));
    if (!readmeText.isEmpty()) readme->setMarkdown(readmeText);
    else readme->setPlainText(QStringLiteral("README not available."));
    tabs->addTab(readme, QStringLiteral("Read me"));

    auto* license = makeBrowser();
    const QString licenseText = readResource(QStringLiteral(":/LICENSE"));
    license->setPlainText(licenseText.isEmpty()
        ? QStringLiteral("License not available.") : licenseText);
    tabs->addTab(license, QStringLiteral("License"));

    panelCol->addWidget(tabs, 1);
}
