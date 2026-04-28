#pragma once
#include <QColor>
#include <QWidget>

class QPaintEvent;
class QPropertyAnimation;

/// Small coloured circle that doubles as a state indicator.
/// Pulses when recording (opacity loop), steady otherwise.
class StatusDot : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulse READ pulse WRITE setPulse)
public:
    explicit StatusDot(QWidget *parent = nullptr);

    enum class Mode { Idle, Connecting, Recording, Error };
    void setMode(Mode mode);

    qreal pulse() const { return pulse_; }
    void setPulse(qreal p);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Mode mode_ = Mode::Idle;
    qreal pulse_ = 1.0;
    QPropertyAnimation *anim_;
    QColor colorFor(Mode mode) const;
};
