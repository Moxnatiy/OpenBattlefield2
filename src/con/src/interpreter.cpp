#include "obf2/con/interpreter.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <unordered_map>

#include "obf2/con/lexer.h"
#include "obf2/core/path.h"

namespace obf2::con {
namespace {

std::string toLower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

// Numbers in a .con are always in the C format ("0.06", "-1.5e2"), regardless of
// the user's locale. from_chars for float is not in every stdlib (libc++ went
// without it for a long time), so where it is missing strtof remains — and then
// the engine is obliged to keep LC_NUMERIC="C".
bool parseFloat(std::string_view s, float& out) {
  if (s.empty()) return false;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  const char* first = s.data();
  const char* last = s.data() + s.size();
  const auto res = std::from_chars(first, last, out);
  return res.ec == std::errc{} && res.ptr == last;
#else
  const std::string tmp(s);
  char* end = nullptr;
  const float v = std::strtof(tmp.c_str(), &end);
  if (end != tmp.c_str() + tmp.size()) return false;
  out = v;
  return true;
#endif
}

bool parseInt(std::string_view s, int& out) {
  if (s.empty()) return false;
  const char* first = s.data();
  const char* last = s.data() + s.size();
  const auto res = std::from_chars(first, last, out);
  return res.ec == std::errc{} && res.ptr == last;
}

}  // namespace

// --- Command -----------------------------------------------------------------

std::string_view Command::argStr(std::size_t i) const {
  return i < args.size() ? std::string_view{args[i]} : std::string_view{};
}

std::optional<float> Command::argFloat(std::size_t i) const {
  float v = 0.0f;
  if (i < args.size() && parseFloat(args[i], v)) return v;
  return std::nullopt;
}

std::optional<int> Command::argInt(std::size_t i) const {
  int v = 0;
  if (i < args.size() && parseInt(args[i], v)) return v;
  return std::nullopt;
}

std::optional<bool> Command::argBool(std::size_t i) const {
  if (i >= args.size()) return std::nullopt;
  const std::string lower = toLower(args[i]);
  if (lower == "1" || lower == "true" || lower == "yes") return true;
  if (lower == "0" || lower == "false" || lower == "no") return false;
  return std::nullopt;
}

std::optional<Vec3> Command::argVec3(std::size_t i) const {
  if (i >= args.size()) return std::nullopt;
  const std::string_view s = args[i];
  Vec3 v;
  float* dst[3] = {&v.x, &v.y, &v.z};
  std::size_t start = 0;
  int found = 0;
  for (std::size_t p = 0; p <= s.size(); ++p) {
    if (p != s.size() && s[p] != '/') continue;
    if (found >= 3) return std::nullopt;
    if (!parseFloat(s.substr(start, p - start), *dst[found])) return std::nullopt;
    ++found;
    start = p + 1;
  }
  return found == 3 ? std::optional<Vec3>{v} : std::nullopt;
}

// --- Interpreter -------------------------------------------------------------

struct Interpreter::Frame {
  std::unordered_map<std::string, std::string> vars;  // keys in lower case
};

Interpreter::Interpreter(FileProvider& files, CommandFn onCommand, DiagnosticFn onDiagnostic,
                         Options options)
    : files_(files),
      onCommand_(std::move(onCommand)),
      onDiagnostic_(std::move(onDiagnostic)),
      options_(options) {}

void Interpreter::diagnose(std::string_view file, int line, std::string message,
                           Severity severity) {
  if (severity == Severity::Error) ++errors_; else ++warnings_;
  if (onDiagnostic_) {
    onDiagnostic_(Diagnostic{severity, std::string(file), line, std::move(message)});
  }
}

bool Interpreter::runFile(std::string_view path, const std::vector<std::string>& args) {
  const std::string normalized = normalizeAssetPath(path);
  const auto text = files_.loadText(normalized);
  if (!text) {
    diagnose(normalized, 0, "file not found");  // the entry point — already an error
    return false;
  }
  return execute(*text, normalized, args, 0);
}

bool Interpreter::runText(std::string_view text, std::string_view virtualPath,
                          const std::vector<std::string>& args) {
  return execute(text, normalizeAssetPath(virtualPath), args, 0);
}

bool Interpreter::execute(std::string_view text, std::string_view normalizedPath,
                          const std::vector<std::string>& args, int depth) {
  if (depth > options_.maxIncludeDepth) {
    diagnose(normalizedPath, 0, "include/run nesting limit exceeded");
    return false;
  }
  if (std::find(activeFiles_.begin(), activeFiles_.end(), normalizedPath) != activeFiles_.end()) {
    diagnose(normalizedPath, 0, "cyclic include");
    return false;
  }
  activeFiles_.emplace_back(normalizedPath);
  struct Pop {
    std::vector<std::string>& v;
    ~Pop() { v.pop_back(); }
  } pop{activeFiles_};

  Frame frame;
  for (std::size_t i = 0; i < args.size(); ++i) {
    frame.vars["v_arg" + std::to_string(i + 1)] = args[i];
  }

  const std::string dir(assetParentDir(normalizedPath));

  auto substitute = [&frame](std::string token) {
    std::string_view name = token;
    if (!name.empty() && name.front() == '$') name.remove_prefix(1);
    const auto it = frame.vars.find(toLower(name));
    return it == frame.vars.end() ? token : it->second;
  };

  int remDepth = 0;               // beginRem/endRem nesting
  std::vector<bool> ifStack;      // true = the branch is executed
  auto skipping = [&ifStack] {
    return std::find(ifStack.begin(), ifStack.end(), false) != ifStack.end();
  };

  bool ok = true;
  int lineNo = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t eol = text.find('\n', pos);
    const std::string_view line =
        text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
    pos = (eol == std::string_view::npos) ? text.size() + 1 : eol + 1;
    ++lineNo;

    std::vector<std::string> tokens = tokenizeLine(line);
    if (tokens.empty()) continue;
    const std::string keyword = toLower(tokens[0]);

    // 1. Block comments have the highest priority: nothing applies inside them.
    if (keyword == "beginrem") { ++remDepth; continue; }
    if (keyword == "endrem") {
      if (remDepth > 0) --remDepth;
      else diagnose(normalizedPath, lineNo, "endRem without beginRem");
      continue;
    }
    if (remDepth > 0) continue;
    if (keyword == "rem") continue;

    // 2. if/endIf is counted even in a skipped branch, otherwise the nesting slides.
    if (keyword == "if") {
      bool value = false;
      if (tokens.size() >= 4) {
        const std::string lhs = substitute(tokens[1]);
        const std::string rhs = substitute(tokens[3]);
        const std::string& op = tokens[2];
        if (op == "==") value = (lhs == rhs);
        else if (op == "!=") value = (lhs != rhs);
        else diagnose(normalizedPath, lineNo, "unknown operator in if: " + op);
      } else if (tokens.size() == 2) {
        const std::string v = substitute(tokens[1]);
        value = !v.empty() && v != "0";
      } else {
        diagnose(normalizedPath, lineNo, "empty expression in if");
      }
      ifStack.push_back(value);
      continue;
    }
    if (keyword == "endif") {
      if (!ifStack.empty()) ifStack.pop_back();
      else diagnose(normalizedPath, lineNo, "endIf without if");
      continue;
    }
    if (skipping()) continue;

    // 3. Local variables: `var v_dist = 20`
    if (keyword == "var") {
      if (tokens.size() >= 4 && tokens[2] == "=") {
        frame.vars[toLower(tokens[1])] = substitute(tokens[3]);
      } else if (tokens.size() == 2) {
        frame.vars[toLower(tokens[1])] = std::string{};
      } else {
        diagnose(normalizedPath, lineNo, "malformed var");
      }
      continue;
    }

    // 4. Including other files. include keeps the same context, run is a call
    //    with arguments that become v_arg1..N. For us the only difference is the arguments.
    if (keyword == "include" || keyword == "run") {
      if (tokens.size() < 2) {
        diagnose(normalizedPath, lineNo, keyword + " without a path");
        continue;
      }
      const std::string target = joinAssetPath(dir, substitute(tokens[1]));
      std::vector<std::string> callArgs;
      for (std::size_t i = 2; i < tokens.size(); ++i) callArgs.push_back(substitute(tokens[i]));

      auto nested = files_.loadText(target);
      if (!nested) {
        // A second chance: the path from the mod's root rather than relative to the current file.
        const std::string fromRoot = normalizeAssetPath(substitute(tokens[1]));
        nested = files_.loadText(fromRoot);
        if (nested) {
          ok &= execute(*nested, fromRoot, callArgs, depth + 1);
          continue;
        }
        // As in the original: a missing include does not stop loading.
        diagnose(normalizedPath, lineNo, keyword + ": file not found — " + target,
                 Severity::Warning);
        continue;
      }
      ok &= execute(*nested, target, callArgs, depth + 1);
      if (!ok && options_.stopOnError) return false;
      continue;
    }

    // 5. An ordinary command.
    Command cmd;
    cmd.path = splitCommandPath(tokens[0]);
    if (cmd.path.empty()) {
      diagnose(normalizedPath, lineNo, "empty command name");
      continue;
    }
    cmd.lowerPath = toLower(tokens[0]);
    cmd.args.reserve(tokens.size() - 1);
    for (std::size_t i = 1; i < tokens.size(); ++i) cmd.args.push_back(substitute(tokens[i]));
    cmd.file = normalizedPath;
    cmd.line = lineNo;
    if (onCommand_) onCommand_(cmd);
  }

  if (remDepth != 0) diagnose(normalizedPath, lineNo, "unclosed beginRem");
  if (!ifStack.empty()) diagnose(normalizedPath, lineNo, "unclosed if");
  return ok;
}

}  // namespace obf2::con
