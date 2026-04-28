#include "AuroraBars.h"
#include "Theme.h"
#include <QPainter>
#include <QLinearGradient>
#include <QTimer>
#include <cmath>

AuroraBars::AuroraBars(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(Theme::BAR_AREA_WIDTH, Theme::BAR_AREA_HEIGHT);
    clock_.start();

    // ~60 fps refresh, the noise term needs continuous repaint even when level is steady.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    timer->start(16);
}

void AuroraBars::setLevel(double level) {
    level_ = std::clamp(level, 0.0, 1.0);
}

void AuroraBars::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double t = clock_.elapsed() / 1000.0;
    const int N = Theme::BAR_COUNT;
    const double w = static_cast<double>(width());
    const double h = static_cast<double>(height());
    const double sideMargin = 4.0;
    const double barW = (w - sideMargin * 2.0) / N;

    QLinearGradient g(0, 0, w, 0);
    g.setColorAt(0.0, Theme::accent());
    g.setColorAt(0.5, Theme::accentDeep());
    g.setColorAt(1.0, Theme::accentDark());
    p.setBrush(g);
    p.setPen(Qt::NoPen);

    for (int i = 0; i < N; ++i) {
        const double noise =
            0.45 +
            0.30 * std::sin(i * 0.4 + t * 3.0) +
            0.25 * std::sin(i * 0.9 - t * 2.1);
        const double scale = std::max(0.15, noise);
        // Always show a baseline "breathing" minimum so the dock doesn't feel
        // dead even when audio level is 0.
        const double minH = 3.0 + 2.0 * std::abs(std::sin(t * 1.6 + i * 0.2));
        const double bh = std::max(minH, level_ * (h - 6.0) * scale);
        const double x = sideMargin + i * barW;
        const double y = (h - bh) / 2.0;
        const QRectF rect(x + 0.5, y, barW - 1.5, bh);
        const double r = (barW - 1.5) / 2.0;
        p.setOpacity(0.85);
        p.drawRoundedRect(rect, r, r);
    }
    p.setOpacity(1.0);
}
