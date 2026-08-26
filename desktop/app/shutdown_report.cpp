#include "genplusgx/app/shutdown_report.h"

#include <utility>

namespace genplusgx::app {

ShutdownReport::ShutdownReport(int applicationExitCode) noexcept
  : applicationExitCode_(applicationExitCode)
{
}

void ShutdownReport::addFailure(std::string service, std::string detail)
{
  if (service.empty()) {
    service = "Unknown service";
  }
  if (detail.empty()) {
    detail = "No diagnostic detail was provided.";
  }
  failures_.push_back({std::move(service), std::move(detail)});
}

bool ShutdownReport::succeeded() const noexcept { return failures_.empty(); }

int ShutdownReport::exitCode() const noexcept
{
  return applicationExitCode_ != 0
    ? applicationExitCode_
    : (succeeded() ? 0 : 1);
}

const std::vector<ShutdownFailure>& ShutdownReport::failures() const noexcept
{
  return failures_;
}

std::string ShutdownReport::summary() const
{
  std::string result;
  for (const auto &failure : failures_) {
    if (!result.empty()) {
      result += ' ';
    }
    result += failure.service + ": " + failure.detail;
  }
  return result;
}

} // namespace genplusgx::app
