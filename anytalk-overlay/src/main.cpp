#include "AsrController.h"
#include "Config.h"
#include "OverlayService.h"
#include "OverlayState.h"
#include "OverlayWindow.h"
#include "SettingsDialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

#if __has_include(<LayerShellQt/Shell>)
#  include <LayerShellQt/Shell>
#  define ANYTALK_HAS_LAYER_SHELL 1
#endif

namespace {

/// Show the SettingsDialog and, on Save, push the new config into the
/// running AsrController so the user can record without restarting the
/// overlay.
bool runSettingsDialog(AsrController &asr) {
    SettingsDialog dlg(OverlayConfig::load());
    if (dlg.exec() != QDialog::Accepted) return false;
    asr.applyConfig(dlg.config());
    return true;
}

} // namespace

int main(int argc, char **argv) {
#ifdef ANYTALK_HAS_LAYER_SHELL
    if (qgetenv("XDG_SESSION_TYPE") == "wayland") {
        LayerShellQt::Shell::useLayerShell();
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationName("anytalk-overlay");
    app.setApplicationVersion("0.4.0");
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription("Aurora-style voice activation overlay for fcitx5-anytalk");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption settingsOption(QStringLiteral("settings"),
                                       QStringLiteral("Open the settings dialog and exit."));
    parser.addOption(settingsOption);
    parser.process(app);

    OverlayWindow overlay;

    AsrController asr;
    OverlayConfig cfg = OverlayConfig::load();
    if (!asr.applyConfig(cfg)) {
        qWarning() << "anytalk-overlay: ASR backend not configured. The first F2 will "
                      "open the settings dialog.";
    }

    // CLI-driven settings: launch dialog and exit.
    if (parser.isSet(settingsOption)) {
        return runSettingsDialog(asr) ? 0 : 1;
    }

    OverlayService service(&overlay, &asr);
    if (!service.registerOnBus()) {
        qWarning() << "anytalk-overlay: D-Bus registration failed; another "
                      "instance may already own the name.";
        return 1;
    }

    // Announce liveness so any subscriber holding stale state from a
    // previously-killed overlay (notably the fcitx5 addon's cached
    // current_state_) resets immediately.
    emit service.StateChanged(state::Idle);

    // Drive local UI from ASR events.
    QObject::connect(&asr, &AsrController::stateChanged,
                     &overlay, &OverlayWindow::onStateChanged);
    QObject::connect(&asr, &AsrController::audioLevel,
                     &overlay, &OverlayWindow::onAudioLevel);
    QObject::connect(&asr, &AsrController::transcriptPartial,
                     &overlay, &OverlayWindow::onTranscriptPartial);
    QObject::connect(&asr, &AsrController::transcriptFinal,
                     &overlay, &OverlayWindow::onTranscriptFinal);
    QObject::connect(&asr, &AsrController::errorOccurred,
                     &overlay, &OverlayWindow::onErrorOccurred);

    // Re-broadcast on D-Bus.
    QObject::connect(&asr, &AsrController::stateChanged, &service,
                     &OverlayService::StateChanged);
    QObject::connect(&asr, &AsrController::audioLevel, &service,
                     &OverlayService::AudioLevel);
    QObject::connect(&asr, &AsrController::transcriptPartial, &service,
                     &OverlayService::TranscriptPartial);
    QObject::connect(&asr, &AsrController::transcriptFinal, &service,
                     &OverlayService::TranscriptFinal);
    QObject::connect(&asr, &AsrController::errorOccurred, &service,
                     &OverlayService::ErrorOccurred);
    QObject::connect(&asr, &AsrController::commitText, &service,
                     &OverlayService::CommitText);

    // Settings dialog can be triggered through the addon (or any client) via
    // OverlayService::OpenSettings → openSettingsRequested.
    QObject::connect(&service, &OverlayService::openSettingsRequested, &app,
                     [&asr]() { runSettingsDialog(asr); });

    return app.exec();
}
