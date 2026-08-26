#pragma once

#include <string>
#include <vector>

namespace genplusgx::app {

struct ShutdownFailure final {
  std::string service;
  std::string detail;
};

class ShutdownReport final {
public:
  explicit ShutdownReport(int applicationExitCode) noexcept;

  void addFailure(std::string service, std::string detail);

  [[nodiscard]] bool succeeded() const noexcept;
  [[nodiscard]] int exitCode() const noexcept;
  [[nodiscard]] const std::vector<ShutdownFailure>& failures() const noexcept;
  [[nodiscard]] std::string summary() const;

private:
  int applicationExitCode_{0};
  std::vector<ShutdownFailure> failures_;
};

} // namespace genplusgx::app
