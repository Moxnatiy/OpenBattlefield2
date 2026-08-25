#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obf2::con {

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

// Одна виконана команда, напр. `ObjectTemplate.fire.projectileStartPosition 0.06/-0.12/0`
struct Command {
  std::vector<std::string> path;  // {"ObjectTemplate","fire","projectileStartPosition"}
  std::string lowerPath;          // "objecttemplate.fire.projectilestartposition"
  std::vector<std::string> args;  // {"0.06/-0.12/0"}
  std::string file;               // нормалізований шлях джерела
  int line = 0;                   // 1-based

  std::string_view target() const { return path.empty() ? std::string_view{} : path.front(); }
  std::string_view method() const { return path.empty() ? std::string_view{} : path.back(); }

  std::optional<float> argFloat(std::size_t i) const;
  std::optional<int> argInt(std::size_t i) const;
  std::optional<bool> argBool(std::size_t i) const;
  // "0.06/-0.12/0" — типовий для BF2 запис вектора через слеш.
  std::optional<Vec3> argVec3(std::size_t i) const;
  std::string_view argStr(std::size_t i) const;
};

// Refractor 2 мовчки ковтає відсутній include: у самій грі є 159 таких
// посилань (напр. objects/kits/ch/ch_kits.tweak, якого немає в жодному архіві),
// і вона від цього не падає. Тому "файл не знайдено" — це Warning, а не Error,
// інакше жоден оригінальний мод не завантажиться.
enum class Severity { Warning, Error };

struct Diagnostic {
  Severity severity = Severity::Error;
  std::string file;
  int line = 0;
  std::string message;

  bool isError() const { return severity == Severity::Error; }
};

// Джерело файлів: диск, zip-архів або тестова заглушка.
// Шлях приходить уже нормалізованим (див. obf2::normalizeAssetPath).
class FileProvider {
 public:
  virtual ~FileProvider() = default;
  virtual std::optional<std::string> loadText(std::string_view normalizedPath) = 0;
};

struct Options {
  int maxIncludeDepth = 64;
  bool stopOnError = false;
};

// Інтерпретатор мови .con/.tweak.
//
// Мова — потік команд, а не дерево, тому AST не будуємо: кожна виконана
// команда одразу віддається у callback. Керівні конструкції (rem, beginrem,
// if/endIf, var, include/run) обробляє сам інтерпретатор і назовні не віддає.
class Interpreter {
 public:
  using CommandFn = std::function<void(const Command&)>;
  using DiagnosticFn = std::function<void(const Diagnostic&)>;

  Interpreter(FileProvider& files, CommandFn onCommand, DiagnosticFn onDiagnostic = {},
              Options options = {});

  // args -> v_arg1, v_arg2, ... у викликаному файлі (як `run file.con a b`).
  bool runFile(std::string_view path, const std::vector<std::string>& args = {});
  bool runText(std::string_view text, std::string_view virtualPath,
               const std::vector<std::string>& args = {});

  int errorCount() const { return errors_; }
  int warningCount() const { return warnings_; }

 private:
  struct Frame;
  bool execute(std::string_view text, std::string_view normalizedPath,
               const std::vector<std::string>& args, int depth);
  void diagnose(std::string_view file, int line, std::string message,
                Severity severity = Severity::Error);

  FileProvider& files_;
  CommandFn onCommand_;
  DiagnosticFn onDiagnostic_;
  Options options_;
  int errors_ = 0;
  int warnings_ = 0;
  std::vector<std::string> activeFiles_;  // захист від циклічних include
};

}  // namespace obf2::con
