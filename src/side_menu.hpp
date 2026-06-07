#pragma once
#include <QWidget>
#include <QString>

class Settings;
class QPushButton;
class QLabel;
class QStackedWidget;
class QVBoxLayout;
class QPropertyAnimation;

// Touch-first navigation drawer that slides in over the chart from the left.
//
// Built to the touch UI requirements in ProjectSpec.md: large tap targets,
// no hover-only or right-click interactions, and one-touch dismissal by tapping
// the dimmed scrim. Display toggles are bound to the core Settings object, so
// changing them publishes through the normal settings channel rather than
// reaching into the chart view directly.
//
// The panel has two pages: the main menu (chart sets + view options) and a
// Settings page reached via the "Settings" item. The main page lists the user's
// defined chart sets so they can switch charts with one tap.
class SideMenu : public QWidget {
    Q_OBJECT
public:
    SideMenu(Settings* settings, QWidget* parent);

    void openMenu();
    void closeMenu();
    bool isOpen() const { return open_; }

    // Reflect the view's auto-follow state in the menu's checkmark (e.g. when a
    // pan turns it off). Does not re-emit autoFollowToggled.
    void setAutoFollowChecked(bool on);

    // Show/clear a green dot next to the NMEA 0183 item while it is decoding.
    void setNmeaActive(bool on);

signals:
    void fitRequested();
    void centerOnOwnshipRequested();                  // recenter on ownship
    void autoFollowToggled(bool on);                  // auto-follow on/off
    void chartSetSelected(const QString& directory);  // user tapped a set to load
    void manageChartSetsRequested();                  // open the Chart Sets dialog
    void basemapFolderRequested();                    // pick the GSHHG data folder
    void editUnitsRequested();                        // open the Units dialog
    void editStaleThresholdsRequested();              // open stale-data dialog
    void editOwnshipPredictionRequested();            // open predictor-length dialog
    void editNmeaRequested();                         // open NMEA 0183 dialog

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    QLabel*      makeHeader(const QString& text);
    QPushButton* makeAction(const QString& text);
    QPushButton* makeToggle(const QString& text, bool checked);
    // Checkable action that shows a check mark when on (like the active set).
    QPushButton* makeCheckAction(const QString& text, bool checked);
    QWidget*     buildMainPage();
    QWidget*     buildSettingsPage();
    QWidget*     wrapScroll(QWidget* content);   // scrollable container for a page
    void rebuildChartSets();
    void showMainPage();
    void showSettingsPage();
    void layoutPanel();

    Settings* settings_ = nullptr;
    QWidget*  scrim_ = nullptr;   // dim layer; tap to dismiss
    QWidget*  panel_ = nullptr;   // the menu surface itself
    QLabel*   title_ = nullptr;   // top bar, reflects the current page
    QStackedWidget* stack_ = nullptr;
    QVBoxLayout* chartSetsBox_ = nullptr;   // container for the dynamic set buttons
    QPushButton* autoFollowBtn_ = nullptr;  // checkable Auto Follow item
    QPushButton* nmeaBtn_ = nullptr;        // NMEA 0183 item (carries status dot)
    QPropertyAnimation* anim_ = nullptr;
    int  panelWidth_ = 320;
    bool open_ = false;
    int  mainIndex_ = 0;
    int  settingsIndex_ = 1;
};
