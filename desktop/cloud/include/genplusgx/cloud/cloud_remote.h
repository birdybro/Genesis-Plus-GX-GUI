#pragma once

#include "genplusgx/cloud/cloud_settings.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::cloud {

using Cancellation = std::function<bool()>;

struct RemoteReadResult final {
  Status status;
  bool exists{false};
  std::vector<std::uint8_t> data;
  std::string etag;
};

enum class WriteCondition : std::uint8_t {
  createOnly,
  match,
};

struct RemoteWriteResult final {
  Status status;
  bool preconditionFailed{false};
  std::string etag;
};

class RemoteStore {
public:
  virtual ~RemoteStore() = default;

  [[nodiscard]] virtual Status ensureCollection(
    const std::string& relativePath) = 0;
  [[nodiscard]] virtual RemoteReadResult read(
    const std::string& relativePath, std::size_t maximumBytes) = 0;
  [[nodiscard]] virtual RemoteWriteResult write(
    const std::string& relativePath,
    std::span<const std::uint8_t> data,
    WriteCondition condition,
    const std::string& etag = {}) = 0;
};

class WebDavRemoteStore final : public RemoteStore {
public:
  WebDavRemoteStore(
    Settings settings,
    std::string password,
    Cancellation cancellation = {},
    std::vector<std::string> additionalTrustedCaDer = {});
  ~WebDavRemoteStore() override;

  WebDavRemoteStore(const WebDavRemoteStore&) = delete;
  WebDavRemoteStore& operator=(const WebDavRemoteStore&) = delete;

  [[nodiscard]] Status ensureCollection(
    const std::string& relativePath) override;
  [[nodiscard]] RemoteReadResult read(
    const std::string& relativePath, std::size_t maximumBytes) override;
  [[nodiscard]] RemoteWriteResult write(
    const std::string& relativePath,
    std::span<const std::uint8_t> data,
    WriteCondition condition,
    const std::string& etag = {}) override;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

} // namespace genplusgx::cloud
