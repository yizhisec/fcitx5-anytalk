#pragma once
#include "AsrBackend.h"

#include <memory>

class OverlayConfig;

namespace asr {
/// Creates the backend named in `cfg.backend`, configured from the matching
/// section of `cfg`. Returns nullptr when the backend name is unknown or
/// required credentials are missing.
std::unique_ptr<AsrBackend> create(const OverlayConfig &cfg, QObject *parent = nullptr);
} // namespace asr
