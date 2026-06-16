#pragma once
#include <QDialog>
#include <vector>

class RouteStore;
class QScrollArea;
class QWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QVBoxLayout;

// Modeless list of saved waypoints. Same touch-first drag-to-scroll pattern as
// the route list: each row shows name and position, a Visible checkbox toggles
// chart display, tapping a row selects it, and Delete removes the selected one.
class WaypointListDialog : public QDialog {
    Q_OBJECT
public:
    WaypointListDialog(RouteStore* store, bool pickMode = false, QWidget* parent = nullptr);

signals:
    void waypointPicked(qint64 id);       // pick mode: user chose a waypoint
    void propertiesRequested(qint64 id);  // open the Properties editor for a waypoint
    void editRequested(qint64 id);        // start the chart-drag edit session
    void newWaypointAtOwnshipRequested(); // "Drop at boat" button

public slots:
    void refresh();

private:
    struct Row {
        QPushButton* btn  = nullptr;
        QLabel*      name = nullptr;
        QLabel*      pos  = nullptr;
        QCheckBox*   vis  = nullptr;
        qint64       id   = -1;
    };
    Row  makeRow();
    void selectRow(qint64 id);
    void restyleRows();
    void deleteSelected();

    RouteStore*  store_       = nullptr;
    bool         pickMode_    = false;
    qint64       selectedId_  = -1;
    QScrollArea* scrollArea_  = nullptr;
    QWidget*     rowContainer_= nullptr;
    QVBoxLayout* rowLayout_   = nullptr;
    QLabel*      countLabel_  = nullptr;
    QPushButton* deleteBtn_   = nullptr;
    QPushButton* propsBtn_    = nullptr;
    QPushButton* editBtn_     = nullptr;
    QPushButton* dropBtn_     = nullptr;
    std::vector<Row> rows_;
};
