#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;

// Small touch-friendly dialog to name a route or waypoint, with an optional
// description. Used when completing a route or creating/dropping a waypoint.
class NameDialog : public QDialog {
    Q_OBJECT
public:
    NameDialog(const QString& title, const QString& initialName,
               const QString& initialDescription, QWidget* parent = nullptr);

    QString name() const;
    QString description() const;

private:
    QLineEdit* nameEdit_ = nullptr;
    QLineEdit* descEdit_ = nullptr;
};
