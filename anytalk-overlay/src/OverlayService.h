#pragma once
#include <QObject>

class OverlayWindow;
class AsrController;

/// D-Bus surface of anytalk-overlay.
///
///   Bus name : org.fcitx.Fcitx5.AnyTalk.Overlay
///   Path     : /overlay
///   Interface: org.fcitx.Fcitx5.AnyTalk.Overlay
///
/// Methods (called by the fcitx5 addon over D-Bus):
///   StartRecording()
///   StopRecording()
///   CancelRecording()
///   Show()/Hide()/Ping()  — leftover from Phase 2; kept for diagnostics
///
/// Signals (broadcast to the addon and any other observer):
///   StateChanged(s)        idle / connecting / recording
///   TranscriptPartial(s)   streaming preedit text
///   TranscriptFinal(s)     committed segment
///   AudioLevel(d)          0..1, ~20 Hz
///   ErrorOccurred(s)       human-readable error
class OverlayService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.fcitx.Fcitx5.AnyTalk.Overlay")
public:
    OverlayService(OverlayWindow *window, AsrController *asr, QObject *parent = nullptr);

    bool registerOnBus();

public slots:
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    Q_SCRIPTABLE void Ping();
    Q_SCRIPTABLE void StartRecording();
    Q_SCRIPTABLE void StopRecording();
    Q_SCRIPTABLE void CancelRecording();
    Q_SCRIPTABLE void OpenSettings();

signals:
    Q_SCRIPTABLE void StateChanged(const QString &state);
    Q_SCRIPTABLE void TranscriptPartial(const QString &text);
    Q_SCRIPTABLE void TranscriptFinal(const QString &text);
    Q_SCRIPTABLE void AudioLevel(double level);
    Q_SCRIPTABLE void ErrorOccurred(const QString &text);
    /// Final text ready to be committed. Replaces the streaming preedit path:
    /// the addon performs a single ic->commitString(text) when this arrives.
    Q_SCRIPTABLE void CommitText(const QString &text);

    /// In-process: SettingsDialog should open. Not a D-Bus signal — the
    /// dialog is local UI; D-Bus method `OpenSettings()` triggers it.
    void openSettingsRequested();

private:
    OverlayWindow *window_;
    AsrController *asr_;
};
