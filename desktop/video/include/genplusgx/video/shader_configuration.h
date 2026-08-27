#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace genplusgx::video {

enum class ShaderMode {
  disabled,
  builtinCrt,
  libretroPreset,
};

struct ShaderParameterValue final {
  std::string name;
  float value{0.0F};

  [[nodiscard]] bool operator==(const ShaderParameterValue&) const = default;
};

struct ShaderConfiguration final {
  ShaderMode mode{ShaderMode::disabled};
  std::filesystem::path presetPath;
  std::vector<ShaderParameterValue> parameters;

  [[nodiscard]] bool operator==(const ShaderConfiguration&) const = default;
};

struct ShaderParameter final {
  std::string name;
  std::string description;
  float initial{0.0F};
  float minimum{0.0F};
  float maximum{0.0F};
  float step{0.0F};
};

struct ShaderInspectionResult final {
  bool success{false};
  std::filesystem::path resolvedPresetPath;
  std::vector<ShaderParameter> parameters;
  std::string error;
};

inline constexpr std::size_t maximumShaderParameters = 128U;
inline constexpr std::size_t maximumShaderPathCharacters = 4096U;
inline constexpr std::size_t maximumShaderParameterNameCharacters = 128U;

[[nodiscard]] bool validateShaderConfiguration(
  const ShaderConfiguration& configuration) noexcept;
[[nodiscard]] std::filesystem::path builtinCrtPresetPath();
[[nodiscard]] ShaderInspectionResult inspectShaderConfiguration(
  const ShaderConfiguration& configuration);

} // namespace genplusgx::video
