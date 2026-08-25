#include "obf2/core/path.h"

#include <cctype>
#include <vector>

namespace obf2 {
namespace {

char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

}  // namespace

std::string normalizeAssetPath(std::string_view raw) {
  std::vector<std::string_view> parts;
  parts.reserve(8);

  std::size_t start = 0;
  const std::size_t n = raw.size();
  for (std::size_t i = 0; i <= n; ++i) {
    const bool sep = (i == n) || raw[i] == '/' || raw[i] == '\\';
    if (!sep) continue;
    const std::string_view seg = raw.substr(start, i - start);
    start = i + 1;
    if (seg.empty() || seg == ".") continue;
    if (seg == "..") {
      if (!parts.empty()) parts.pop_back();
      continue;
    }
    parts.push_back(seg);
  }

  std::string out;
  std::size_t reserve = 0;
  for (const auto& p : parts) reserve += p.size() + 1;
  out.reserve(reserve);
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) out.push_back('/');
    for (const char c : parts[i]) out.push_back(lower(c));
  }
  return out;
}

std::string joinAssetPath(std::string_view base, std::string_view rel) {
  if (base.empty()) return normalizeAssetPath(rel);
  if (rel.empty()) return normalizeAssetPath(base);
  std::string combined;
  combined.reserve(base.size() + rel.size() + 1);
  combined.append(base);
  combined.push_back('/');
  combined.append(rel);
  return normalizeAssetPath(combined);
}

std::string_view assetParentDir(std::string_view normalized) {
  const std::size_t slash = normalized.find_last_of('/');
  if (slash == std::string_view::npos) return {};
  return normalized.substr(0, slash);
}

std::string_view assetExtension(std::string_view normalized) {
  const std::size_t dot = normalized.find_last_of('.');
  if (dot == std::string_view::npos) return {};
  const std::size_t slash = normalized.find_last_of('/');
  if (slash != std::string_view::npos && dot < slash) return {};
  return normalized.substr(dot + 1);
}

}  // namespace obf2
