#include "obf2/game/template_numbers.h"

#include <algorithm>
#include <cctype>

namespace obf2::game {
namespace {

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && lower(a) == lower(b);
}

bool endsWith(std::string_view text, std::string_view tail) {
  return text.size() >= tail.size() &&
         equalsIgnoreCase(text.substr(text.size() - tail.size()), tail);
}

}  // namespace

bool templateFileLess(std::string_view a, std::string_view b) {
  std::size_t i = 0, j = 0;
  while (i <= a.size() && j <= b.size()) {
    const std::size_t ai = std::min(a.find('/', i), a.size());
    const std::size_t bj = std::min(b.find('/', j), b.size());
    const std::string pa = lower(a.substr(i, ai - i));
    const std::string pb = lower(b.substr(j, bj - j));
    if (pa != pb) return pa < pb;
    const bool aEnds = ai == a.size(), bEnds = bj == b.size();
    if (aEnds || bEnds) return aEnds && !bEnds;
    i = ai + 1;
    j = bj + 1;
  }
  return false;
}

std::vector<std::string> archivesFromCon(std::string_view text) {
  std::vector<std::string> out;
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(at, end - at);
    at = end + 1;
    const std::string_view command = "fileManager.mountArchive";
    const std::size_t start = line.find_first_not_of(" \t");
    if (start == std::string_view::npos) continue;
    line = line.substr(start);
    if (line.size() <= command.size() || !equalsIgnoreCase(line.substr(0, command.size()), command)) {
      continue;
    }
    line = line.substr(command.size());
    const std::size_t nameStart = line.find_first_not_of(" \t");
    if (nameStart == std::string_view::npos) continue;
    const std::size_t nameEnd = line.find_first_of(" \t\r", nameStart);
    out.emplace_back(line.substr(nameStart, nameEnd == std::string_view::npos ? nameEnd
                                                                              : nameEnd - nameStart));
  }
  return out;
}

void TemplateNumbers::add(std::string_view name) {
  std::string key = lower(name);
  if (numbers_.count(key) != 0) return;
  numbers_.emplace(std::move(key), static_cast<std::uint32_t>(names_.size()));
  names_.emplace_back(name);
}

const std::string* TemplateNumbers::nameOf(std::uint32_t number) const {
  return number < names_.size() ? &names_[number] : nullptr;
}

std::optional<std::uint32_t> TemplateNumbers::numberOf(std::string_view name) const {
  const auto found = numbers_.find(lower(name));
  if (found == numbers_.end()) return std::nullopt;
  return found->second;
}

TemplateNumbers TemplateNumbers::build(con::FileProvider& files,
                                       const std::vector<std::vector<std::string>>& archives) {
  TemplateNumbers numbers;
  for (const auto& entries : archives) {
    std::vector<std::string> cons;
    for (const auto& path : entries) {
      if (endsWith(path, ".con")) cons.push_back(path);
    }
    std::sort(cons.begin(), cons.end(), templateFileLess);
    for (const auto& path : cons) {
      con::Options options;
      options.highestLevel = true;  // how the engine loads them, see con::Options
      con::Interpreter interpreter(
          files,
          [&numbers](const con::Command& command) {
            if (command.lowerPath != "objecttemplate.create") return;
            if (command.args.size() >= 2) numbers.add(command.args[1]);
          },
          {}, options);
      interpreter.runFile(path);
    }
  }
  return numbers;
}

}  // namespace obf2::game
