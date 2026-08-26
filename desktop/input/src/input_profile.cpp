#include "genplusgx/input/input_profile.h"

#include "genplusgx/persistence.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyCombination>
#include <QKeySequence>
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

constexpr std::array hotkeyActions{
  EmulatorHotkeyAction::openGame,
  EmulatorHotkeyAction::closeGame,
  EmulatorHotkeyAction::gameLibrary,
  EmulatorHotkeyAction::pause,
  EmulatorHotkeyAction::hardReset,
  EmulatorHotkeyAction::softReset,
  EmulatorHotkeyAction::fullscreen,
  EmulatorHotkeyAction::fastForwardHold,
  EmulatorHotkeyAction::fastForwardToggle,
  EmulatorHotkeyAction::frameAdvance,
  EmulatorHotkeyAction::saveState,
  EmulatorHotkeyAction::loadState,
  EmulatorHotkeyAction::stateSlot0,
  EmulatorHotkeyAction::stateSlot1,
  EmulatorHotkeyAction::stateSlot2,
  EmulatorHotkeyAction::stateSlot3,
  EmulatorHotkeyAction::stateSlot4,
  EmulatorHotkeyAction::stateSlot5,
  EmulatorHotkeyAction::stateSlot6,
  EmulatorHotkeyAction::stateSlot7,
  EmulatorHotkeyAction::stateSlot8,
  EmulatorHotkeyAction::stateSlot9,
  EmulatorHotkeyAction::previousStateSlot,
  EmulatorHotkeyAction::nextStateSlot,
  EmulatorHotkeyAction::deleteState,
  EmulatorHotkeyAction::screenshot,
  EmulatorHotkeyAction::mute,
  EmulatorHotkeyAction::volumeUp,
  EmulatorHotkeyAction::volumeDown,
};

static_assert(hotkeyActions.size() == emulatorHotkeyActionCount);

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

std::optional<EmulatorHotkeyAction> parseHotkeyAction(const QString& name)
{
  const auto text = name.toStdString();
  const auto found = std::find_if(hotkeyActions.begin(), hotkeyActions.end(),
    [&text](EmulatorHotkeyAction action) {
      return emulatorHotkeyActionName(action) == text;
    });
  return found == hotkeyActions.end() ? std::nullopt : std::optional{*found};
}

bool isModifierKey(Qt::Key key) noexcept
{
  return key == Qt::Key_Shift || key == Qt::Key_Control ||
    key == Qt::Key_Meta || key == Qt::Key_Alt || key == Qt::Key_AltGr;
}

bool isValidHotkeyCombination(int combined) noexcept
{
  if (combined == 0) {
    return false;
  }
  const auto combination = QKeyCombination::fromCombined(combined);
  return combination.key() != Qt::Key_unknown &&
    !isModifierKey(combination.key()) &&
    !QKeySequence{combination}.toString(QKeySequence::PortableText).isEmpty();
}

int standardCombination(QKeySequence::StandardKey standard, int fallback)
{
  if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
    return fallback;
  }
  const QKeySequence sequence{standard};
  return sequence.count() == 0 ? fallback : sequence[0].toCombined();
}

int combined(Qt::KeyboardModifiers modifiers, Qt::Key key) noexcept
{
  return QKeyCombination{modifiers, key}.toCombined();
}

std::optional<std::vector<HotkeyBinding>> hotkeysFromJson(
  const QJsonValue& value,
  int schema)
{
  if (!value.isArray()) {
    return std::nullopt;
  }
  std::vector<HotkeyBinding> hotkeys;
  const auto values = value.toArray();
  hotkeys.reserve(static_cast<std::size_t>(values.size()));
  for (const auto entry : values) {
    if (!entry.isObject()) {
      return std::nullopt;
    }
    const auto object = entry.toObject();
    const auto actionName = object.value(QStringLiteral("action")).toString();
    const auto action = schema < 3 && actionName == QStringLiteral("fast-forward")
      ? std::optional{EmulatorHotkeyAction::fastForwardToggle}
      : parseHotkeyAction(actionName);
    const auto sequenceText = object.value(QStringLiteral("sequence"));
    if (!action || !sequenceText.isString()) {
      return std::nullopt;
    }
    const auto sequence = QKeySequence::fromString(
      sequenceText.toString(), QKeySequence::PortableText);
    if (sequence.count() != 1 || !isValidHotkeyCombination(sequence[0].toCombined())) {
      return std::nullopt;
    }
    hotkeys.push_back({*action, sequence[0].toCombined()});
  }
  if (schema < 3) {
    const std::array candidates{
      combined(Qt::NoModifier, Qt::Key_Tab),
      combined(Qt::NoModifier, Qt::Key_QuoteLeft),
      combined(Qt::NoModifier, Qt::Key_Backslash),
    };
    const auto available = std::ranges::find_if(candidates, [&hotkeys](int candidate) {
      return std::ranges::none_of(hotkeys, [candidate](const HotkeyBinding& binding) {
        return binding.keyCombination == candidate;
      });
    });
    if (available == candidates.end()) {
      return std::nullopt;
    }
    hotkeys.push_back({EmulatorHotkeyAction::fastForwardHold, *available});
  }
  return hotkeys;
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
  QJsonArray hotkeys;
  for (const auto& binding : configuration.hotkeys) {
    hotkeys.push_back(QJsonObject{
      {QStringLiteral("action"),
        QString::fromLatin1(emulatorHotkeyActionName(binding.action))},
      {QStringLiteral("sequence"),
        QKeySequence{QKeyCombination::fromCombined(binding.keyCombination)}
          .toString(QKeySequence::PortableText)},
    });
  }
  return QJsonDocument{QJsonObject{
    {QStringLiteral("schemaVersion"), InputConfiguration::currentSchemaVersion},
    {QStringLiteral("activeProfile"), QString::fromStdString(configuration.activeProfile)},
    {QStringLiteral("profiles"), profiles},
    {QStringLiteral("hotkeys"), hotkeys},
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
  if (schema < 2) {
    configuration.hotkeys = defaultEmulatorHotkeys();
  } else {
    const auto hotkeys = hotkeysFromJson(
      root.value(QStringLiteral("hotkeys")), schema);
    if (!hotkeys) {
      return {
        .status = failure(InputProfileError::parseFailed,
          "Input profile JSON contains invalid emulator hotkeys."),
        .exists = true,
        .configuration = defaultInputConfiguration(),
      };
    }
    configuration.hotkeys = *hotkeys;
  }
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

std::string_view emulatorHotkeyActionName(EmulatorHotkeyAction action) noexcept
{
  switch (action) {
  case EmulatorHotkeyAction::openGame:
    return "open-game";
  case EmulatorHotkeyAction::closeGame:
    return "close-game";
  case EmulatorHotkeyAction::gameLibrary:
    return "game-library";
  case EmulatorHotkeyAction::pause:
    return "pause";
  case EmulatorHotkeyAction::hardReset:
    return "hard-reset";
  case EmulatorHotkeyAction::softReset:
    return "soft-reset";
  case EmulatorHotkeyAction::fullscreen:
    return "fullscreen";
  case EmulatorHotkeyAction::fastForwardHold:
    return "fast-forward-hold";
  case EmulatorHotkeyAction::fastForwardToggle:
    return "fast-forward-toggle";
  case EmulatorHotkeyAction::frameAdvance:
    return "frame-advance";
  case EmulatorHotkeyAction::saveState:
    return "save-state";
  case EmulatorHotkeyAction::loadState:
    return "load-state";
  case EmulatorHotkeyAction::stateSlot0:
    return "state-slot-0";
  case EmulatorHotkeyAction::stateSlot1:
    return "state-slot-1";
  case EmulatorHotkeyAction::stateSlot2:
    return "state-slot-2";
  case EmulatorHotkeyAction::stateSlot3:
    return "state-slot-3";
  case EmulatorHotkeyAction::stateSlot4:
    return "state-slot-4";
  case EmulatorHotkeyAction::stateSlot5:
    return "state-slot-5";
  case EmulatorHotkeyAction::stateSlot6:
    return "state-slot-6";
  case EmulatorHotkeyAction::stateSlot7:
    return "state-slot-7";
  case EmulatorHotkeyAction::stateSlot8:
    return "state-slot-8";
  case EmulatorHotkeyAction::stateSlot9:
    return "state-slot-9";
  case EmulatorHotkeyAction::previousStateSlot:
    return "previous-state-slot";
  case EmulatorHotkeyAction::nextStateSlot:
    return "next-state-slot";
  case EmulatorHotkeyAction::deleteState:
    return "delete-state";
  case EmulatorHotkeyAction::screenshot:
    return "screenshot";
  case EmulatorHotkeyAction::mute:
    return "mute";
  case EmulatorHotkeyAction::volumeUp:
    return "volume-up";
  case EmulatorHotkeyAction::volumeDown:
    return "volume-down";
  }
  return {};
}

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

CoreInputSettings coreInputSettings(const InputProfile& profile) noexcept
{
  CoreInputSettings settings;
  for (std::size_t index = 0U; index < profile.devices.size(); ++index) {
    switch (profile.devices[index]) {
      case LogicalDeviceType::none:
        settings.devices[index] = CoreInputDevice::none;
        break;
      case LogicalDeviceType::pad3Button:
        settings.devices[index] = CoreInputDevice::pad3Button;
        break;
      case LogicalDeviceType::pad6Button:
        settings.devices[index] = CoreInputDevice::pad6Button;
        break;
      case LogicalDeviceType::segaMouse:
        settings.devices[index] = CoreInputDevice::segaMouse;
        break;
      case LogicalDeviceType::lightGun:
        settings.devices[index] = CoreInputDevice::lightGun;
        break;
      case LogicalDeviceType::paddle:
        settings.devices[index] = CoreInputDevice::paddle;
        break;
      case LogicalDeviceType::sportsPad:
        settings.devices[index] = CoreInputDevice::sportsPad;
        break;
      case LogicalDeviceType::xe1Ap:
        settings.devices[index] = CoreInputDevice::xe1Ap;
        break;
      case LogicalDeviceType::pico:
        settings.devices[index] = CoreInputDevice::pico;
        break;
      case LogicalDeviceType::terebiOekaki:
        settings.devices[index] = CoreInputDevice::terebiOekaki;
        break;
      case LogicalDeviceType::graphicBoard:
        settings.devices[index] = CoreInputDevice::graphicBoard;
        break;
      case LogicalDeviceType::activator:
        settings.devices[index] = CoreInputDevice::activator;
        break;
    }
  }
  return settings;
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
  configuration.hotkeys = defaultEmulatorHotkeys();
  return configuration;
}

std::vector<HotkeyBinding> defaultEmulatorHotkeys()
{
  const auto control = Qt::ControlModifier;
  return {
    {EmulatorHotkeyAction::openGame,
      standardCombination(QKeySequence::Open,
        combined(control, Qt::Key_O))},
    {EmulatorHotkeyAction::closeGame,
      standardCombination(QKeySequence::Close,
        combined(control, Qt::Key_W))},
    {EmulatorHotkeyAction::gameLibrary, combined(control, Qt::Key_L)},
    {EmulatorHotkeyAction::pause, combined(Qt::NoModifier, Qt::Key_Space)},
    {EmulatorHotkeyAction::hardReset, combined(control, Qt::Key_R)},
    {EmulatorHotkeyAction::softReset,
      combined(control | Qt::ShiftModifier, Qt::Key_R)},
    {EmulatorHotkeyAction::fullscreen, combined(Qt::AltModifier, Qt::Key_Return)},
    {EmulatorHotkeyAction::fastForwardHold,
      combined(Qt::NoModifier, Qt::Key_Tab)},
    {EmulatorHotkeyAction::fastForwardToggle,
      combined(Qt::NoModifier, Qt::Key_QuoteLeft)},
    {EmulatorHotkeyAction::frameAdvance, combined(Qt::NoModifier, Qt::Key_N)},
    {EmulatorHotkeyAction::saveState, combined(Qt::NoModifier, Qt::Key_F5)},
    {EmulatorHotkeyAction::loadState, combined(Qt::NoModifier, Qt::Key_F8)},
    {EmulatorHotkeyAction::stateSlot0, combined(control, Qt::Key_0)},
    {EmulatorHotkeyAction::stateSlot1, combined(control, Qt::Key_1)},
    {EmulatorHotkeyAction::stateSlot2, combined(control, Qt::Key_2)},
    {EmulatorHotkeyAction::stateSlot3, combined(control, Qt::Key_3)},
    {EmulatorHotkeyAction::stateSlot4, combined(control, Qt::Key_4)},
    {EmulatorHotkeyAction::stateSlot5, combined(control, Qt::Key_5)},
    {EmulatorHotkeyAction::stateSlot6, combined(control, Qt::Key_6)},
    {EmulatorHotkeyAction::stateSlot7, combined(control, Qt::Key_7)},
    {EmulatorHotkeyAction::stateSlot8, combined(control, Qt::Key_8)},
    {EmulatorHotkeyAction::stateSlot9, combined(control, Qt::Key_9)},
    {EmulatorHotkeyAction::previousStateSlot,
      combined(control, Qt::Key_BracketLeft)},
    {EmulatorHotkeyAction::nextStateSlot,
      combined(control, Qt::Key_BracketRight)},
    {EmulatorHotkeyAction::deleteState, combined(control, Qt::Key_Delete)},
    {EmulatorHotkeyAction::screenshot, combined(Qt::NoModifier, Qt::Key_F12)},
    {EmulatorHotkeyAction::mute, combined(Qt::NoModifier, Qt::Key_M)},
    {EmulatorHotkeyAction::volumeUp, combined(Qt::NoModifier, Qt::Key_Plus)},
    {EmulatorHotkeyAction::volumeDown, combined(Qt::NoModifier, Qt::Key_Minus)},
  };
}

std::vector<int> reservedGameplayHotkeyKeys(
  const std::vector<HotkeyBinding>& hotkeys)
{
  std::set<int> reserved;
  for (const auto& binding : hotkeys) {
    const auto combination = QKeyCombination::fromCombined(binding.keyCombination);
    const auto modifiers = combination.keyboardModifiers();
    if (modifiers.testAnyFlags(
          Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
      continue;
    }
    reserved.insert(static_cast<int>(combination.key()));
    if (modifiers.testFlag(Qt::ShiftModifier)) {
      reserved.insert(Qt::Key_Shift);
    }
  }
  return {reserved.begin(), reserved.end()};
}

std::vector<int> defaultReservedHotkeyKeys()
{
  return reservedGameplayHotkeyKeys(defaultEmulatorHotkeys());
}

std::optional<int> hotkeyCombination(
  const InputConfiguration& configuration,
  EmulatorHotkeyAction action) noexcept
{
  const auto found = std::find_if(configuration.hotkeys.begin(),
    configuration.hotkeys.end(),
    [action](const HotkeyBinding& binding) { return binding.action == action; });
  return found == configuration.hotkeys.end()
    ? std::nullopt : std::optional{found->keyCombination};
}

namespace {

InputProfileStatus validateInputProfileWithHotkeys(
  const InputProfile& profile,
  const std::vector<int>& reservedHotkeys)
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
  if (!validateCoreInputSettings(coreInputSettings(profile))) {
    return failure(InputProfileError::invalidConfiguration,
      "Emulated devices must be contiguous; 3-8 players require pads, and Pico or Terebi must be the only device.");
  }
  return success();
}

} // namespace

InputProfileStatus validateInputProfile(const InputProfile& profile)
{
  return validateInputProfileWithHotkeys(profile, defaultReservedHotkeyKeys());
}

InputProfileStatus validateInputConfiguration(const InputConfiguration& configuration)
{
  if (configuration.schemaVersion != InputConfiguration::currentSchemaVersion ||
      configuration.profiles.empty() || configuration.profiles.size() > 32U) {
    return failure(InputProfileError::invalidConfiguration,
      "Input configuration schema or profile count is invalid.");
  }
  if (configuration.hotkeys.size() != emulatorHotkeyActionCount) {
    return failure(InputProfileError::invalidConfiguration,
      "Every emulator hotkey must have exactly one assignment.");
  }
  std::set<EmulatorHotkeyAction> hotkeyAssignments;
  std::set<int> hotkeyCombinations;
  for (const auto& binding : configuration.hotkeys) {
    if (std::find(hotkeyActions.begin(), hotkeyActions.end(), binding.action) ==
          hotkeyActions.end() ||
        !isValidHotkeyCombination(binding.keyCombination) ||
        !hotkeyAssignments.insert(binding.action).second ||
        !hotkeyCombinations.insert(binding.keyCombination).second) {
      return failure(InputProfileError::invalidConfiguration,
        "Emulator hotkeys contain an empty, unknown, or duplicate assignment.");
    }
  }
  const auto reservedHotkeys = reservedGameplayHotkeyKeys(configuration.hotkeys);
  std::set<std::string> names;
  for (const auto& profile : configuration.profiles) {
    const auto validation = validateInputProfileWithHotkeys(profile, reservedHotkeys);
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
  return setKeyboardBinding(
    profile, input, key, defaultReservedHotkeyKeys());
}

bool setKeyboardBinding(
  InputProfile& profile,
  InputButton input,
  int key,
  const std::vector<int>& applicationHotkeys)
{
  if (key == 0 || !isValidInput(input) ||
      keyboardBindingConflict(profile, input, key, applicationHotkeys)) {
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
