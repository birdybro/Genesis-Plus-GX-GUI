#include "genplusgx/video/shader_configuration.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  using genplusgx::video::ShaderConfiguration;
  using genplusgx::video::ShaderMode;

  const ShaderConfiguration disabled;
  const ShaderConfiguration builtinWithParameter{
    .mode = ShaderMode::builtinCrt,
    .presetPath = {},
    .parameters = {{.name = "CURVATURE", .value = 0.1F}},
  };
  if (!check(genplusgx::video::validateShaderConfiguration(disabled),
        "The disabled shader configuration was rejected") ||
      !check(genplusgx::video::validateShaderConfiguration(
        builtinWithParameter), "The built-in CRT configuration was rejected")) {
    return 1;
  }

  auto custom = ShaderConfiguration{
    .mode = ShaderMode::libretroPreset,
    .presetPath = std::filesystem::temp_directory_path() / "test.slangp",
    .parameters = {{.name = "TEST_VALUE", .value = 0.5F}},
  };
  if (!check(genplusgx::video::validateShaderConfiguration(custom),
        "A valid absolute Slang preset configuration was rejected")) {
    return 2;
  }
  custom.presetPath = "relative.slangp";
  if (!check(!genplusgx::video::validateShaderConfiguration(custom),
        "A relative shader path was accepted")) {
    return 3;
  }
  custom.presetPath = std::filesystem::temp_directory_path() / "legacy.glslp";
  if (!check(!genplusgx::video::validateShaderConfiguration(custom),
        "An unsupported legacy GLSL preset was accepted")) {
    return 4;
  }
  custom.presetPath = std::filesystem::temp_directory_path() / "test.slangp";
  custom.parameters.push_back({.name = "TEST_VALUE", .value = 0.8F});
  if (!check(!genplusgx::video::validateShaderConfiguration(custom),
        "Duplicate shader parameters were accepted")) {
    return 5;
  }
  custom.parameters = {{
    .name = "TEST_VALUE",
    .value = std::numeric_limits<float>::quiet_NaN(),
  }};
  if (!check(!genplusgx::video::validateShaderConfiguration(custom),
        "A non-finite shader parameter was accepted")) {
    return 6;
  }

  const ShaderConfiguration builtin{
    .mode = ShaderMode::builtinCrt,
    .presetPath = {},
    .parameters = {},
  };
  const auto inspection = genplusgx::video::inspectShaderConfiguration(builtin);
  if (!check(inspection.success, inspection.error.c_str()) ||
      !check(inspection.resolvedPresetPath.is_absolute(),
        "The built-in CRT preset did not resolve to an absolute path") ||
      !check(inspection.parameters.size() == 5U,
        "The built-in CRT preset did not expose all parameters") ||
      !check(inspection.parameters.front().name == "SCANLINE_STRENGTH",
        "The built-in CRT parameter metadata was not parsed")) {
    return 7;
  }
  const auto unknownParameter = genplusgx::video::inspectShaderConfiguration({
    .mode = ShaderMode::builtinCrt,
    .presetPath = {},
    .parameters = {{.name = "NOT_DECLARED", .value = 0.5F}},
  });
  const auto outOfRangeParameter =
    genplusgx::video::inspectShaderConfiguration({
      .mode = ShaderMode::builtinCrt,
      .presetPath = {},
      .parameters = {{.name = "CURVATURE", .value = 2.0F}},
    });
  if (!check(!unknownParameter.success && !unknownParameter.error.empty(),
        "An undeclared shader parameter was accepted") ||
      !check(!outOfRangeParameter.success && !outOfRangeParameter.error.empty(),
        "An out-of-range shader parameter was accepted")) {
    return 8;
  }

  QTemporaryDir directory;
  const auto malformedPath =
    std::filesystem::path{directory.path().toStdString()} / "broken.slangp";
  std::ofstream malformed{malformedPath};
  malformed << "this is not a libretro shader preset\n";
  malformed.close();
  const auto malformedResult = genplusgx::video::inspectShaderConfiguration({
    .mode = ShaderMode::libretroPreset,
    .presetPath = malformedPath,
    .parameters = {},
  });
  if (!check(!malformedResult.success && !malformedResult.error.empty(),
        "A malformed shader preset did not produce a useful error")) {
    return 9;
  }

  const auto oversizedPath =
    std::filesystem::path{directory.path().toStdString()} / "oversized.slangp";
  std::ofstream oversized{oversizedPath};
  oversized << "shaders = 1\n";
  oversized.close();
  std::filesystem::resize_file(oversizedPath, 4U * 1024U * 1024U + 1U);
  const auto oversizedResult = genplusgx::video::inspectShaderConfiguration({
    .mode = ShaderMode::libretroPreset,
    .presetPath = oversizedPath,
    .parameters = {},
  });
  if (!check(!oversizedResult.success &&
        oversizedResult.error.find("4 MiB") != std::string::npos,
        "An oversized shader preset was accepted")) {
    return 10;
  }

  const auto missingResult = genplusgx::video::inspectShaderConfiguration({
    .mode = ShaderMode::libretroPreset,
    .presetPath = malformedPath.parent_path() / "missing.slangp",
    .parameters = {},
  });
  return check(!missingResult.success &&
      missingResult.error.find("does not exist") != std::string::npos,
      "A missing shader preset did not produce a useful error")
    ? 0 : 11;
}
