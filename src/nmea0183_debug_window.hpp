#pragma once
#include <QDialog>

class QPlainTextEdit;
class QPushButton;

// Modeless window showing raw NMEA 0183 lines as they arrive. Not a decoder —
// just a scrolling tail of the feed (about 50 lines) with a Pause toggle so the
// user can stop the scroll to read, and a Clear button.
class Nmea0183DebugWindow : public QDialog {
    Q_OBJECT
public:
    explicit Nmea0183DebugWindow(QWidget* parent = nullptr);

public slots:
    void appendLine(const QString& line);     // received lines (default colour)
    void appendTxLine(const QString& line);   // transmitted lines (distinct colour)

private:
    QPlainTextEdit* view_ = nullptr;
    QPushButton*    pauseBtn_ = nullptr;
    bool            paused_ = false;
};
