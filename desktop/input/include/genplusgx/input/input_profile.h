#pragma once

#include "genplusgx/input/controller_input.h"
#include "genplusgx/input/keyboard_input.h"
#include "genplusgx/core_input_settings.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace genplusgx::input {

enum class LogicalDeviceType : std::uint8_t {
  none,
  pad3Button,
  pad6Button,
  segaMouse,
  lightGun,
  paddle,
  sportsPad,
  xe1Ap,
  pico,
  terebiOekaki,
  graphicBoard,
  activator,
};

enum class EmulatorHotkeyAction : std::uint8_t {
  openGame,
  closeGame,
  gameLibrary,
  pause,
  hardReset,
  softReset,
  fullscreen,
  fastForwardHold,
  fastForwardToggle,
  frameAdvance,
  saveState,
  loadState,
  stateSlot0,
  stateSlot1,
  stateSlot2,
  stateSlot3,
  stateSlot4,
  stateSlot5,
  stateSlot6,
  stateSlot7,
  stateSlot8,
  stateSlot9,
  previousStateSlot,
  nextStateSlot,
  deleteState,
  screenshot,
  mute,
  volumeUp,
  volumeDown,
};

inline constexpr std::size_t emulatorHotkeyActionCount = 29U;

struct HotkeyBinding final {
  EmulatorHotkeyAction action{EmulatorHotkeyAction::openGame};
  int keyCombination{0};

  friend bool operator==(const HotkeyBinding&, const HotkeyBinding&) = default;
};

[[nodiscard]] std::string_view emulatorHotkeyActionName(
  EmulatorHotkeyAction action) noexcept;

[[nodiscard]] std::string_view logicalDeviceTypeName(LogicalDeviceType type) noexcept;

struct InputProfile;
[[nodiscard]] CoreInputSettings coreInputSettings(
  const InputProfile& profile) noexcept;

struct InputProfile final {
  std::string name;
  std::int16_t deadzone{8'000};
  std::vector<KeyboardBinding> keyboardBindings;
  std::vector<ControllerBinding> controllerBindings;
  std::vector<ControllerAxisBinding> controllerAxisBindings;
  std::array<LogicalDeviceType, InputSnapshot::maximumPlayers> devices{};

  friend bool operator==(const InputProfile&, const InputProfile&) = default;
};

struct InputConfiguration final {
  static constexpr int currentSchemaVersion = 3;

  int schemaVersion{currentSchemaVersion};
  std::string activeProfile;
  std::vector<InputProfile> profiles;
  std::vector<HotkeyBinding> hotkeys;

  [[nodiscard]] InputProfile* active() noexcept;
  [[nodiscard]] const InputProfile* active() const noexcept;

  friend bool operator==(const InputConfiguration&, const InputConfiguration&) = default;
};

enum class InputProfileError {
  none,
  invalidConfiguration,
  unsupportedSchema,
  parseFailed,
  readFailed,
  writeFailed,
};

struct InputProfileStatus final {
  InputProfileError error{InputProfileError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == InputProfileError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct InputProfileLoadResult final {
  InputProfileStatus status;
  bool exists{false};
  bool migrated{false};
  InputConfiguration configuration;
};

enum class InputConflictKind {
  duplicateKeyboardKey,
  duplicateControllerButton,
  applicationHotkey,
};

struct InputBindingConflict final {
  InputConflictKind kind{InputConflictKind::duplicateKeyboardKey};
  InputButton requestedInput{InputButton::a};
  std::optional<InputButton> existingInput;
  int physicalCode{0};

  friend bool operator==(const InputBindingConflict&, const InputBindingConflict&) = default;
};

[[nodiscard]] InputProfile defaultInputProfile(std::string name = "Default");
[[nodiscard]] InputConfiguration defaultInputConfiguration();
[[nodiscard]] std::vector<HotkeyBinding> defaultEmulatorHotkeys();
[[nodiscard]] std::vector<int> reservedGameplayHotkeyKeys(
  const std::vector<HotkeyBinding>& hotkeys);
[[nodiscard]] std::vector<int> defaultReservedHotkeyKeys();
[[nodiscard]] std::optional<int> hotkeyCombination(
  const InputConfiguration& configuration,
  EmulatorHotkeyAction action) noexcept;
[[nodiscard]] InputProfileStatus validateInputProfile(const InputProfile& profile);
[[nodiscard]] InputProfileStatus validateInputConfiguration(
  const InputConfiguration& configuration);

[[nodiscard]] std::optional<InputBindingConflict> keyboardBindingConflict(
  const InputProfile& profile,
  InputButton input,
  int key,
  const std::vector<int>& applicationHotkeys = {});
[[nodiscard]] std::optional<InputBindingConflict> controllerBindingConflict(
  const InputProfile& profile,
  InputButton input,
  SDL_GamepadButton button);
[[nodiscard]] bool setKeyboardBinding(InputProfile& profile, InputButton input, int key);
[[nodiscard]] bool setKeyboardBinding(
  InputProfile& profile,
  InputButton input,
  int key,
  const std::vector<int>& applicationHotkeys);
[[nodiscard]] bool setControllerBinding(
  InputProfile& profile,
  InputButton input,
  SDL_GamepadButton button);

class InputProfileStore final {
public:
  static constexpr std::size_t maximumFileBytes = 1024U * 1024U;

  explicit InputProfileStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] InputProfileLoadResult load() const;
  [[nodiscard]] InputProfileStatus save(
    const InputConfiguration& configuration) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::input
