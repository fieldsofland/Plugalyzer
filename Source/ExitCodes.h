#pragma once

namespace ExitCode {
constexpr int Success = 0;
constexpr int CliUsageError = 1;
constexpr int InfrastructureError = 2;
constexpr int TestFailure = 3;
constexpr int CrashOrTimeout = 4;
constexpr int MissingDependency = 5;
constexpr int Unsupported = 6;
} // namespace ExitCode
