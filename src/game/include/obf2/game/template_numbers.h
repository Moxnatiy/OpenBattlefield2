#pragma once
// The number the server gives each object template, reproduced from the game's
// own archives — the number `CreateObjectEvent` carries instead of a name.
//
// The number is the order of creation (`ObjectTemplateManager::createTemplate`,
// bf2_events.h). The order, found by matching against 22 pairs measured on a
// live Strike at Karkand server (`tools/template_order.py --walk lower --check`,
// every pair at offset 0; docs/functions/network-events.md):
//
//   1. the archives in the order `ServerArchives.con` mounts them;
//   2. inside each, every `.con` in a directory walk that sorts each directory's
//      entries by their lower-case names — so `_asia` comes before `ambient…`,
//      unlike the zip's own order, which put 266 templates in the wrong place;
//   3. inside each file, `ObjectTemplate.create` in the order the interpreter runs
//      them, `include`d and `run` files included;
//   4. a name created again takes no new number — the 25 repeats before the
//      soldiers were exactly the offset against the server.
//
// Checked only for the mod's archives; the level's own templates (control points,
// 5720 and 5721 on Karkand) come after them and are not numbered here.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "obf2/con/interpreter.h"

namespace obf2::game {

// Step 2's order: component by component, each compared lower-case.
bool templateFileLess(std::string_view a, std::string_view b);

// `fileManager.mountArchive <zip> <mount>` lines, in order.
std::vector<std::string> archivesFromCon(std::string_view text);

class TemplateNumbers {
 public:
  // `archives` holds every archive's file list (VFS paths), in mount order.
  static TemplateNumbers build(con::FileProvider& files,
                               const std::vector<std::vector<std::string>>& archives);

  // Step 4: a name already numbered is ignored.
  void add(std::string_view name);

  const std::string* nameOf(std::uint32_t number) const;
  std::optional<std::uint32_t> numberOf(std::string_view name) const;
  std::size_t size() const { return names_.size(); }

 private:
  std::vector<std::string> names_;
  std::unordered_map<std::string, std::uint32_t> numbers_;  // lower-case name
};

}  // namespace obf2::game
