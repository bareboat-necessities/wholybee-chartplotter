#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;

// Dialog for entering the user's own vessel MMSI.
// Accepts exactly 9 digits or an empty field (to clear). OK is enabled only
// when the field is empty or contains a valid 9-digit MMSI.
class OwnshipMmsiDialog : public QDialog {
    Q_OBJECT
public:
    explicit OwnshipMmsiDialog(const QString& currentMmsi, QWidget* parent = nullptr);

    // Returns the entered MMSI (9-digit string) or an empty string if cleared.
    QString mmsi() const;

private:
    void updateOkState();

    QLineEdit*   edit_ = nullptr;
    QPushButton* okBtn_ = nullptr;
};
