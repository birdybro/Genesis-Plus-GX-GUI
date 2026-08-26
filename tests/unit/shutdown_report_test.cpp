#include "genplusgx/app/shutdown_report.h"

#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  genplusgx::app::ShutdownReport clean{0};
  if (!check(clean.succeeded(), "A clean shutdown was reported as failed") ||
      !check(clean.exitCode() == 0, "A clean shutdown changed the exit code") ||
      !check(clean.summary().empty(), "A clean shutdown produced a summary")) {
    return 1;
  }

  genplusgx::app::ShutdownReport failed{0};
  failed.addFailure("Emulation worker", "Save data could not be flushed.");
  failed.addFailure("", "");
  if (!check(!failed.succeeded(), "Cleanup failures were ignored") ||
      !check(failed.exitCode() != 0, "Cleanup failure retained a success code") ||
      !check(failed.failures().size() == 2U, "Cleanup failures were lost") ||
      !check(
        failed.summary().find("Emulation worker") != std::string::npos,
        "The shutdown summary omitted the service") ||
      !check(
        failed.summary().find("Unknown service") != std::string::npos,
        "Empty shutdown diagnostics were not normalized")) {
    return 2;
  }

  genplusgx::app::ShutdownReport applicationFailed{7};
  applicationFailed.addFailure("Audio output", "Device shutdown failed.");
  if (!check(
        applicationFailed.exitCode() == 7,
        "Cleanup failure replaced the application's existing exit code")) {
    return 3;
  }

  return 0;
}
