#pragma once
#include <QDialog>
#include <vector>
#include "route_types.hpp"

class QLineEdit;
class QScrollArea;
class QWidget;
class QVBoxLayout;
class QLabel;

// Properties editor for a single route. Operates on a working copy: name,
// description, and the ordered point list are edited here and only committed by
// the caller when the dialog is accepted (OK). Each point row exposes editable
// latitude/longitude fields, a Delete button, and an Edit button that asks the
// host to let the user drag the point on the chart (editPointRequested).
//
// The host (MainWindow) drives the drag round-trip: on editPointRequested it
// reads currentRoute(), hides this dialog, runs the chart drag, then calls
// setRoute() with the updated points and re-shows the dialog.
class RoutePropertiesDialog : public QDialog {
    Q_OBJECT
public:
    RoutePropertiesDialog(const Route& route, QWidget* parent = nullptr);

    // Current edited state (name/description from the fields, points from the
    // row lat/lon editors). Keeps the original id / created / visible.
    Route currentRoute() const;
    // Replace the working state and rebuild the rows (used after a chart drag).
    void setRoute(const Route& route);

signals:
    void editPointRequested(int index);   // user tapped a row's Edit (drag) button

private:
    struct Row {
        QWidget*   widget = nullptr;
        QLineEdit* lat = nullptr;
        QLineEdit* lon = nullptr;
    };
    void rebuildRows();
    void commitFieldsToWorking();   // pull name/desc/coords from widgets into work_
    void onDeletePoint(int index);
    void onOk();

    Route work_;                    // working copy (keeps id/created/visible)
    QLineEdit*   nameEdit_ = nullptr;
    QLineEdit*   descEdit_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget*     rowContainer_ = nullptr;
    QVBoxLayout* rowLayout_ = nullptr;
    QLabel*      countLabel_ = nullptr;
    std::vector<Row> rows_;
};
