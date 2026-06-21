#pragma once
#include <QDialog>
#include <QPoint>

class QFrame;
class QVBoxLayout;
class QPushButton;
class QMouseEvent;

// Shared base for the chart's floating info windows (AIS target, chart object).
// Gives them the dark "instrument panel" look of the nav display window and the
// instruments plugin: a frameless, translucent top-level window whose whole
// surface is a single rounded, bordered dark panel — no OS title bar, so the
// panel (not the system chrome) is what the user sees.
//
// Because there is no title bar, the panel is draggable anywhere (plain labels
// and tiles don't grab the mouse, so the press falls through to here) and the
// subclass must supply a close button via makeCloseButton(). Subclasses build
// their content into panelLayout().
class FramelessInfoDialog : public QDialog {
    Q_OBJECT
public:
    explicit FramelessInfoDialog(QWidget* parent = nullptr);

protected:
    // The rounded dark panel and its top-level vertical layout. Add content to
    // panelLayout(); style child QLabels against the dark panel background.
    QFrame*      panel() const       { return panel_; }
    QVBoxLayout* panelLayout() const { return panelLayout_; }

    // A small "✕" button styled for the panel and pre-wired to close(). The
    // caller positions it within its own header layout (returned as QWidget* so
    // callers needn't pull in QPushButton just to add it).
    QWidget* makeCloseButton();

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QFrame*      panel_       = nullptr;
    QVBoxLayout* panelLayout_ = nullptr;
    bool         dragging_    = false;
    QPoint       dragPos_;        // cursor offset from the window's top-left
};
