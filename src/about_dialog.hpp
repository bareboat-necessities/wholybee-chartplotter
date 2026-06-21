#pragma once
#include <QDialog>

// Frameless "About" dialog styled with the side-menu palette (light/dark aware),
// matching the chart-object chooser and AIS list. Shows the application name and
// version, plus the bundled README and LICENSE (embedded as Qt resources) in two
// tabs. Draggable by its title bar; dismissed via the title-bar ✕ or Esc.
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
