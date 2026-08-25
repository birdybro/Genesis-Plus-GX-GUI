#pragma once

#include "genplusgx/persistence.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace genplusgx {

struct RecentGame final {
  std::filesystem::path path;
  std::int64_t lastOpenedMilliseconds{0};

  [[nodiscard]] bool operator==(const RecentGame&) const = default;
};

class RecentGamesModel final {
public:
  static constexpr std::size_t maximumEntries = 12U;

  [[nodiscard]] bool add(
    const std::filesystem::path& path,
    std::int64_t lastOpenedMilliseconds);
  [[nodiscard]] bool remove(const std::filesystem::path& path);
  void clear() noexcept;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const std::vector<RecentGame>& entries() const noexcept;

private:
  std::vector<RecentGame> entries_;
};

struct RecentGamesLoadResult final {
  PersistenceStatus status;
  RecentGamesModel model;
  bool migrated{false};
};

class RecentGamesStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;
  static constexpr std::size_t maximumFileBytes = 64U * 1024U;

  explicit RecentGamesStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path& path() const noexcept;
  [[nodiscard]] RecentGamesLoadResult load() const;
  [[nodiscard]] PersistenceStatus save(const RecentGamesModel& model) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx
