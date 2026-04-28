#include "StatusDot.h"
#include "Theme.h"

#include <QEasingCurve>
#include <QPainter>
#include <QPropertyAnimation>

StatusDot::StatusDot(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(Theme::STATUS_DOT_DIAMETER + 4, Theme::STATUS_DOT_DIAMETER + 4);
    anim_ = new QPropertyAnimation(this, "pulse", this);
    anim_->setDuration(1100);
    anim_->setStartValue(1.0);
    anim_->setKeyValueAt(0.5, 0.35);
    anim_->setEndValue(1.0);
    anim_->setEasingCurve(QEasingCurve::InOutSine);
    anim_->setLoopCount(-1);
}

void StatusDot::setPulse(qreal p) {
    pulse_ = p;
    update();
}

void StatusDot::setMode(Mode mode) {
    mode_ = mode;
    if (mode == Mode::Recording || mode == Mode::Connecting) {
        if (anim_->state() != QAbstractAnimation::Running) anim_->start();
    } else {
        anim_->stop();
        pulse_ = 1.0;
    }
    update();
}

QColor StatusDot::colorFor(Mode mode) const {
    switch (mode) {
    case Mode::Connecting: return Theme::connectingColor();
    case Mode::Recording: return Theme::errorColor(); // recording = mic-active red
    case Mode::Error: return Theme::errorColor();
    case Mode::Idle:
    default: return Theme::textDim();
    }
}

void StatusDot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPointF c(width() / 2.0, height() / 2.0);
    const qreal r = Theme::STATUS_DOT_DIAMETER / 2.0;

    QColor col = colorFor(mode_);
    QColor coreCol = col;
    coreCol.setAlphaF(pulse_);
    QColor glowCol = col;
    glowCol.setAlphaF(0.35 * pulse_);

    // Soft glow ring
    p.setPen(Qt::NoPen);
    p.setBrush(glowCol);
    p.drawEllipse(c, r + 2, r + 2);

    // Solid dot
    p.setBrush(coreCol);
    p.drawEllipse(c, r, r);
}
