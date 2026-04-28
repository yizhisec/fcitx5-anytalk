#pragma once
#include <QString>
#include <QWidget>

class AuroraBars;
class StatusDot;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QLabel;
class QPaintEvent;
class QPropertyAnimation;
class QShowEvent;

class OverlayWindow : public QWidget {
    Q_OBJECT
public:
    explicit OverlayWindow(QWidget *parent = nullptr);

public slots:
    void onStateChanged(const QString &state);
    void onAudioLevel(double level);
    void onTranscriptPartial(const QString &text);
    void onTranscriptFinal(const QString &text);
    void onErrorOccurred(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Vis { Hidden, Active, Error };

    void enterListening(bool connecting);
    void enterError(const QString &text);
    void enterHidden();

    void setTranscript(const QString &text, bool dim);

    void fadeIn();
    void fadeOut();
    void positionAtBottom();
    void configureLayerShell();

    StatusDot *statusDot_ = nullptr;
    AuroraBars *bars_ = nullptr;
    QLabel *transcriptLabel_ = nullptr;
    QLabel *hintLabel_ = nullptr;

    QGraphicsOpacityEffect *fadeEffect_ = nullptr;
    QPropertyAnimation *fadeAnim_ = nullptr;

    Vis vis_ = Vis::Hidden;
    QString partialText_;
    QString finalText_;
};
