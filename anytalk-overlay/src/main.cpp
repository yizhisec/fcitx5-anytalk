#include "AsrController.h"
#include "Config.h"
#include "OverlayService.h"
#include "OverlayWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

#if __has_include(<LayerShellQt/Shell>)
#  include <LayerShellQt/Shell>
#  define ANYTALK_HAS_LAYER_SHELL 1
#endif

int main(int argc, char **argv) {
    // On Wayland, xdg-shell forbids client-controlled placement so move()
    // is a no-op. Use wlr-layer-shell (via LayerShellQt) for proper centering
    // on KDE/Sway/wlroots. Falls back to standard toplevel under X11/GNOME.
#ifdef ANYTALK_HAS_LAYER_SHELL
    if (qgetenv("XDG_SESSION_TYPE") == "wayland") {
        LayerShellQt::Shell::useLayerShell();
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationName("anytalk-overlay");
    app.setApplicationVersion("0.3.0");
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription("Aurora-style voice activation overlay for fcitx5-anytalk");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    OverlayWindow overlay;

    AsrController asr;
    if (!asr.initialise(OverlayConfig::load())) {
        qWarning() << "anytalk-overlay: ASR engine failed to initialise. "
                      "Recording will not work. Check ~/.config/fcitx5/conf/anytalk.conf.";
    }

    OverlayService service(&overlay, &asr);
    if (!service.registerOnBus()) {
        qWarning() << "anytalk-overlay: D-Bus registration failed; another "
                      "instance may already own the name.";
        return 1;
    }

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

    // Re-broadcast ASR events on D-Bus so the fcitx5 addon (and any other
    // observer) can react. The addon uses TranscriptFinal to commit text and
    // TranscriptPartial to drive preedit.
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

    return app.exec();
}
