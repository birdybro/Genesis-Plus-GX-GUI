#pragma once

#include "genplusgx/persistence.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace genplusgx::platform {

enum class BiosSlot : std::uint8_t {
  genesis,
  masterSystemUsa,
  masterSystemEurope,
  masterSystemJapan,
  gameGear,
  segaCdUsa,
  segaCdEurope,
  segaCdJapan,
};

inline constexpr std::size_t biosSlotCount = 8U;

struct BiosDescriptor final {
  BiosSlot slot{BiosSlot::genesis};
  std::string_view key;
  std::string_view displayName;
  std::string_view expectedRegion;
};

[[nodiscard]] const std::array<BiosDescriptor, biosSlotCount>&
biosDescriptors() noexcept;
[[nodiscard]] const BiosDescriptor& biosDescriptor(BiosSlot slot) noexcept;

struct BiosConfiguration final {
  std::array<std::filesystem::path, biosSlotCount> paths;

  [[nodiscard]] const std::filesystem::path& path(BiosSlot slot) const noexcept;
  void setPath(BiosSlot slot, std::filesystem::path path);
  [[nodiscard]] bool operator==(const BiosConfiguration&) const = default;
};

enum class BiosValidationState : std::uint8_t {
  notConfigured,
  missing,
  notRegularFile,
  unreadable,
  pathTooLong,
  invalidSize,
  invalidContent,
  valid,
};

struct BiosValidation final {
  BiosSlot slot{BiosSlot::genesis};
  std::filesystem::path path;
  BiosValidationState state{BiosValidationState::notConfigured};
  std::uintmax_t fileSize{0};
  std::string sha256;
  std::string detectedType;
  std::string message;

  [[nodiscard]] bool valid() const noexcept
  {
    return state == BiosValidationState::valid;
  }
};

[[nodiscard]] BiosValidation validateBios(
  BiosSlot slot,
  const std::filesystem::path& path);

struct BiosSnapshot final {
  BiosConfiguration configuration;
  std::array<BiosValidation, biosSlotCount> validation;
};

struct BiosConfigurationLoadResult final {
  PersistenceStatus status;
  BiosConfiguration configuration;
  bool migrated{false};
};

class BiosConfigurationStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 64U * 1024U;

  explicit BiosConfigurationStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] BiosConfigurationLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(
    const BiosConfiguration& configuration) const;

private:
  std::filesystem::path path_;
};

class BiosManager final {
public:
  explicit BiosManager(BiosConfigurationStore store);

  [[nodiscard]] PersistenceStatus load();
  [[nodiscard]] PersistenceStatus apply(BiosConfiguration configuration);
  [[nodiscard]] const BiosSnapshot& snapshot() const noexcept;

private:
  void refreshValidation();

  BiosConfigurationStore store_;
  BiosSnapshot snapshot_;
};

} // namespace genplusgx::platform
