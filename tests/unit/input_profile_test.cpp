#include "genplusgx/input/input_profile.h"

#include "genplusgx/persistence.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyCombination>
#include <QKeySequence>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const std::string& message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool writeBytes(const std::filesystem::path& path, const QByteArray& data)
{
  return genplusgx::writeFileAtomically(
    path,
    std::span<const std::uint8_t>{
      reinterpret_cast<const std::uint8_t*>(data.constData()),
      static_cast<std::size_t>(data.size())},
    genplusgx::input::InputProfileStore::maximumFileBytes);
}

} // namespace

int main()
{
  using namespace genplusgx;
  using namespace genplusgx::input;

  auto configuration = defaultInputConfiguration();
  const auto reservedDefaults = defaultReservedHotkeyKeys();
  if (!check(validateInputConfiguration(configuration),
        "Default input configuration did not validate") ||
      !check(configuration.active() != nullptr,
        "Default active profile was not found") ||
      !check(configuration.active()->keyboardBindings.size() == 12U &&
        configuration.active()->controllerBindings.size() == 12U &&
        configuration.active()->controllerAxisBindings.size() == 4U,
        "Default profile does not contain both complete six-button layouts") ||
      !check(configuration.active()->devices[0] == LogicalDeviceType::pad6Button &&
        configuration.active()->devices[1] == LogicalDeviceType::pad6Button,
        "Default logical devices are incorrect") ||
      !check(coreInputSettings(*configuration.active()).devices[0] ==
          CoreInputDevice::pad6Button &&
        coreInputSettings(*configuration.active()).devices[1] ==
          CoreInputDevice::pad6Button,
        "Default logical devices did not map to core-neutral settings") ||
      !check(configuration.hotkeys.size() == emulatorHotkeyActionCount,
        "Default emulator hotkeys are incomplete") ||
      !check(hotkeyCombination(configuration, EmulatorHotkeyAction::softReset) ==
        QKeyCombination{Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_R}
          .toCombined(),
        "Soft-reset hotkey default is incorrect") ||
      !check(hotkeyCombination(
          configuration, EmulatorHotkeyAction::fastForwardHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Tab}.toCombined() &&
        hotkeyCombination(
          configuration, EmulatorHotkeyAction::fastForwardToggle) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_QuoteLeft}.toCombined(),
        "Fast-forward hold/toggle defaults are incorrect") ||
      !check(hotkeyCombination(configuration, EmulatorHotkeyAction::rewindHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Backspace}.toCombined(),
        "Rewind hold hotkey default is incorrect") ||
      !check(hotkeyCombination(
          configuration, EmulatorHotkeyAction::slowMotionHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Slash}.toCombined() &&
        hotkeyCombination(
          configuration, EmulatorHotkeyAction::slowMotionToggle) ==
          QKeyCombination{Qt::ControlModifier, Qt::Key_Slash}.toCombined(),
        "Slow-motion hold/toggle defaults are incorrect") ||
      !check(std::ranges::find(reservedDefaults, Qt::Key_M) !=
        reservedDefaults.end(),
        "Unmodified emulator hotkeys were not reserved from gameplay")) {
    return EXIT_FAILURE;
  }

  auto* profile = configuration.active();
  const auto keyboardDuplicate = keyboardBindingConflict(
    *profile, InputButton::b, Qt::Key_Z);
  const auto hotkey = keyboardBindingConflict(
    *profile, InputButton::b, Qt::Key_M, {Qt::Key_M});
  const auto controllerDuplicate = controllerBindingConflict(
    *profile, InputButton::c, SDL_GAMEPAD_BUTTON_SOUTH);
  if (!check(keyboardDuplicate &&
        keyboardDuplicate->kind == InputConflictKind::duplicateKeyboardKey &&
        keyboardDuplicate->existingInput == InputButton::a,
        "Duplicate keyboard binding was not identified") ||
      !check(hotkey && hotkey->kind == InputConflictKind::applicationHotkey,
        "Application hotkey conflict was not identified") ||
      !check(controllerDuplicate &&
        controllerDuplicate->kind == InputConflictKind::duplicateControllerButton &&
        controllerDuplicate->existingInput == InputButton::b,
        "Duplicate controller binding was not identified") ||
      !check(!setKeyboardBinding(*profile, InputButton::b, Qt::Key_Z),
        "Duplicate keyboard assignment was accepted") ||
      !check(!setControllerBinding(
        *profile, InputButton::c, SDL_GAMEPAD_BUTTON_SOUTH),
        "Duplicate controller assignment was accepted") ||
      !check(setKeyboardBinding(*profile, InputButton::a, Qt::Key_Q),
        "Unique keyboard reassignment failed") ||
      !check(setControllerBinding(
        *profile, InputButton::a, SDL_GAMEPAD_BUTTON_MISC1),
        "Unique controller reassignment failed")) {
    return EXIT_FAILURE;
  }
  profile->deadzone = 12'500;
  profile->devices[0] = LogicalDeviceType::segaMouse;
  const auto mute = std::ranges::find_if(configuration.hotkeys,
    [](const HotkeyBinding& binding) {
      return binding.action == EmulatorHotkeyAction::mute;
    });
  if (!check(mute != configuration.hotkeys.end(), "Mute hotkey was not found")) {
    return EXIT_FAILURE;
  }
  mute->keyCombination = QKeyCombination{
    Qt::ControlModifier | Qt::AltModifier, Qt::Key_M}.toCombined();
  if (!check(validateInputConfiguration(configuration),
        "A unique customized hotkey was rejected") ||
      !check(coreInputSettings(*profile).devices[0] ==
          CoreInputDevice::segaMouse &&
        coreInputSettings(*profile).devices[1] ==
          CoreInputDevice::pad6Button,
        "Specialized input devices did not map to the core model")) {
    return EXIT_FAILURE;
  }

  QTemporaryDir directory;
  if (!check(directory.isValid(), "Could not create temporary profile directory")) {
    return EXIT_FAILURE;
  }
  const auto path = std::filesystem::path{directory.path().toStdString()} /
    "nested" / "input-profiles.json";
  InputProfileStore store{path};
  const auto missing = store.load();
  if (!check(missing.status && !missing.exists &&
        missing.configuration == defaultInputConfiguration(),
        "Missing profile file did not produce safe defaults") ||
      !check(store.save(configuration), "Valid input profile could not be saved")) {
    return EXIT_FAILURE;
  }
  const auto loaded = store.load();
  if (!check(loaded.status && loaded.exists && !loaded.migrated,
        "Saved input profile did not load") ||
      !check(loaded.configuration == configuration,
        "Input profile persistence did not round-trip exactly")) {
    return EXIT_FAILURE;
  }

  auto invalid = configuration;
  invalid.profiles.push_back(invalid.profiles.front());
  if (!check(validateInputConfiguration(invalid).error ==
        InputProfileError::invalidConfiguration,
        "Duplicate profile names were not rejected") ||
      !check(store.save(invalid).error == InputProfileError::invalidConfiguration,
        "Invalid profile configuration was written")) {
    return EXIT_FAILURE;
  }
  auto invalidHotkey = defaultInputConfiguration();
  invalidHotkey.active()->keyboardBindings.front().key = Qt::Key_M;
  if (!check(validateInputConfiguration(invalidHotkey).error ==
        InputProfileError::invalidConfiguration,
      "A persisted application-hotkey conflict was accepted")) {
    return EXIT_FAILURE;
  }
  auto gappedDevices = defaultInputConfiguration();
  gappedDevices.active()->devices[1] = LogicalDeviceType::none;
  gappedDevices.active()->devices[2] = LogicalDeviceType::pad6Button;
  auto invalidMultitapDevices = defaultInputConfiguration();
  invalidMultitapDevices.active()->devices[0] = LogicalDeviceType::segaMouse;
  invalidMultitapDevices.active()->devices[2] = LogicalDeviceType::pad6Button;
  if (!check(validateInputConfiguration(gappedDevices).error ==
        InputProfileError::invalidConfiguration,
        "A gapped player-device layout was accepted") ||
      !check(validateInputConfiguration(invalidMultitapDevices).error ==
        InputProfileError::invalidConfiguration,
        "A non-pad multitap layout was accepted")) {
    return EXIT_FAILURE;
  }
  auto duplicateHotkey = defaultInputConfiguration();
  duplicateHotkey.hotkeys[1].keyCombination =
    duplicateHotkey.hotkeys[0].keyCombination;
  auto missingHotkey = defaultInputConfiguration();
  missingHotkey.hotkeys.pop_back();
  auto gameplayConflict = configuration;
  const auto pause = std::ranges::find_if(gameplayConflict.hotkeys,
    [](const HotkeyBinding& binding) {
      return binding.action == EmulatorHotkeyAction::pause;
    });
  pause->keyCombination = QKeyCombination{Qt::NoModifier, Qt::Key_Q}.toCombined();
  if (!check(validateInputConfiguration(duplicateHotkey).error ==
        InputProfileError::invalidConfiguration,
        "Duplicate emulator shortcut was accepted") ||
      !check(validateInputConfiguration(missingHotkey).error ==
        InputProfileError::invalidConfiguration,
        "Incomplete emulator hotkeys were accepted") ||
      !check(validateInputConfiguration(gameplayConflict).error ==
        InputProfileError::invalidConfiguration,
        "Configured hotkey/gameplay conflict was accepted")) {
    return EXIT_FAILURE;
  }

  const auto persisted = readFileBounded(path, InputProfileStore::maximumFileBytes);
  auto legacyRoot = QJsonDocument::fromJson(QByteArray{
    reinterpret_cast<const char*>(persisted.data.data()),
    static_cast<qsizetype>(persisted.data.size())}).object();
  auto schemaTwoRoot = legacyRoot;
  schemaTwoRoot.insert(QStringLiteral("schemaVersion"), 2);
  auto schemaTwoHotkeys = schemaTwoRoot.value(QStringLiteral("hotkeys")).toArray();
  for (qsizetype index = schemaTwoHotkeys.size() - 1; index >= 0; --index) {
    auto binding = schemaTwoHotkeys[index].toObject();
    const auto action = binding.value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("fast-forward-hold")) {
      schemaTwoHotkeys.removeAt(index);
    } else if (action == QStringLiteral("fast-forward-toggle")) {
      binding.insert(QStringLiteral("action"), QStringLiteral("fast-forward"));
      binding.insert(QStringLiteral("sequence"), QStringLiteral("Tab"));
      schemaTwoHotkeys[index] = binding;
    }
  }
  schemaTwoRoot.insert(QStringLiteral("hotkeys"), schemaTwoHotkeys);
  if (!check(writeBytes(path, QJsonDocument{schemaTwoRoot}.toJson()),
        "Could not create the schema-2 fast-forward migration fixture")) {
    return EXIT_FAILURE;
  }
  const auto schemaTwo = store.load();
  if (!check(schemaTwo.status && schemaTwo.migrated &&
        hotkeyCombination(schemaTwo.configuration,
          EmulatorHotkeyAction::fastForwardToggle) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Tab}.toCombined() &&
        hotkeyCombination(schemaTwo.configuration,
          EmulatorHotkeyAction::fastForwardHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_QuoteLeft}.toCombined(),
        "Schema 2 did not preserve toggle behavior and add a unique hold binding")) {
    return EXIT_FAILURE;
  }

  auto schemaThreeRoot = legacyRoot;
  schemaThreeRoot.insert(QStringLiteral("schemaVersion"), 3);
  auto schemaThreeHotkeys =
    schemaThreeRoot.value(QStringLiteral("hotkeys")).toArray();
  for (qsizetype index = schemaThreeHotkeys.size() - 1; index >= 0; --index) {
    const auto action = schemaThreeHotkeys[index].toObject()
      .value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("rewind-hold")) {
      schemaThreeHotkeys.removeAt(index);
    }
  }
  schemaThreeRoot.insert(QStringLiteral("hotkeys"), schemaThreeHotkeys);
  if (!check(writeBytes(path, QJsonDocument{schemaThreeRoot}.toJson()),
        "Could not create the schema-3 rewind migration fixture")) {
    return EXIT_FAILURE;
  }
  const auto schemaThree = store.load();
  if (!check(schemaThree.status && schemaThree.migrated &&
        hotkeyCombination(schemaThree.configuration,
          EmulatorHotkeyAction::rewindHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Backspace}.toCombined() &&
        hotkeyCombination(schemaThree.configuration,
          EmulatorHotkeyAction::slowMotionHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Slash}.toCombined() &&
        hotkeyCombination(schemaThree.configuration,
          EmulatorHotkeyAction::slowMotionToggle) ==
          QKeyCombination{Qt::ControlModifier, Qt::Key_Slash}.toCombined(),
        "Schema 3 did not add conflict-free rewind and slow-motion bindings")) {
    return EXIT_FAILURE;
  }

  auto schemaFourRoot = legacyRoot;
  schemaFourRoot.insert(QStringLiteral("schemaVersion"), 4);
  auto schemaFourHotkeys =
    schemaFourRoot.value(QStringLiteral("hotkeys")).toArray();
  for (qsizetype index = schemaFourHotkeys.size() - 1; index >= 0; --index) {
    const auto action = schemaFourHotkeys[index].toObject()
      .value(QStringLiteral("action")).toString();
    if (action == QStringLiteral("slow-motion-hold") ||
        action == QStringLiteral("slow-motion-toggle")) {
      schemaFourHotkeys.removeAt(index);
    }
  }
  schemaFourRoot.insert(QStringLiteral("hotkeys"), schemaFourHotkeys);
  if (!check(writeBytes(path, QJsonDocument{schemaFourRoot}.toJson()),
        "Could not create the schema-4 slow-motion migration fixture")) {
    return EXIT_FAILURE;
  }
  const auto schemaFour = store.load();
  if (!check(schemaFour.status && schemaFour.migrated &&
        hotkeyCombination(schemaFour.configuration,
          EmulatorHotkeyAction::slowMotionHold) ==
          QKeyCombination{Qt::NoModifier, Qt::Key_Slash}.toCombined() &&
        hotkeyCombination(schemaFour.configuration,
          EmulatorHotkeyAction::slowMotionToggle) ==
          QKeyCombination{Qt::ControlModifier, Qt::Key_Slash}.toCombined(),
        "Schema 4 did not add conflict-free slow-motion bindings")) {
    return EXIT_FAILURE;
  }

  legacyRoot.insert(QStringLiteral("schemaVersion"), 1);
  legacyRoot.remove(QStringLiteral("hotkeys"));
  if (!check(writeBytes(path, QJsonDocument{legacyRoot}.toJson()),
        "Could not create the schema-1 migration fixture")) {
    return EXIT_FAILURE;
  }
  const auto schemaOne = store.load();
  if (!check(schemaOne.status && schemaOne.migrated &&
        schemaOne.configuration.hotkeys == defaultEmulatorHotkeys(),
        "Schema 1 did not migrate to complete default hotkeys")) {
    return EXIT_FAILURE;
  }

  legacyRoot.insert(QStringLiteral("schemaVersion"), 0);
  legacyRoot.insert(QStringLiteral("selectedProfile"),
    legacyRoot.take(QStringLiteral("activeProfile")));
  auto legacyProfiles = legacyRoot.value(QStringLiteral("profiles")).toArray();
  for (qsizetype index = 0; index < legacyProfiles.size(); ++index) {
    auto object = legacyProfiles[index].toObject();
    object.remove(QStringLiteral("devices"));
    object.remove(QStringLiteral("controllerAxes"));
    legacyProfiles[index] = object;
  }
  legacyRoot.insert(QStringLiteral("profiles"), legacyProfiles);
  if (!check(writeBytes(path, QJsonDocument{legacyRoot}.toJson()),
        "Could not create the legacy migration fixture")) {
    return EXIT_FAILURE;
  }
  const auto migrated = store.load();
  if (!check(migrated.status && migrated.migrated,
        "Schema 0 input profile was not migrated") ||
      !check(migrated.configuration.schemaVersion ==
        InputConfiguration::currentSchemaVersion,
        "Migrated configuration retained its old schema") ||
      !check(migrated.configuration.active()->devices[0] ==
        LogicalDeviceType::pad6Button,
        "Migration did not supply the legacy device default") ||
      !check(migrated.configuration.hotkeys == defaultEmulatorHotkeys(),
        "Migration did not supply default emulator hotkeys")) {
    return EXIT_FAILURE;
  }

  legacyRoot.insert(QStringLiteral("schemaVersion"), 999);
  if (!check(writeBytes(path, QJsonDocument{legacyRoot}.toJson()),
        "Could not create future-schema fixture") ||
      !check(store.load().status.error == InputProfileError::unsupportedSchema,
        "Future input schema was not rejected")) {
    return EXIT_FAILURE;
  }
  if (!check(writeBytes(path, QByteArray{"{ definitely not JSON"}),
        "Could not create corrupt profile fixture")) {
    return EXIT_FAILURE;
  }
  const auto corrupt = store.load();
  if (!check(corrupt.status.error == InputProfileError::parseFailed &&
        corrupt.configuration == defaultInputConfiguration(),
        "Corrupt input profile did not fail safely with defaults")) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
