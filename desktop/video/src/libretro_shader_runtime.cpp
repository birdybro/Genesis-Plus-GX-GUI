#include "genplusgx/video/libretro_shader_runtime.h"

#if GENPLUSGX_HAS_LIBRETRO_SHADERS
#include <librashader.h>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QOpenGLContext>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace genplusgx::video {
namespace {

#if GENPLUSGX_HAS_LIBRETRO_SHADERS
QString fromPath(const std::filesystem::path& path)
{
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  const auto bytes = path.u8string();
  return QString::fromUtf8(reinterpret_cast<const char*>(bytes.data()),
    static_cast<qsizetype>(bytes.size()));
#endif
}
#endif

std::filesystem::path toPath(const QString& path)
{
#ifdef _WIN32
  return std::filesystem::path{path.toStdWString()};
#else
  const auto bytes = path.toUtf8();
  return std::filesystem::path{bytes.constData()};
#endif
}

std::filesystem::path installedShaderPath()
{
  const QDir applicationDirectory{QCoreApplication::applicationDirPath()};
#ifdef Q_OS_MACOS
  return toPath(applicationDirectory.absoluteFilePath(
    QStringLiteral("../Resources/shaders/genplusgx-crt.slangp")));
#else
  return toPath(applicationDirectory.absoluteFilePath(
    QStringLiteral("../share/Genesis-Plus-GX-GUI/shaders/"
                   "genplusgx-crt.slangp")));
#endif
}

std::filesystem::path resolvePresetPath(
  const ShaderConfiguration& configuration)
{
  if (configuration.mode == ShaderMode::builtinCrt) {
    return builtinCrtPresetPath();
  }
  if (configuration.mode == ShaderMode::libretroPreset) {
    return configuration.presetPath;
  }
  return {};
}

constexpr std::uintmax_t maximumShaderPresetFileBytes = 4U * 1024U * 1024U;

#if GENPLUSGX_HAS_LIBRETRO_SHADERS

constexpr std::size_t maximumParameterDescriptionCharacters = 512U;

bool validRuntimeParameter(const libra_preset_param_t& parameter)
{
  if (parameter.name == nullptr || parameter.description == nullptr) {
    return false;
  }
  const std::string name{parameter.name};
  const std::string description{parameter.description};
  const bool validName = !name.empty() &&
    name.size() <= maximumShaderParameterNameCharacters &&
    std::all_of(name.cbegin(), name.cend(), [](unsigned char character) {
      return (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '_';
    });
  return validName &&
    description.size() <= maximumParameterDescriptionCharacters &&
    std::isfinite(parameter.initial) && std::isfinite(parameter.minimum) &&
    std::isfinite(parameter.maximum) && std::isfinite(parameter.step) &&
    parameter.minimum <= parameter.initial &&
    parameter.initial <= parameter.maximum && parameter.step >= 0.0F;
}

struct LibrashaderApi final {
  std::unique_ptr<QLibrary> library;
  PFN_libra_instance_abi_version instanceAbiVersion{nullptr};
  PFN_libra_instance_api_version instanceApiVersion{nullptr};
  PFN_libra_error_write errorWrite{nullptr};
  PFN_libra_error_free errorFree{nullptr};
  PFN_libra_error_free_string errorFreeString{nullptr};
  PFN_libra_preset_create_with_options presetCreateWithOptions{nullptr};
  PFN_libra_preset_free presetFree{nullptr};
  PFN_libra_preset_set_param presetSetParam{nullptr};
  PFN_libra_preset_get_runtime_params presetGetRuntimeParams{nullptr};
  PFN_libra_preset_free_runtime_params presetFreeRuntimeParams{nullptr};
  PFN_libra_gl_filter_chain_create glFilterChainCreate{nullptr};
  PFN_libra_gl_filter_chain_frame glFilterChainFrame{nullptr};
  PFN_libra_gl_filter_chain_free glFilterChainFree{nullptr};

  template<typename Function>
  bool resolve(Function& function, const char* name)
  {
    function = reinterpret_cast<Function>(library->resolve(name));
    return function != nullptr;
  }

  [[nodiscard]] std::string consumeError(libra_error_t error) const
  {
    if (error == nullptr) {
      return {};
    }
    char* message = nullptr;
    std::string result{"Unknown librashader error."};
    if (errorWrite(error, &message) == 0 && message != nullptr) {
      result = message;
      errorFreeString(&message);
    }
    errorFree(&error);
    return result;
  }
};

std::vector<QString> libraryCandidates()
{
  std::vector<QString> candidates;
#ifdef GENPLUSGX_LIBRASHADER_BUILD_PATH
  candidates.emplace_back(QString::fromUtf8(GENPLUSGX_LIBRASHADER_BUILD_PATH));
#endif
  const QDir appDirectory{QCoreApplication::applicationDirPath()};
#ifdef Q_OS_WIN
  candidates.emplace_back(appDirectory.absoluteFilePath(
    QStringLiteral("librashader.dll")));
#elif defined(Q_OS_MACOS)
  candidates.emplace_back(appDirectory.absoluteFilePath(
    QStringLiteral("../Frameworks/librashader.dylib")));
#else
  candidates.emplace_back(appDirectory.absoluteFilePath(
    QStringLiteral("../lib/librashader.so")));
#endif
  candidates.emplace_back(QStringLiteral("librashader"));
  return candidates;
}

std::pair<std::shared_ptr<LibrashaderApi>, std::string> loadApi()
{
  std::string lastError{"The librashader runtime could not be found."};
  for (const auto& candidate : libraryCandidates()) {
    auto library = std::make_unique<QLibrary>(candidate);
    library->setLoadHints(QLibrary::ResolveAllSymbolsHint);
    if (!library->load()) {
      lastError = library->errorString().toStdString();
      continue;
    }
    auto api = std::make_shared<LibrashaderApi>();
    api->library = std::move(library);
    const bool complete =
      api->resolve(api->instanceAbiVersion, "libra_instance_abi_version") &&
      api->resolve(api->instanceApiVersion, "libra_instance_api_version") &&
      api->resolve(api->errorWrite, "libra_error_write") &&
      api->resolve(api->errorFree, "libra_error_free") &&
      api->resolve(api->errorFreeString, "libra_error_free_string") &&
      api->resolve(api->presetCreateWithOptions,
        "libra_preset_create_with_options") &&
      api->resolve(api->presetFree, "libra_preset_free") &&
      api->resolve(api->presetSetParam, "libra_preset_set_param") &&
      api->resolve(api->presetGetRuntimeParams,
        "libra_preset_get_runtime_params") &&
      api->resolve(api->presetFreeRuntimeParams,
        "libra_preset_free_runtime_params") &&
      api->resolve(api->glFilterChainCreate,
        "libra_gl_filter_chain_create") &&
      api->resolve(api->glFilterChainFrame,
        "libra_gl_filter_chain_frame") &&
      api->resolve(api->glFilterChainFree,
        "libra_gl_filter_chain_free");
    if (!complete) {
      lastError = "The librashader runtime is missing required OpenGL symbols.";
      continue;
    }
    if (api->instanceAbiVersion() != LIBRASHADER_CURRENT_ABI ||
        api->instanceApiVersion() < LIBRASHADER_CURRENT_VERSION) {
      lastError = "The installed librashader runtime has an incompatible ABI.";
      continue;
    }
    return {std::move(api), {}};
  }
  return {nullptr, std::move(lastError)};
}

std::string utf8Path(const std::filesystem::path& path)
{
  return fromPath(path).toUtf8().toStdString();
}

libra_shader_preset_t createPreset(
  const std::shared_ptr<LibrashaderApi>& api,
  const std::filesystem::path& path,
  std::string& error)
{
  libra_shader_preset_t preset = nullptr;
  libra_preset_opt_t options{};
  options.version = LIBRASHADER_CURRENT_VERSION;
  options.original_aspect_uniforms = true;
  options.frametime_uniforms = true;
  const auto filename = utf8Path(path);
  error = api->consumeError(api->presetCreateWithOptions(
    filename.c_str(), nullptr, &options, &preset));
  return preset;
}

const void* openGlLoader(const char* name)
{
  const auto* context = QOpenGLContext::currentContext();
  if (context == nullptr) {
    return nullptr;
  }
  const auto function = context->getProcAddress(name);
  const void* result = nullptr;
  static_assert(sizeof(function) == sizeof(result));
  std::memcpy(&result, &function, sizeof(result));
  return result;
}

#endif

} // namespace

std::filesystem::path builtinCrtPresetPath()
{
  const auto installedPath = installedShaderPath();
  std::error_code error;
  if (std::filesystem::is_regular_file(installedPath, error)) {
    return installedPath;
  }
#ifdef GENPLUSGX_BUILTIN_SHADER_SOURCE_DIR
  const auto sourcePath = std::filesystem::path{
    GENPLUSGX_BUILTIN_SHADER_SOURCE_DIR} / "genplusgx-crt.slangp";
  error.clear();
  if (std::filesystem::is_regular_file(sourcePath, error)) {
    return sourcePath;
  }
#endif
  return installedPath;
}

ShaderInspectionResult inspectShaderConfiguration(
  const ShaderConfiguration& configuration)
{
  if (!validateShaderConfiguration(configuration)) {
    return {.success = false, .resolvedPresetPath = {}, .parameters = {},
      .error = "The shader configuration is invalid."};
  }
  if (configuration.mode == ShaderMode::disabled) {
    return {.success = true, .resolvedPresetPath = {}, .parameters = {},
      .error = {}};
  }
  const auto path = resolvePresetPath(configuration);
  std::error_code filesystemError;
  if (!std::filesystem::is_regular_file(path, filesystemError)) {
    return {.success = false, .resolvedPresetPath = path, .parameters = {},
      .error = "The selected Libretro shader preset does not exist."};
  }
  const auto presetSize = std::filesystem::file_size(path, filesystemError);
  if (filesystemError || presetSize > maximumShaderPresetFileBytes) {
    return {.success = false, .resolvedPresetPath = path, .parameters = {},
      .error = "The selected Libretro shader preset is unreadable or exceeds 4 MiB."};
  }
#if GENPLUSGX_HAS_LIBRETRO_SHADERS
  auto [api, loadError] = loadApi();
  if (!api) {
    return {.success = false, .resolvedPresetPath = path, .parameters = {},
      .error = std::move(loadError)};
  }
  std::string error;
  auto preset = createPreset(api, path, error);
  if (preset == nullptr) {
    return {.success = false, .resolvedPresetPath = path, .parameters = {},
      .error = std::move(error)};
  }
  libra_preset_param_list_t list{};
  error = api->consumeError(api->presetGetRuntimeParams(&preset, &list));
  std::vector<ShaderParameter> parameters;
  if (error.empty()) {
    if (list.length > maximumShaderParameters) {
      error = "The shader exposes too many runtime parameters.";
    } else if (list.length > 0U && list.parameters == nullptr) {
      error = "The shader returned invalid runtime parameter data.";
    } else {
      parameters.reserve(static_cast<std::size_t>(list.length));
      std::set<std::string> names;
      for (std::uint64_t index = 0; index < list.length; ++index) {
        const auto& value = list.parameters[index];
        if (!validRuntimeParameter(value) ||
            !names.insert(value.name).second) {
          error = "The shader exposes invalid runtime parameter metadata.";
          break;
        }
        parameters.push_back({
          .name = value.name,
          .description = value.description,
          .initial = value.initial,
          .minimum = value.minimum,
          .maximum = value.maximum,
          .step = value.step,
        });
      }
    }
    static_cast<void>(api->consumeError(api->presetFreeRuntimeParams(list)));
  }
  static_cast<void>(api->consumeError(api->presetFree(&preset)));
  if (!error.empty()) {
    return {.success = false, .resolvedPresetPath = path, .parameters = {},
      .error = std::move(error)};
  }
  for (const auto& overrideValue : configuration.parameters) {
    const auto declared = std::find_if(parameters.cbegin(), parameters.cend(),
      [&overrideValue](const ShaderParameter& parameter) {
        return parameter.name == overrideValue.name;
      });
    if (declared == parameters.cend() ||
        overrideValue.value < declared->minimum ||
        overrideValue.value > declared->maximum) {
      return {.success = false, .resolvedPresetPath = path, .parameters = {},
        .error = "The shader parameter '" + overrideValue.name +
          "' is missing or outside its declared range."};
    }
  }
  return {.success = true, .resolvedPresetPath = path,
    .parameters = std::move(parameters), .error = {}};
#else
  return {.success = false, .resolvedPresetPath = path, .parameters = {},
    .error = "This build does not include Libretro shader support."};
#endif
}

class LibretroShaderRuntime::Impl final {
public:
#if GENPLUSGX_HAS_LIBRETRO_SHADERS
  std::shared_ptr<LibrashaderApi> api;
  libra_gl_filter_chain_t chain{nullptr};
#endif
  std::string error;
};

LibretroShaderRuntime::LibretroShaderRuntime()
  : impl_(std::make_unique<Impl>())
{
}

LibretroShaderRuntime::~LibretroShaderRuntime()
{
  reset();
}

bool LibretroShaderRuntime::initialize(
  const ShaderConfiguration& configuration)
{
  reset();
  if (configuration.mode == ShaderMode::disabled) {
    impl_->error = "No shader preset was selected.";
    return false;
  }
  const auto inspection = inspectShaderConfiguration(configuration);
  if (!inspection.success) {
    impl_->error = inspection.error;
    return false;
  }
#if GENPLUSGX_HAS_LIBRETRO_SHADERS
  auto [api, loadError] = loadApi();
  if (!api) {
    impl_->error = std::move(loadError);
    return false;
  }
  std::string error;
  auto preset = createPreset(api, inspection.resolvedPresetPath, error);
  if (preset == nullptr) {
    impl_->error = std::move(error);
    return false;
  }
  for (const auto& parameter : configuration.parameters) {
    error = api->consumeError(api->presetSetParam(
      &preset, parameter.name.c_str(), parameter.value));
    if (!error.empty()) {
      static_cast<void>(api->consumeError(api->presetFree(&preset)));
      impl_->error = "Invalid shader parameter '" + parameter.name +
        "': " + error;
      return false;
    }
  }
  filter_chain_gl_opt_t options{};
  options.version = LIBRASHADER_CURRENT_VERSION;
  options.glsl_version = 330;
  options.use_dsa = false;
  options.force_no_mipmaps = false;
  options.disable_cache = true;
  libra_gl_filter_chain_t chain = nullptr;
  error = api->consumeError(api->glFilterChainCreate(
    &preset, openGlLoader, &options, &chain));
  if (!error.empty() || chain == nullptr) {
    impl_->error = error.empty() ? "Could not create the shader chain." : error;
    return false;
  }
  impl_->api = std::move(api);
  impl_->chain = chain;
  return true;
#else
  impl_->error = "This build does not include Libretro shader support.";
  return false;
#endif
}

void LibretroShaderRuntime::reset()
{
#if GENPLUSGX_HAS_LIBRETRO_SHADERS
  if (impl_->chain != nullptr && impl_->api) {
    static_cast<void>(impl_->api->consumeError(
      impl_->api->glFilterChainFree(&impl_->chain)));
  }
  impl_->api.reset();
#endif
  impl_->error.clear();
}

bool LibretroShaderRuntime::isInitialized() const noexcept
{
#if GENPLUSGX_HAS_LIBRETRO_SHADERS
  return impl_->chain != nullptr;
#else
  return false;
#endif
}

const std::string& LibretroShaderRuntime::lastError() const noexcept
{
  return impl_->error;
}

bool LibretroShaderRuntime::render(
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
  float sourceAspectRatio)
{
#if GENPLUSGX_HAS_LIBRETRO_SHADERS
  if (!isInitialized()) {
    return false;
  }
  const libra_image_gl_t input{
    .handle = inputTexture,
    .format = inputFormat,
    .width = inputWidth,
    .height = inputHeight,
  };
  const libra_image_gl_t output{
    .handle = outputTexture,
    .format = outputFormat,
    .width = outputWidth,
    .height = outputHeight,
  };
  const libra_viewport_t viewport{
    .x = 0.0F,
    .y = 0.0F,
    .width = outputWidth,
    .height = outputHeight,
  };
  frame_gl_opt_t options{};
  options.version = LIBRASHADER_CURRENT_VERSION;
  options.frame_direction = 1;
  options.total_subframes = 1;
  options.current_subframe = 1;
  options.aspect_ratio = sourceAspectRatio;
  options.frames_per_second = sourceFramesPerSecond;
  options.frametime_delta = sourceFramesPerSecond > 0.0F
    ? static_cast<std::uint32_t>(std::lround(1000.0F / sourceFramesPerSecond))
    : 0U;
  options.brightness_nits = 200.0F;
  impl_->error = impl_->api->consumeError(impl_->api->glFilterChainFrame(
    &impl_->chain, static_cast<std::size_t>(frameCount), input, output,
    &viewport, nullptr, &options));
  return impl_->error.empty();
#else
  static_cast<void>(inputTexture);
  static_cast<void>(inputFormat);
  static_cast<void>(inputWidth);
  static_cast<void>(inputHeight);
  static_cast<void>(outputTexture);
  static_cast<void>(outputFormat);
  static_cast<void>(outputWidth);
  static_cast<void>(outputHeight);
  static_cast<void>(frameCount);
  static_cast<void>(sourceFramesPerSecond);
  static_cast<void>(sourceAspectRatio);
  impl_->error = "This build does not include Libretro shader support.";
  return false;
#endif
}

} // namespace genplusgx::video
