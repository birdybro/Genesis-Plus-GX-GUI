#include "genplusgx/input/input_profile.h"

#include "genplusgx/persistence.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <utility>

namespace genplusgx::input {
namespace {

constexpr std::array allInputs{
  InputButton::up,
  InputButton::down,
  InputButton::left,
  InputButton::right,
  InputButton::a,
  InputButton::b,
  InputButton::c,
  InputButton::x,
  InputButton::y,
  InputButton::z,
  InputButton::mode,
  InputButton::start,
};

constexpr std::array deviceTypes{
  LogicalDeviceType::none,
  LogicalDeviceType::pad3Button,
  LogicalDeviceType::pad6Button,
  LogicalDeviceType::segaMouse,
  LogicalDeviceType::lightGun,
  LogicalDeviceType::paddle,
  LogicalDeviceType::sportsPad,
  LogicalDeviceType::xe1Ap,
  LogicalDeviceType::pico,
  LogicalDeviceType::terebiOekaki,
  LogicalDeviceType::graphicBoard,
  LogicalDeviceType::activator,
};

InputProfileStatus success()
{
  return {};
}

InputProfileStatus failure(InputProfileError error, std::string message)
{
  return {.error = error, .message = std::move(message)};
}

bool isValidInput(InputButton input) noexcept
{
  return std::find(allInputs.begin(), allInputs.end(), input) != allInputs.end();
}

std::string_view inputName(InputButton input) noexcept
{
  switch (input) {
  case InputButton::up:
    return "up";
  case InputButton::down:
    return "down";
  case InputButton::left:
    return "left";
  case InputButton::right:
    return "right";
  case InputButton::a:
    return "a";
  case InputButton::b:
    return "b";
  case InputButton::c:
    return "c";
  case InputButton::start:
    return "start";
  case InputButton::x:
    return "x";
  case InputButton::y:
    return "y";
  case InputButton::z:
    return "z";
  case InputButton::mode:
    return "mode";
  }
  return {};
}

std::optional<InputButton> parseInputName(const QString& name)
{
  const auto text = name.toStdString();
  const auto found = std::find_if(allInputs.begin(), allInputs.end(),
    [&text](InputButton input) { return inputName(input) == text; });
  return found == allInputs.end() ? std::nullopt : std::optional{*found};
}

std::optional<LogicalDeviceType> parseDeviceType(const QString& name)
{
  const auto text = name.toStdString();
  const auto found = std::find_if(deviceTypes.begin(), deviceTypes.end(),
    [&text](LogicalDeviceType type) { return logicalDeviceTypeName(type) == text; });
  return found == deviceTypes.end() ? std::nullopt : std::optional{*found};
}

QJsonObject profileToJson(const InputProfile& profile)
{
  QJsonArray keyboard;
  for (const auto& binding : profile.keyboardBindings) {
    keyboard.push_back(QJsonObject{
      {QStringLiteral("input"), QString::fromLatin1(inputName(binding.button))},
      {QStringLiteral("key"), binding.key},
    });
  }

  QJsonArray controller;
  for (const auto& binding : profile.controllerBindings) {
    controller.push_back(QJsonObject{
      {QStringLiteral("input"), QString::fromLatin1(inputName(binding.input))},
      {QStringLiteral("button"), static_cast<int>(binding.button)},
    });
  }

  QJsonArray controllerAxes;
  for (const auto& binding : profile.controllerAxisBindings) {
    controllerAxes.push_back(QJsonObject{
      {QStringLiteral("axis"), static_cast<int>(binding.axis)},
      {QStringLiteral("direction"), binding.direction},
      {QStringLiteral("input"), QString::fromLatin1(inputName(binding.input))},
    });
  }

  QJsonArray devices;
  for (const auto device : profile.devices) {
    devices.push_back(QString::fromLatin1(logicalDeviceTypeName(device)));
  }

  return {
    {QStringLiteral("name"), QString::fromStdString(profile.name)},
    {QStringLiteral("deadzone"), profile.deadzone},
    {QStringLiteral("keyboard"), keyboard},
    {QStringLiteral("controller"), controller},
    {QStringLiteral("controllerAxes"), controllerAxes},
    {QStringLiteral("devices"), devices},
  };
}

std::optional<InputProfile> profileFromJson(const QJsonObject& object)
{
  if (!object.value(QStringLiteral("name")).isString() ||
      !object.value(QStringLiteral("deadzone")).isDouble() ||
      !object.value(QStringLiteral("keyboard")).isArray() ||
      !object.value(QStringLiteral("controller")).isArray()) {
    return std::nullopt;
  }

  InputProfile profile;
  profile.name = object.value(QStringLiteral("name")).toString().toStdString();
  const auto deadzone = object.value(QStringLiteral("deadzone")).toInt(-1);
  if (deadzone < 0 || deadzone > std::numeric_limits<std::int16_t>::max()) {
    return std::nullopt;
  }
  profile.deadzone = static_cast<std::int16_t>(deadzone);

  for (const auto value : object.value(QStringLiteral("keyboard")).toArray()) {
    if (!value.isObject()) {
      return std::nullopt;
    }
    const auto binding = value.toObject();
    const auto input = parseInputName(binding.value(QStringLiteral("input")).toString());
    const auto key = binding.value(QStringLiteral("key"));
    if (!input || !key.isDouble()) {
      return std::nullopt;
    }
    profile.keyboardBindings.push_back({key.toInt(), *input});
  }

  for (const auto value : object.value(QStringLiteral("controller")).toArray()) {
    if (!value.isObject()) {
      return std::nullopt;
    }
    const auto binding = value.toObject();
    const auto input = parseInputName(binding.value(QStringLiteral("input")).toString());
    const auto buttonValue = binding.value(QStringLiteral("button"));
    const auto button = buttonValue.toInt(-1);
    if (!input || !buttonValue.isDouble() || button < 0 ||
        button >= SDL_GAMEPAD_BUTTON_COUNT) {
      return std::nullopt;
    }
    profile.controllerBindings.push_back(
      {static_cast<SDL_GamepadButton>(button), *input});
  }

  const auto controllerAxesValue = object.value(QStringLiteral("controllerAxes"));
  if (controllerAxesValue.isArray()) {
    for (const auto value : controllerAxesValue.toArray()) {
      if (!value.isObject()) {
        return std::nullopt;
      }
      const auto binding = value.toObject();
      const auto input = parseInputName(
        binding.value(QStringLiteral("input")).toString());
      const int axis = binding.value(QStringLiteral("axis")).toInt(-1);
      const int direction = binding.value(QStringLiteral("direction")).toInt(0);
      if (!input || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT ||
          (direction != -1 && direction != 1)) {
        return std::nullopt;
      }
      profile.controllerAxisBindings.push_back({
        static_cast<SDL_GamepadAxis>(axis), direction, *input});
    }
  } else {
    profile.controllerAxisBindings = defaultGenesisControllerAxisBindings();
  }

  profile.devices.fill(LogicalDeviceType::none);
  const auto devicesValue = object.value(QStringLiteral("devices"));
  if (devicesValue.isArray()) {
    const auto values = devicesValue.toArray();
    if (values.size() != static_cast<qsizetype>(profile.devices.size())) {
      return std::nullopt;
    }
    for (qsizetype index = 0; index < values.size(); ++index) {
      const auto type = parseDeviceType(values[index].toString());
      if (!type) {
        return std::nullopt;
      }
      profile.devices[static_cast<std::size_t>(index)] = *type;
    }
  } else {
    // Schema 0 did not persist advanced logical-device selections.
    profile.devices[0] = LogicalDeviceType::pad6Button;
    profile.devices[1] = LogicalDeviceType::pad6Button;
  }
  return profile;
}

QByteArray configurationToJson(const InputConfiguration& configuration)
{
  QJsonArray profiles;
  for (const auto& profile : configuration.profiles) {
    profiles.push_back(profileToJson(profile));
  }
  return QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), InputConfiguration::currentSchemaVersion},
    {QStringLiteral("activeProfile"), QString::fromStdString(configuration.activeProfile)},
    {QStringLiteral("profiles"), profiles},
  }}.toJson(QJsonDocument::Indented);
}

InputProfileLoadResult parseConfiguration(const QByteArray& bytes)
{
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return {
      .status = failure(InputProfileError::parseFailed,
        "Input profile JSON is malformed: " + parseError.errorString().toStdString()),
      .exists = true,
      .configuration = defaultInputConfiguration(),
    };
  }

  const auto root = document.object();
  const int schema = root.value(QStringLiteral("schemaVersion")).toInt(-1);
  if (schema < 0 || schema > InputConfiguration::currentSchemaVersion) {
    return {
      .status = failure(InputProfileError::unsupportedSchema,
        "Input profile schema is newer than this application supports."),
      .exists = true,
      .configuration = defaultInputConfiguration(),
    };
  }
  if (!root.value(QStringLiteral("profiles")).isArray()) {
    return {
      .status = failure(InputProfileError::parseFailed,
        "Input profile JSON does not contain a profiles array."),
      .exists = true,
      .configuration = defaultInputConfiguration(),
    };
  }

  InputConfiguration configuration;
  configuration.schemaVersion = InputConfiguration::currentSchemaVersion;
  const auto activeKey = schema == 0
    ? QStringLiteral("selectedProfile")
    : QStringLiteral("activeProfile");
  configuration.activeProfile = root.value(activeKey).toString().toStdString();
  for (const auto value : root.value(QStringLiteral("profiles")).toArray()) {
    if (!value.isObject()) {
      return {
        .status = failure(InputProfileError::parseFailed,
          "Input profile list contains a non-object value."),
        .exists = true,
        .configuration = defaultInputConfiguration(),
      };
    }
    const auto profile = profileFromJson(value.toObject());
    if (!profile) {
      return {
        .status = failure(InputProfileError::parseFailed,
          "An input profile contains invalid fields."),
        .exists = true,
        .configuration = defaultInputConfiguration(),
      };
    }
    configuration.profiles.push_back(*profile);
  }
  const auto validation = validateInputConfiguration(configuration);
  if (!validation) {
    return {
      .status = validation,
      .exists = true,
      .configuration = defaultInputConfiguration(),
    };
  }
  return {
    .status = success(),
    .exists = true,
    .migrated = schema != InputConfiguration::currentSchemaVersion,
    .configuration = std::move(configuration),
  };
}

} // namespace

std::string_view logicalDeviceTypeName(LogicalDeviceType type) noexcept
{
  switch (type) {
  case LogicalDeviceType::none:
    return "none";
  case LogicalDeviceType::pad3Button:
    return "pad-3-button";
  case LogicalDeviceType::pad6Button:
    return "pad-6-button";
  case LogicalDeviceType::segaMouse:
    return "sega-mouse";
  case LogicalDeviceType::lightGun:
    return "light-gun";
  case LogicalDeviceType::paddle:
    return "paddle";
  case LogicalDeviceType::sportsPad:
    return "sports-pad";
  case LogicalDeviceType::xe1Ap:
    return "xe-1ap";
  case LogicalDeviceType::pico:
    return "pico";
  case LogicalDeviceType::terebiOekaki:
    return "terebi-oekaki";
  case LogicalDeviceType::graphicBoard:
    return "graphic-board";
  case LogicalDeviceType::activator:
    return "activator";
  }
  return "none";
}

InputProfile* InputConfiguration::active() noexcept
{
  const auto found = std::find_if(profiles.begin(), profiles.end(),
    [this](const InputProfile& profile) { return profile.name == activeProfile; });
  return found == profiles.end() ? nullptr : &*found;
}

const InputProfile* InputConfiguration::active() const noexcept
{
  const auto found = std::find_if(profiles.begin(), profiles.end(),
    [this](const InputProfile& profile) { return profile.name == activeProfile; });
  return found == profiles.end() ? nullptr : &*found;
}

InputProfile defaultInputProfile(std::string name)
{
  InputProfile profile;
  profile.name = std::move(name);
  profile.keyboardBindings = defaultGenesisKeyboardBindings();
  profile.controllerBindings = defaultGenesisControllerBindings();
  profile.controllerAxisBindings = defaultGenesisControllerAxisBindings();
  profile.devices.fill(LogicalDeviceType::none);
  profile.devices[0] = LogicalDeviceType::pad6Button;
  profile.devices[1] = LogicalDeviceType::pad6Button;
  return profile;
}

InputConfiguration defaultInputConfiguration()
{
  InputConfiguration configuration;
  configuration.activeProfile = "Default";
  configuration.profiles.push_back(defaultInputProfile());
  return configuration;
}

std::vector<int> defaultReservedHotkeyKeys()
{
  return {Qt::Key_Space, Qt::Key_Tab, Qt::Key_N, Qt::Key_F5,
    Qt::Key_F8, Qt::Key_F12, Qt::Key_M, Qt::Key_Plus, Qt::Key_Minus};
}

InputProfileStatus validateInputProfile(const InputProfile& profile)
{
  if (profile.name.empty() || profile.name.size() > 64U) {
    return failure(InputProfileError::invalidConfiguration,
      "Input profile names must contain 1-64 bytes.");
  }
  if (profile.deadzone < 0) {
    return failure(InputProfileError::invalidConfiguration,
      "Controller deadzones cannot be negative.");
  }
  std::set<int> keyboardKeys;
  std::set<InputButton> keyboardInputs;
  const auto reservedHotkeys = defaultReservedHotkeyKeys();
  for (const auto& binding : profile.keyboardBindings) {
    if (binding.key == 0 || !isValidInput(binding.button) ||
        std::find(reservedHotkeys.begin(), reservedHotkeys.end(), binding.key) !=
          reservedHotkeys.end() ||
        !keyboardKeys.insert(binding.key).second ||
        !keyboardInputs.insert(binding.button).second) {
      return failure(InputProfileError::invalidConfiguration,
        "Keyboard bindings contain an empty or duplicate assignment.");
    }
  }
  std::set<int> controllerButtons;
  std::set<InputButton> controllerInputs;
  for (const auto& binding : profile.controllerBindings) {
    const int button = static_cast<int>(binding.button);
    if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT ||
        !isValidInput(binding.input) ||
        !controllerButtons.insert(button).second ||
        !controllerInputs.insert(binding.input).second) {
      return failure(InputProfileError::invalidConfiguration,
        "Controller bindings contain an invalid or duplicate assignment.");
    }
  }
  std::set<std::pair<int, int>> controllerAxes;
  for (const auto& binding : profile.controllerAxisBindings) {
    const int axis = static_cast<int>(binding.axis);
    if (axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT ||
        (binding.direction != -1 && binding.direction != 1) ||
        !isValidInput(binding.input) ||
        !controllerAxes.emplace(axis, binding.direction).second) {
      return failure(InputProfileError::invalidConfiguration,
        "Controller axis bindings contain an invalid or duplicate direction.");
    }
  }
  for (const auto device : profile.devices) {
    if (std::find(deviceTypes.begin(), deviceTypes.end(), device) == deviceTypes.end()) {
      return failure(InputProfileError::invalidConfiguration,
        "Input profile contains an unknown logical device type.");
    }
  }
  return success();
}

InputProfileStatus validateInputConfiguration(const InputConfiguration& configuration)
{
  if (configuration.schemaVersion != InputConfiguration::currentSchemaVersion ||
      configuration.profiles.empty() || configuration.profiles.size() > 32U) {
    return failure(InputProfileError::invalidConfiguration,
      "Input configuration schema or profile count is invalid.");
  }
  std::set<std::string> names;
  for (const auto& profile : configuration.profiles) {
    const auto validation = validateInputProfile(profile);
    if (!validation) {
      return validation;
    }
    if (!names.insert(profile.name).second) {
      return failure(InputProfileError::invalidConfiguration,
        "Input profile names must be unique.");
    }
  }
  if (configuration.active() == nullptr) {
    return failure(InputProfileError::invalidConfiguration,
      "The active input profile does not exist.");
  }
  return success();
}

std::optional<InputBindingConflict> keyboardBindingConflict(
  const InputProfile& profile,
  InputButton input,
  int key,
  const std::vector<int>& applicationHotkeys)
{
  if (std::find(applicationHotkeys.begin(), applicationHotkeys.end(), key) !=
      applicationHotkeys.end()) {
    return InputBindingConflict{
      .kind = InputConflictKind::applicationHotkey,
      .requestedInput = input,
      .existingInput = std::nullopt,
      .physicalCode = key,
    };
  }
  const auto found = std::find_if(profile.keyboardBindings.begin(),
    profile.keyboardBindings.end(),
    [input, key](const KeyboardBinding& binding) {
      return binding.key == key && binding.button != input;
    });
  if (found == profile.keyboardBindings.end()) {
    return std::nullopt;
  }
  return InputBindingConflict{
    .kind = InputConflictKind::duplicateKeyboardKey,
    .requestedInput = input,
    .existingInput = found->button,
    .physicalCode = key,
  };
}

std::optional<InputBindingConflict> controllerBindingConflict(
  const InputProfile& profile,
  InputButton input,
  SDL_GamepadButton button)
{
  const auto found = std::find_if(profile.controllerBindings.begin(),
    profile.controllerBindings.end(),
    [input, button](const ControllerBinding& binding) {
      return binding.button == button && binding.input != input;
    });
  if (found == profile.controllerBindings.end()) {
    return std::nullopt;
  }
  return InputBindingConflict{
    .kind = InputConflictKind::duplicateControllerButton,
    .requestedInput = input,
    .existingInput = found->input,
    .physicalCode = static_cast<int>(button),
  };
}

bool setKeyboardBinding(InputProfile& profile, InputButton input, int key)
{
  if (key == 0 || !isValidInput(input) ||
      keyboardBindingConflict(profile, input, key, defaultReservedHotkeyKeys())) {
    return false;
  }
  const auto found = std::find_if(profile.keyboardBindings.begin(),
    profile.keyboardBindings.end(),
    [input](const KeyboardBinding& binding) { return binding.button == input; });
  if (found == profile.keyboardBindings.end()) {
    profile.keyboardBindings.push_back({key, input});
  } else {
    found->key = key;
  }
  return true;
}

bool setControllerBinding(
  InputProfile& profile,
  InputButton input,
  SDL_GamepadButton button)
{
  if (button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT ||
      !isValidInput(input) ||
      controllerBindingConflict(profile, input, button)) {
    return false;
  }
  const auto found = std::find_if(profile.controllerBindings.begin(),
    profile.controllerBindings.end(),
    [input](const ControllerBinding& binding) { return binding.input == input; });
  if (found == profile.controllerBindings.end()) {
    profile.controllerBindings.push_back({button, input});
  } else {
    found->button = button;
  }
  return true;
}

InputProfileStore::InputProfileStore(std::filesystem::path path)
  : path_(std::move(path))
{
}

const std::filesystem::path& InputProfileStore::path() const noexcept
{
  return path_;
}

InputProfileLoadResult InputProfileStore::load() const
{
  const auto loaded = readFileBounded(path_, maximumFileBytes);
  if (!loaded.status) {
    return {
      .status = failure(InputProfileError::readFailed, loaded.status.message),
      .exists = loaded.exists,
      .configuration = defaultInputConfiguration(),
    };
  }
  if (!loaded.exists) {
    return {
      .status = success(),
      .exists = false,
      .configuration = defaultInputConfiguration(),
    };
  }
  return parseConfiguration(QByteArray{
    reinterpret_cast<const char*>(loaded.data.data()),
    static_cast<qsizetype>(loaded.data.size())});
}

InputProfileStatus InputProfileStore::save(
  const InputConfiguration& configuration) const
{
  const auto validation = validateInputConfiguration(configuration);
  if (!validation) {
    return validation;
  }
  const auto json = configurationToJson(configuration);
  const auto bytes = std::span<const std::uint8_t>{
    reinterpret_cast<const std::uint8_t*>(json.constData()),
    static_cast<std::size_t>(json.size())};
  const auto written = writeFileAtomically(path_, bytes, maximumFileBytes);
  if (!written) {
    return failure(InputProfileError::writeFailed, written.message);
  }
  return success();
}

} // namespace genplusgx::input
