#pragma once

#include "genplusgx/input_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::movies {

inline constexpr std::uint32_t movieSchemaVersion = 1U;
inline constexpr std::size_t maximumMovieFrames = 1'000'000U;
inline constexpr std::size_t maximumInitialStateBytes = 32U * 1024U * 1024U;
inline constexpr std::size_t maximumMovieFileBytes = 96U * 1024U * 1024U;

enum class MovieError : std::uint8_t {
  none,
  invalidPath,
  invalidMovie,
  incompatibleMovie,
  fileOpenFailed,
  fileReadFailed,
  fileWriteFailed,
  fileTooLarge,
  corruptFile,
  unsupportedVersion,
  frameLimitReached,
};

struct MovieStatus final {
  MovieError error{MovieError::none};
  std::string message;

  [[nodiscard]] bool ok() const noexcept { return error == MovieError::none; }
  [[nodiscard]] operator bool() const noexcept { return ok(); }
};

struct MovieDescriptor final {
  std::string gameSha256;
  std::string settingsSha256;
  std::string coreVersion;

  [[nodiscard]] bool valid() const noexcept;
  friend bool operator==(const MovieDescriptor&, const MovieDescriptor&) = default;
};

struct MovieMetadata final {
  std::string author;
  std::string notes;
  std::uint64_t rerecordCount{0U};

  friend bool operator==(const MovieMetadata&, const MovieMetadata&) = default;
};

struct InputMovie final {
  MovieDescriptor descriptor;
  MovieMetadata metadata;
  std::uint64_t startFrame{0U};
  std::vector<std::uint8_t> initialState;
  std::vector<InputSnapshot> frames;

  [[nodiscard]] bool valid() const noexcept;
  friend bool operator==(const InputMovie&, const InputMovie&) = default;
};

struct MovieReadResult final {
  MovieStatus status;
  InputMovie movie;
};

[[nodiscard]] bool validMovieInput(const InputSnapshot& input) noexcept;
[[nodiscard]] MovieStatus compatibleMovie(
  const InputMovie& movie, const MovieDescriptor& expected) noexcept;
[[nodiscard]] MovieStatus saveMovie(
  const std::filesystem::path& path, const InputMovie& movie);
[[nodiscard]] MovieReadResult loadMovie(const std::filesystem::path& path);

[[nodiscard]] MovieStatus setFrame(
  InputMovie& movie, std::size_t index, InputSnapshot input) noexcept;
[[nodiscard]] MovieStatus insertFrames(
  InputMovie& movie, std::size_t index, std::size_t count,
  const InputSnapshot& input = {}) noexcept;
[[nodiscard]] MovieStatus eraseFrames(
  InputMovie& movie, std::size_t index, std::size_t count) noexcept;
[[nodiscard]] MovieStatus branchFrom(
  InputMovie& movie, std::size_t firstChangedFrame) noexcept;

} // namespace genplusgx::movies
