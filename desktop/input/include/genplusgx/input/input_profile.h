#pragma once

#include "genplusgx/input/controller_input.h"
#include "genplusgx/input/keyboard_input.h"

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

[[nodiscard]] std::string_view logicalDeviceTypeName(LogicalDeviceType type) noexcept;

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
  static constexpr int currentSchemaVersion = 1;

  int schemaVersion{currentSchemaVersion};
  std::string activeProfile;
  std::vector<InputProfile> profiles;

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
[[nodiscard]] std::vector<int> defaultReservedHotkeyKeys();
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
