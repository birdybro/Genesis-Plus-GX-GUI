#pragma once

#include "genplusgx/video/shader_configuration.h"

#include <cstdint>
#include <memory>
#include <string>

namespace genplusgx::video {

class LibretroShaderRuntime final {
public:
  LibretroShaderRuntime();
  ~LibretroShaderRuntime();

  LibretroShaderRuntime(const LibretroShaderRuntime&) = delete;
  LibretroShaderRuntime& operator=(const LibretroShaderRuntime&) = delete;

  [[nodiscard]] bool initialize(const ShaderConfiguration& configuration);
  void reset();
  [[nodiscard]] bool isInitialized() const noexcept;
  [[nodiscard]] const std::string& lastError() const noexcept;

  [[nodiscard]] bool render(
    std::uint32_t inputTexture,
    std::uint32_t inputFormat,
    std::uint32_t inputWidth,
    std::uint32_t inputHeight,
    std::uint32_t outputTexture,
    std::uint32_t outputFormat,
    std::uint32_t outputWidth,
    std::uint32_t outputHeight,
    std::uint64_t frameCount,
    float sourceFramesPerSecond,
    float sourceAspectRatio);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace genplusgx::video
