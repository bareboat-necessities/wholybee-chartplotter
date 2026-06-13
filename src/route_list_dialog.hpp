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

// Modeless list of saved routes. Touch-first: drag-to-scroll via QScroller on a
// QScrollArea, matching the AIS list. Each row shows the route name, its point
// count, and a Visible checkbox that toggles whether the route draws on the
// chart. Tapping a row selects it (highlight); Delete removes the selected route.
//
// In pick mode (used by Edit Route) a row tap instead emits routePicked() and
// closes — the Delete button is hidden.
class RouteListDialog : public QDialog {
    Q_OBJECT
public:
    RouteListDialog(RouteStore* store, bool pickMode, QWidget* parent = nullptr);

signals:
    void routePicked(qint64 id);          // pick mode: user chose a route
    void propertiesRequested(qint64 id);  // open the Properties editor for a route

private slots:
    void refresh();

private:
    struct Row {
        QPushButton* btn  = nullptr;
        QLabel*      name = nullptr;
        QLabel*      meta = nullptr;
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
    std::vector<Row> rows_;
};
