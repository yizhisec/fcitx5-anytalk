#include "PulseSourceProbe.h"

#include <QDebug>
#include <QDeadlineTimer>
#include <pulse/pulseaudio.h>
#include <sys/time.h>
#include <algorithm>

namespace anytalk {

bool PulseSourceInfo::isBluetooth() const {
    // Three signals — any match flips us to on-demand. We bias toward
    // false positives (treat unknown as risky) because the cost of a
    // missed detection is a kernel-level SCO deadlock, while the cost
    // of a false positive is one extra second of zero-padding on first
    // F2 press. Trade is obvious.
    // device.api is "bluez" under raw BlueZ but "bluez5" via PipeWire's
    // module-bluez5-device, so match by substring.
    if (deviceApi.contains(QStringLiteral("bluez"), Qt::CaseInsensitive)) return true;
    if (deviceBus.compare(QStringLiteral("bluetooth"), Qt::CaseInsensitive) == 0) return true;
    if (name.startsWith(QStringLiteral("bluez_"), Qt::CaseInsensitive)) return true;
    if (driver.contains(QStringLiteral("bluez"), Qt::CaseInsensitive)) return true;
    return false;
}

namespace {

struct ProbeState {
    pa_threaded_mainloop *loop = nullptr;
    pa_context *ctx = nullptr;
    bool ctxReady = false;
    bool ctxFailed = false;

    QString defaultSourceName;
    bool serverInfoDone = false;

    PulseSourceInfo info;
    bool sourceInfoDone = false;
    bool sourceInfoFailed = false;
};

void contextStateCb(pa_context *c, void *userdata) {
    auto *s = static_cast<ProbeState *>(userdata);
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            s->ctxReady = true;
            pa_threaded_mainloop_signal(s->loop, 0);
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            s->ctxFailed = true;
            pa_threaded_mainloop_signal(s->loop, 0);
            break;
        default:
            break;
    }
}

void serverInfoCb(pa_context * /*c*/, const pa_server_info *info, void *userdata) {
    auto *s = static_cast<ProbeState *>(userdata);
    if (info && info->default_source_name) {
        s->defaultSourceName = QString::fromUtf8(info->default_source_name);
    }
    s->serverInfoDone = true;
    pa_threaded_mainloop_signal(s->loop, 0);
}

void sourceInfoCb(pa_context * /*c*/, const pa_source_info *info, int eol, void *userdata) {
    auto *s = static_cast<ProbeState *>(userdata);
    if (eol < 0) {
        s->sourceInfoFailed = true;
        s->sourceInfoDone = true;
        pa_threaded_mainloop_signal(s->loop, 0);
        return;
    }
    if (eol > 0) {
        s->sourceInfoDone = true;
        pa_threaded_mainloop_signal(s->loop, 0);
        return;
    }
    if (info) {
        s->info.name = QString::fromUtf8(info->name ? info->name : "");
        s->info.driver = QString::fromUtf8(info->driver ? info->driver : "");
        if (info->proplist) {
            const char *api = pa_proplist_gets(info->proplist, PA_PROP_DEVICE_API);
            const char *bus = pa_proplist_gets(info->proplist, PA_PROP_DEVICE_BUS);
            if (api) s->info.deviceApi = QString::fromUtf8(api);
            if (bus) s->info.deviceBus = QString::fromUtf8(bus);
        }
    }
}

/// Wait until predicate() returns true or the deadline passes. Caller must
/// hold the mainloop lock. Returns true if the predicate triggered.
///
/// pa_threaded_mainloop has no native timed wait. We re-arm a one-shot
/// timer on the mainloop's API for each iteration; when it fires it just
/// signals the loop, which wakes us so we can re-check the deadline.
/// Combined with the callbacks above (which also signal on completion),
/// this is signal-driven — we never sleep when a callback has already
/// landed.
template <typename Pred>
bool waitFor(pa_threaded_mainloop *loop, QDeadlineTimer deadline, Pred predicate) {
    auto *api = pa_threaded_mainloop_get_api(loop);
    while (!predicate()) {
        const qint64 remainingMs = deadline.remainingTime();
        if (remainingMs <= 0) return false;

        // Set a wake-up timer at min(remaining, 100ms) so we re-check the
        // deadline periodically even if PA never signals. The 100ms cap
        // bounds late-cancel cost; under a working PA we hit the callback
        // signal first and never sleep that long.
        timeval tv{};
        gettimeofday(&tv, nullptr);
        const qint64 sleepMs = std::min<qint64>(remainingMs, 100);
        tv.tv_sec += sleepMs / 1000;
        tv.tv_usec += static_cast<int>((sleepMs % 1000) * 1000);
        if (tv.tv_usec >= 1'000'000) {
            tv.tv_sec += 1;
            tv.tv_usec -= 1'000'000;
        }

        auto *ev = api->time_new(api, &tv,
            [](pa_mainloop_api * /*a*/, pa_time_event *e, const timeval * /*t*/, void *userdata) {
                auto *loopRef = static_cast<pa_threaded_mainloop *>(userdata);
                pa_threaded_mainloop_signal(loopRef, 0);
                pa_threaded_mainloop_get_api(loopRef)->time_free(e);
            }, loop);

        pa_threaded_mainloop_wait(loop);
        // Best-effort: if predicate is satisfied before the timer fired,
        // free the timer. Safe to call even if it already fired (the
        // callback freed itself; we'd be racing — guard via the api's
        // time_free which is idempotent for the live event handle).
        if (predicate()) {
            // Cancel pending timer if still alive. Once the callback runs
            // it frees itself, so we can only reach this safely while the
            // timer is still pending. The mainloop holds its own lock,
            // and we hold the user lock, so we serialize against the
            // dispatch — if a stale free is risky on a given PA build,
            // the timer simply runs harmlessly and signals an empty wait.
            (void)ev; // intentional: leak to the timer callback to free.
        }
    }
    return true;
}

} // namespace

std::optional<PulseSourceInfo> probeDefaultSource(int timeoutMs) {
    ProbeState state;
    QDeadlineTimer deadline(timeoutMs);

    state.loop = pa_threaded_mainloop_new();
    if (!state.loop) {
        qWarning() << "PulseSourceProbe: pa_threaded_mainloop_new failed";
        return std::nullopt;
    }

    auto cleanup = [&] {
        if (state.ctx) {
            pa_threaded_mainloop_lock(state.loop);
            pa_context_disconnect(state.ctx);
            pa_context_unref(state.ctx);
            state.ctx = nullptr;
            pa_threaded_mainloop_unlock(state.loop);
        }
        pa_threaded_mainloop_stop(state.loop);
        pa_threaded_mainloop_free(state.loop);
    };

    if (pa_threaded_mainloop_start(state.loop) < 0) {
        qWarning() << "PulseSourceProbe: pa_threaded_mainloop_start failed";
        pa_threaded_mainloop_free(state.loop);
        return std::nullopt;
    }

    pa_threaded_mainloop_lock(state.loop);

    state.ctx = pa_context_new(pa_threaded_mainloop_get_api(state.loop), "anytalk-probe");
    if (!state.ctx) {
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }

    pa_context_set_state_callback(state.ctx, contextStateCb, &state);

    if (pa_context_connect(state.ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        qWarning() << "PulseSourceProbe: pa_context_connect failed:"
                   << pa_strerror(pa_context_errno(state.ctx));
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }

    if (!waitFor(state.loop, deadline, [&] { return state.ctxReady || state.ctxFailed; }) ||
        state.ctxFailed) {
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }

    auto *opServer = pa_context_get_server_info(state.ctx, serverInfoCb, &state);
    if (!opServer) {
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }
    pa_operation_unref(opServer);

    if (!waitFor(state.loop, deadline, [&] { return state.serverInfoDone; }) ||
        state.defaultSourceName.isEmpty()) {
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }

    auto *opSrc = pa_context_get_source_info_by_name(
        state.ctx, state.defaultSourceName.toUtf8().constData(), sourceInfoCb, &state);
    if (!opSrc) {
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }
    pa_operation_unref(opSrc);

    if (!waitFor(state.loop, deadline, [&] { return state.sourceInfoDone; }) ||
        state.sourceInfoFailed) {
        pa_threaded_mainloop_unlock(state.loop);
        cleanup();
        return std::nullopt;
    }

    pa_threaded_mainloop_unlock(state.loop);
    cleanup();

    if (state.info.name.isEmpty()) state.info.name = state.defaultSourceName;
    return state.info;
}

} // namespace anytalk
