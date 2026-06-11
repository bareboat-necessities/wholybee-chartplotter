#pragma once
#include <QDialog>
#include "heading_source.hpp"

class QRadioButton;

// Chooses what the ownship glyph points along: true heading or course over
// ground. The selection is read back via source() after the dialog is accepted.
class HeadingSourceDialog : public QDialog {
    Q_OBJECT
public:
    explicit HeadingSourceDialog(HeadingSource current, QWidget* parent = nullptr);

    HeadingSource source() const;

private:
    QRadioButton* headingRadio_ = nullptr;
    QRadioButton* cogRadio_ = nullptr;
};
