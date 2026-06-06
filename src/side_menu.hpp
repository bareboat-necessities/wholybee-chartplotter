#pragma once
#include <QWidget>
#include <QString>

class Settings;
class QPushButton;
class QLabel;
class QPropertyAnimation;

// Touch-first navigation drawer that slides in over the chart from the left.
//
// Built to the touch UI requirements in ProjectSpec.md: large tap targets,
// no hover-only or right-click interactions, and one-touch dismissal by tapping
// the dimmed scrim. Display toggles are bound to the core Settings object, so
// changing them publishes through the normal settings channel rather than
// reaching into the chart view directly.
class SideMenu : public QWidget {
    Q_OBJECT
public:
    SideMenu(Settings* settings, QWidget* parent);

    void openMenu();
    void closeMenu();
    bool isOpen() const { return open_; }

signals:
    void selectFolderRequested();
    void rescanRequested();
    void fitRequested();

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    QLabel*      makeHeader(const QString& text);
    QPushButton* makeAction(const QString& text);
    QPushButton* makeToggle(const QString& text, bool checked);
    void layoutPanel();

    Settings* settings_ = nullptr;
    QWidget*  scrim_ = nullptr;   // dim layer; tap to dismiss
    QWidget*  panel_ = nullptr;   // the menu surface itself
    QPropertyAnimation* anim_ = nullptr;
    int  panelWidth_ = 320;
    bool open_ = false;
};
