/** @file
 * @brief Poll terminal/pipe/file stdin without an uncancellable input thread.
 */
#pragma once
#include <memory>
#include <string>
namespace chat::cli {
/// @brief At most one bounded input chunk, or EOF; an empty live chunk means no ready input.
struct InputChunk {
    std::string bytes;
    bool eof = false;
};
/// @brief Single-reader native input adapter; preserves console configuration on destruction.
class ConsoleInput {
  public:
    ConsoleInput();
    ~ConsoleInput();
    ConsoleInput(const ConsoleInput&) = delete;
    ConsoleInput& operator=(const ConsoleInput&) = delete;
    /// @brief Return available bytes only; terminal editing is limited to Enter/backspace/EOF.
    [[nodiscard]] InputChunk poll();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace chat::cli
