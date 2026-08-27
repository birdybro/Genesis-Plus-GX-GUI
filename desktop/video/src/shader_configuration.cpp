#include "genplusgx/video/shader_configuration.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <string>

namespace genplusgx::video {
namespace {

bool isValidParameterName(const std::string& name) noexcept
{
  if (name.empty() || name.size() > maximumShaderParameterNameCharacters) {
    return false;
  }
  return std::all_of(name.cbegin(), name.cend(), [](unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
      (character >= 'A' && character <= 'Z') ||
      (character >= '0' && character <= '9') || character == '_';
  });
}

std::string lowercaseExtension(const std::filesystem::path& path)
{
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
    [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
  return extension;
}

} // namespace

bool validateShaderConfiguration(
  const ShaderConfiguration& configuration) noexcept
{
  if (static_cast<unsigned>(configuration.mode) >
      static_cast<unsigned>(ShaderMode::libretroPreset) ||
      configuration.parameters.size() > maximumShaderParameters) {
    return false;
  }
  if (configuration.mode != ShaderMode::libretroPreset &&
      !configuration.presetPath.empty()) {
    return false;
  }
  if (configuration.mode == ShaderMode::disabled &&
      !configuration.parameters.empty()) {
    return false;
  }
  if (configuration.mode == ShaderMode::libretroPreset) {
    if (configuration.presetPath.empty() ||
        !configuration.presetPath.is_absolute() ||
        configuration.presetPath.native().size() > maximumShaderPathCharacters ||
        lowercaseExtension(configuration.presetPath) != ".slangp") {
      return false;
    }
  }
  std::set<std::string> names;
  for (const auto& parameter : configuration.parameters) {
    if (!isValidParameterName(parameter.name) ||
        !std::isfinite(parameter.value) ||
        !names.insert(parameter.name).second) {
      return false;
    }
  }
  return true;
}

} // namespace genplusgx::video
