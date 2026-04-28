#include "OverlayWindow.h"
#include "AuroraBars.h"
#include "StatusDot.h"
#include "Theme.h"

#include <QApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
/// Resolve the screen the user is currently on (cursor location). Falls
/// back to primary if the cursor isn't over any screen (rare).
QScreen *currentScreen() {
    if (auto *s = QGuiApplication::screenAt(QCursor::pos())) return s;
    return QGuiApplication::primaryScreen();
}
} // namespace

#if __has_include(<LayerShellQt/Window>)
#  include <LayerShellQt/Window>
#  define ANYTALK_HAS_LAYER_SHELL 1
#endif

OverlayWindow::OverlayWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::CARD_PAD_X, Theme::CARD_PAD_Y,
                              Theme::CARD_PAD_X, Theme::CARD_PAD_Y);
    root->setSpacing(8);

    // ── Top row: status dot + bars on the left, hint on the right ─────
    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(12);

    statusDot_ = new StatusDot(this);
    topRow->addWidget(statusDot_, 0, Qt::AlignVCenter);

    bars_ = new AuroraBars(this);
    bars_->setFixedSize(Theme::BAR_AREA_WIDTH, Theme::BAR_AREA_HEIGHT);
    topRow->addWidget(bars_, 0, Qt::AlignVCenter);

    topRow->addStretch(1);

    hintLabel_ = new QLabel(this);
    hintLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    hintLabel_->setTextFormat(Qt::RichText);
    hintLabel_->setText(QStringLiteral(
        "<span style='color:rgba(255,255,255,110); font-family:\"JetBrains Mono\",monospace; font-size:11px;'>"
        "F2&nbsp;·&nbsp;Esc"
        "</span>"));
    topRow->addWidget(hintLabel_, 0, Qt::AlignVCenter);

    root->addLayout(topRow);

    // ── Bottom row: transcript, wraps inside the dock ─────────────────
    transcriptLabel_ = new QLabel(this);
    transcriptLabel_->setMinimumWidth(Theme::TRANSCRIPT_MIN_WIDTH);
    transcriptLabel_->setText(QStringLiteral("说点什么…"));
    transcriptLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    transcriptLabel_->setWordWrap(true);
    {
        QFont f("Noto Sans CJK SC");
        f.setPixelSize(15);
        transcriptLabel_->setFont(f);
        transcriptLabel_->setStyleSheet(
            QString("color: rgba(255,255,255,%1);")
                .arg(Theme::textPlaceholder().alpha()));
    }
    root->addWidget(transcriptLabel_);

    fadeEffect_ = new QGraphicsOpacityEffect(this);
    fadeEffect_->setOpacity(0.0);
    setGraphicsEffect(fadeEffect_);
    fadeAnim_ = new QPropertyAnimation(fadeEffect_, "opacity", this);
    fadeAnim_->setDuration(Theme::FADE_MS);
}

// ---------- State transitions ----------

void OverlayWindow::onStateChanged(const QString &state) {
    if (state == QStringLiteral("recording")) {
        enterListening(/*connecting=*/false);
    } else if (state == QStringLiteral("connecting")) {
        enterListening(/*connecting=*/true);
    } else if (state == QStringLiteral("error")) {
        if (vis_ != Vis::Error) vis_ = Vis::Error;
    } else {
        enterHidden();
    }
}

void OverlayWindow::onErrorOccurred(const QString &text) { enterError(text); }

void OverlayWindow::enterListening(bool connecting) {
    vis_ = Vis::Active;
    statusDot_->setMode(connecting ? StatusDot::Mode::Connecting
                                    : StatusDot::Mode::Recording);
    partialText_.clear();
    finalText_.clear();
    bars_->setLevel(0.0);
    setTranscript(connecting ? QStringLiteral("正在连接…")
                              : QStringLiteral("说点什么…"),
                  /*dim=*/true);
    fadeIn();
}

void OverlayWindow::enterError(const QString &text) {
    vis_ = Vis::Error;
    statusDot_->setMode(StatusDot::Mode::Error);
    bars_->setLevel(0.0);
    setTranscript(text.isEmpty() ? QStringLiteral("⚠ 麦克风不可用")
                                  : QStringLiteral("⚠ ") + text,
                  /*dim=*/false);
    transcriptLabel_->setStyleSheet(
        QString("color: %1;").arg(Theme::errorColor().name()));
    // No auto-hide — user dismisses with F2 / Esc.
    fadeIn();
}

void OverlayWindow::enterHidden() {
    vis_ = Vis::Hidden;
    statusDot_->setMode(StatusDot::Mode::Idle);
    fadeOut();
}

// Truncate from the front so wrapped text fits in TRANSCRIPT_MAX_LINES.
// Returns the (possibly trimmed) string. The full utterance is preserved
// elsewhere — this function only affects what the label renders.
static QString fitToLines(const QString &text, const QFont &font, int wrapWidth, int maxLines) {
    QFontMetrics fm(font);
    const int maxH = fm.lineSpacing() * maxLines;
    auto fits = [&](const QString &s) {
        const QRect r = fm.boundingRect(0, 0, wrapWidth, INT_MAX,
                                         Qt::TextWordWrap, s);
        return r.height() <= maxH;
    };
    if (fits(text)) return text;
    // Binary search the longest tail that still fits, then prepend "…".
    int lo = 0;
    int hi = text.length();
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        const QString candidate = QStringLiteral("…") + text.right(text.length() - mid);
        if (fits(candidate)) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return QStringLiteral("…") + text.right(text.length() - lo);
}

void OverlayWindow::setTranscript(const QString &text, bool dim) {
    const int wrapWidth = transcriptLabel_->maximumWidth() > 0
                              ? transcriptLabel_->maximumWidth() - 4
                              : Theme::TRANSCRIPT_MIN_WIDTH;
    const QString display =
        fitToLines(text, transcriptLabel_->font(), wrapWidth,
                   Theme::TRANSCRIPT_MAX_LINES);
    transcriptLabel_->setText(display);
    if (dim) {
        transcriptLabel_->setStyleSheet(
            QString("color: rgba(255,255,255,%1);")
                .arg(Theme::textPlaceholder().alpha()));
    } else {
        transcriptLabel_->setStyleSheet(
            QString("color: rgba(255,255,255,%1);")
                .arg(Theme::textPrimary().alpha()));
    }
    adjustSize();
}

// ---------- Streaming inputs ----------

void OverlayWindow::onAudioLevel(double level) {
    if (vis_ != Vis::Active) return;
    bars_->setLevel(level);
}

void OverlayWindow::onTranscriptPartial(const QString &text) {
    if (vis_ != Vis::Active) return;
    partialText_ = text;
    setTranscript(finalText_ + partialText_, /*dim=*/false);
}

void OverlayWindow::onTranscriptFinal(const QString &text) {
    if (vis_ != Vis::Active) return;
    finalText_ += text;
    partialText_.clear();
    setTranscript(finalText_, /*dim=*/false);
}

// ---------- Window plumbing ----------

void OverlayWindow::positionAtBottom() {
    auto *screen = currentScreen();
    if (!screen) return;
    const QRect avail = screen->availableGeometry();
    // Recompute transcript wrap width as 2/3 of the active screen and re-fit.
    const int wrap = std::max(Theme::TRANSCRIPT_MIN_WIDTH,
                               static_cast<int>(avail.width() *
                                                Theme::TRANSCRIPT_WIDTH_FRACTION));
    transcriptLabel_->setMaximumWidth(wrap);
    setMaximumWidth(wrap + Theme::CARD_PAD_X * 2);

    adjustSize();
    const int x = avail.x() + (avail.width() - width()) / 2;
    const int y = avail.y() + avail.height() - height() - Theme::CARD_BOTTOM_MARGIN;
    move(x, y);
}

void OverlayWindow::configureLayerShell() {
#ifdef ANYTALK_HAS_LAYER_SHELL
    auto *handle = windowHandle();
    if (!handle) return;
    auto *ls = LayerShellQt::Window::get(handle);
    if (!ls) return;

    auto *screen = currentScreen();

    ls->setLayer(LayerShellQt::Window::LayerOverlay);
    ls->setAnchors(LayerShellQt::Window::AnchorBottom);
    ls->setMargins(QMargins(0, 0, 0, Theme::CARD_BOTTOM_MARGIN));
    ls->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    ls->setExclusiveZone(0);
    ls->setScope(QStringLiteral("anytalk-overlay"));
    if (screen) ls->setScreen(screen);

    // Constrain transcript wrap width to 2/3 of the *active* screen.
    if (screen) {
        const int wrap = std::max(Theme::TRANSCRIPT_MIN_WIDTH,
                                   static_cast<int>(screen->geometry().width() *
                                                    Theme::TRANSCRIPT_WIDTH_FRACTION));
        transcriptLabel_->setMaximumWidth(wrap);
        setMaximumWidth(wrap + Theme::CARD_PAD_X * 2);
        adjustSize();
    }
#endif
}

void OverlayWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (qgetenv("XDG_SESSION_TYPE") == "wayland") {
        configureLayerShell();
    } else {
        positionAtBottom();
    }
}

void OverlayWindow::fadeIn() {
    if (!isVisible()) {
        fadeEffect_->setOpacity(0.0);
        show();
    }
    fadeAnim_->stop();
    fadeAnim_->setStartValue(fadeEffect_->opacity());
    fadeAnim_->setEndValue(1.0);
    fadeAnim_->start();
}

void OverlayWindow::fadeOut() {
    if (!isVisible()) return;
    fadeAnim_->stop();
    fadeAnim_->setStartValue(fadeEffect_->opacity());
    fadeAnim_->setEndValue(0.0);
    disconnect(fadeAnim_, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(fadeAnim_, &QPropertyAnimation::finished, this, [this]() {
        if (fadeEffect_->opacity() <= 0.01) hide();
    });
    fadeAnim_->start();
}

void OverlayWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        enterHidden();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void OverlayWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1),
                         Theme::CARD_RADIUS, Theme::CARD_RADIUS);
    p.fillPath(path, Theme::cardBg());

    QPen stroke(Theme::cardStroke());
    stroke.setWidthF(1.0);
    p.setPen(stroke);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}
