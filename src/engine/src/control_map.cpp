#include "obf2/engine/control_map.h"

#include <cctype>

namespace obf2::engine {

void ControlMap::add(MappingKind kind, const con::Command& command) {
  if (command.args.empty()) return;

  Mapping mapping;
  mapping.kind = kind;
  mapping.action = command.args[0];
  if (command.args.size() > 1) mapping.device = command.args[1];
  for (std::size_t i = 2; i < command.args.size(); ++i) {
    mapping.elements.push_back(command.args[i]);
  }
  mappings_.push_back(std::move(mapping));
}

void ControlMap::bind(Console& console) {
  console.bind("ControlMap.create", [this](const con::Command& c) {
    maps_.emplace_back(c.argStr(0));
  });

  console.bind("ControlMap.addKeyToTriggerMapping",
               [this](const con::Command& c) { add(MappingKind::KeyToTrigger, c); });
  console.bind("ControlMap.addButtonToTriggerMapping",
               [this](const con::Command& c) { add(MappingKind::ButtonToTrigger, c); });
  console.bind("ControlMap.addAxisToTriggerMapping",
               [this](const con::Command& c) { add(MappingKind::AxisToTrigger, c); });
  console.bind("ControlMap.addAxisToAxisMapping",
               [this](const con::Command& c) { add(MappingKind::AxisToAxis, c); });
  console.bind("ControlMap.addKeysToAxisMapping",
               [this](const con::Command& c) { add(MappingKind::KeysToAxis, c); });

  console.bind("ControlMap.mouseSensitivity", [this](const con::Command& c) {
    mouseSensitivity_ = c.argFloat(0).value_or(mouseSensitivity_);
  });
  console.bind("ControlMap.mouseInvert", [this](const con::Command& c) {
    mouseInvert_ = c.argBool(0).value_or(mouseInvert_);
  });

  // Ці команди в стокових файлах трапляються, але на стан не впливають —
  // приймаємо їх мовчки, щоб не засмічувати список нереалізованого.
  console.bind("ControlMap.setDefaultMap", [](const con::Command&) {});
  console.bind("ControlMap.activateMap", [](const con::Command&) {});
}

std::vector<const Mapping*> ControlMap::forAction(std::string_view action) const {
  auto equalsIgnoringCase = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  };

  std::vector<const Mapping*> found;
  for (const Mapping& mapping : mappings_) {
    if (equalsIgnoringCase(mapping.action, action)) found.push_back(&mapping);
  }
  return found;
}

}  // namespace obf2::engine
