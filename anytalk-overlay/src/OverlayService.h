#pragma once
#include <QObject>

class OverlayWindow;
class AsrController;

/// D-Bus surface of anytalk-overlay (short-lived).
///
///   Bus name : org.fcitx.Fcitx5.AnyTalk.Overlay
///   Path     : /overlay
///   Interface: org.fcitx.Fcitx5.AnyTalk.Overlay
///
/// Lifecycle: every F2 press fires up a fresh overlay process via D-Bus
/// auto-activation. The addon issues ToggleRecording / StopRecording /
/// CancelRecording over the bus, observes signals, and on CommitText
/// performs ic->commitString() then calls Acknowledge() to let the
/// overlay exit. No long-lived in-process state.
///
/// Methods:
///   ToggleRecording()      idempotent: start if idle, stop if active
///   StartRecording()       explicit start
///   StopRecording()        explicit stop (drain server finals → CommitText)
///   CancelRecording()      drop in-flight session, no commit; also serves
///                          as the user/addon "exit immediately" escape
///                          while the overlay is waiting for the post-
///                          commit Acknowledge
///   Acknowledge()          addon-→-overlay: commitString done, please exit
///   OpenSettings()         bring up the SettingsDialog (synchronous)
///
/// Signals:
///   StateChanged(s)        idle / connecting / recording / error
///   TranscriptPartial(s)   streaming preedit text
///   TranscriptFinal(s)     committed segment (server-side final)
///   AudioLevel(d)          0..1, ~20 Hz
///   ErrorOccurred(s)       human-readable error
///   CommitText(s)          final text ready to commit; addon must call
///                          Acknowledge() after handling so overlay can exit
///   Cancelled()            cancel/Esc completed; overlay will exit
class OverlayService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.fcitx.Fcitx5.AnyTalk.Overlay")
public:
    OverlayService(OverlayWindow *window, AsrController *asr, QObject *parent = nullptr);

    bool registerOnBus();

public slots:
    Q_SCRIPTABLE void Show();
    Q_SCRIPTABLE void Hide();
    Q_SCRIPTABLE void ToggleRecording();
    Q_SCRIPTABLE void StartRecording();
    Q_SCRIPTABLE void StopRecording();
    Q_SCRIPTABLE void CancelRecording();
    Q_SCRIPTABLE void OpenSettings();
    /// Addon → overlay: ic->commitString() finished, overlay can exit.
    Q_SCRIPTABLE void Acknowledge();

signals:
    Q_SCRIPTABLE void StateChanged(const QString &state);
    Q_SCRIPTABLE void TranscriptPartial(const QString &text);
    Q_SCRIPTABLE void TranscriptFinal(const QString &text);
    Q_SCRIPTABLE void AudioLevel(double level);
    Q_SCRIPTABLE void ErrorOccurred(const QString &text);
    /// Final text ready to commit; addon calls Acknowledge() afterwards.
    Q_SCRIPTABLE void CommitText(const QString &text);
    /// Cancel completed (Esc or addon-initiated CancelRecording).
    Q_SCRIPTABLE void Cancelled();

    /// In-process only: D-Bus method `OpenSettings` routes here; main()
    /// runs the local SettingsDialog.
    void openSettingsRequested();
    /// In-process: addon called Acknowledge.
    void ackReceived();
    /// In-process: cancel arrived while overlay was awaiting Acknowledge.
    void cancelEscape();

private:
    OverlayWindow *window_;
    AsrController *asr_;
};
