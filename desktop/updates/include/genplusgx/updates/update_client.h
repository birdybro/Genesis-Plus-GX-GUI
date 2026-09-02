#pragma once

#include "genplusgx/updates/update_settings.h"
#include "genplusgx/updates/update_types.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace genplusgx::updates {

using Cancellation = std::function<bool()>;

struct HttpResult final {
  Status status;
  int statusCode{0};
  std::vector<std::uint8_t> data;
};

class HttpTransport {
public:
  virtual ~HttpTransport() = default;
  [[nodiscard]] virtual HttpResult get(
    const std::string& url,
    std::size_t maximumBytes,
    const Trust& trust) = 0;
  [[nodiscard]] virtual DownloadResult download(
    const Asset& asset,
    const std::filesystem::path& destinationDirectory,
    const Trust& trust) = 0;
};

class QtHttpTransport final : public HttpTransport {
public:
  explicit QtHttpTransport(Cancellation cancellation = {},
    std::vector<std::string> additionalTrustedCaDer = {});
  ~QtHttpTransport() override;
  QtHttpTransport(const QtHttpTransport&) = delete;
  QtHttpTransport& operator=(const QtHttpTransport&) = delete;

  [[nodiscard]] HttpResult get(const std::string& url,
    std::size_t maximumBytes, const Trust& trust) override;
  [[nodiscard]] DownloadResult download(const Asset& asset,
    const std::filesystem::path& destinationDirectory,
    const Trust& trust) override;

private:
  class Private;
  std::unique_ptr<Private> private_;
};

[[nodiscard]] CheckResult checkForUpdate(
  const Settings& settings,
  std::string_view currentVersion,
  const Trust& trust,
  HttpTransport& transport);

} // namespace genplusgx::updates
