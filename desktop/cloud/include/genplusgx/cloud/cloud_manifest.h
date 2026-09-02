#pragma once

#include "genplusgx/cloud/cloud_settings.h"

#include <map>
#include <optional>
#include <span>
#include <vector>

namespace genplusgx::cloud {

struct FileRecord final {
  std::string key;
  std::string sha256;
  std::uint64_t size{0U};

  [[nodiscard]] bool operator==(const FileRecord&) const = default;
};

using FileMap = std::map<std::string, FileRecord>;

struct RemoteManifest final {
  FileMap files;

  [[nodiscard]] bool operator==(const RemoteManifest&) const = default;
};

struct Baseline final {
  std::string key;
  std::string localSha256;
  std::string remoteSha256;

  [[nodiscard]] bool operator==(const Baseline&) const = default;
};

struct LocalManifest final {
  std::map<std::string, Baseline> files;
};

struct ManifestResult final {
  Status status;
  RemoteManifest manifest;
};

struct LocalManifestResult final {
  Status status;
  LocalManifest manifest;
};

struct FileScanResult final {
  Status status;
  FileMap files;
};

[[nodiscard]] bool validSha256(const std::string& value) noexcept;
[[nodiscard]] bool validFileKey(const std::string& key) noexcept;
[[nodiscard]] std::size_t maximumBytesForKey(const std::string& key) noexcept;
[[nodiscard]] std::string baselineId(const Settings& settings);
[[nodiscard]] Action chooseAction(
  const FileRecord* local,
  const FileRecord* remote,
  const Baseline* baseline) noexcept;
[[nodiscard]] std::vector<std::uint8_t> encodeRemoteManifest(
  const RemoteManifest& manifest);
[[nodiscard]] ManifestResult decodeRemoteManifest(
  std::span<const std::uint8_t> data);
[[nodiscard]] FileScanResult scanLocalFiles(
  const ApplicationPaths& paths, const Settings& settings);
[[nodiscard]] std::optional<std::filesystem::path> localPathForKey(
  const ApplicationPaths& paths, const std::string& key);

class LocalManifestStore final {
public:
  static constexpr std::uint32_t schemaVersion = 1U;

  explicit LocalManifestStore(std::filesystem::path path);
  [[nodiscard]] LocalManifestResult load() const;
  [[nodiscard]] Status save(const LocalManifest& manifest) const;

private:
  std::filesystem::path path_;
};

} // namespace genplusgx::cloud
