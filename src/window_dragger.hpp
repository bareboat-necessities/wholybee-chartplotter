#pragma once
#include <QObject>
#include <QWidget>
#include <QEvent>
#include <QMouseEvent>
#include <QPoint>

// Makes a frameless window draggable by one of its child widgets (typically a
// title bar). Install it as an event filter on that widget: a left-press starts
// the drag and subsequent moves reposition the top-level window. No Q_OBJECT is
// needed — it only overrides the virtual eventFilter(), so it requires no moc.
//
// Parent it to the window so it is destroyed with it. Child labels on the handle
// should be Qt::WA_TransparentForMouseEvents so the press reaches the handle.
class WindowDragger : public QObject {
public:
    explicit WindowDragger(QWidget* window)
        : QObject(window), window_(window) {}

protected:
    bool eventFilter(QObject* obj, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton) {
                dragging_ = true;
                offset_ = me->globalPosition().toPoint() - window_->frameGeometry().topLeft();
                return true;
            }
        } else if (e->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (dragging_ && (me->buttons() & Qt::LeftButton)) {
                window_->move(me->globalPosition().toPoint() - offset_);
                return true;
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::LeftButton && dragging_) {
                dragging_ = false;
                return true;
            }
        }
        return QObject::eventFilter(obj, e);
    }

private:
    QWidget* window_   = nullptr;
    bool     dragging_ = false;
    QPoint   offset_;
};
