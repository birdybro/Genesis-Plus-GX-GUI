#pragma once

#include <filesystem>

namespace genplusgx::app {

// Selects XCB for the relocatable Linux package when it contains only the XCB
// Qt platform plugin. Explicit user platform selection is always preserved.
// This must run before QApplication is constructed.
[[nodiscard]] bool configureBundledLinuxQtPlatform(
  const std::filesystem::path& executablePath);

} // namespace genplusgx::app
