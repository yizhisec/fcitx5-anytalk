#pragma once
#include <QString>
#include <optional>

namespace anytalk {

/// Snapshot of the PulseAudio default source's identifying metadata.
/// Used to decide whether the always-on capture pattern is safe (wired /
/// built-in mic) or risky (Bluetooth HFP/SCO — see CLAUDE.md "Bluetooth
/// mic warning"; long-lived SCO streams + close trip a kernel race that
/// can wedge the system).
struct PulseSourceInfo {
    QString name;       // e.g. "alsa_input.pci-…", "bluez_source.XX_XX_XX.headset_head_unit"
    QString driver;     // device.driver / module name
    QString deviceApi;  // device.api ("alsa", "bluez", …)
    QString deviceBus;  // device.bus ("pci", "usb", "bluetooth", …)

    bool isBluetooth() const;
};

/// Blocking one-shot query for the default source's metadata. Spins a
/// short-lived pa_threaded_mainloop + pa_context internally; does not
/// interact with the AudioCapture pa_simple stream. Safe to call before
/// AudioCapture::prewarm().
///
/// Returns std::nullopt if PulseAudio is unreachable, the connection
/// or info query times out, or the default source can't be resolved.
/// Callers should treat nullopt as "unknown — assume risky" and pick
/// the on-demand path.
std::optional<PulseSourceInfo> probeDefaultSource(int timeoutMs = 1500);

} // namespace anytalk
