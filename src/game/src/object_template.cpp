#include "obf2/game/object_template.h"

#include <cctype>

namespace obf2::game {
namespace {

std::string toLower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

// Property simply reuses con::Command's value parsing so that the rules
// ("0.06/-0.12/0" is a vector) live in one place.
con::Command asCommand(const Property& property) {
  con::Command command;
  command.args = property.args;
  return command;
}

}  // namespace

std::optional<float> Property::asFloat(std::size_t index) const {
  return asCommand(*this).argFloat(index);
}
std::optional<int> Property::asInt(std::size_t index) const {
  return asCommand(*this).argInt(index);
}
std::optional<bool> Property::asBool(std::size_t index) const {
  return asCommand(*this).argBool(index);
}
std::optional<Vec3> Property::asVec3(std::size_t index) const {
  return asCommand(*this).argVec3(index);
}

// --- ObjectTemplate ----------------------------------------------------------

const std::vector<Property>* ObjectTemplate::propertyHistory(std::string_view name) const {
  const auto it = properties.find(toLower(name));
  return it == properties.end() ? nullptr : &it->second;
}

const Property* ObjectTemplate::property(std::string_view name) const {
  const auto* history = propertyHistory(name);
  return (history == nullptr || history->empty()) ? nullptr : &history->back();
}

const Component* ObjectTemplate::component(std::string_view name) const {
  const std::string wanted = toLower(name);
  for (const Component& candidate : components) {
    if (toLower(candidate.name) == wanted) return &candidate;
  }
  return nullptr;
}

std::string_view ObjectTemplate::text(std::string_view propertyName) const {
  const Property* found = property(propertyName);
  return found == nullptr ? std::string_view{} : found->value(0);
}

std::optional<float> ObjectTemplate::number(std::string_view propertyName) const {
  const Property* found = property(propertyName);
  return found == nullptr ? std::nullopt : found->asFloat(0);
}

// --- Registry ----------------------------------------------------------------

ObjectTemplate& Registry::createTemplate(std::string className, std::string name,
                                         const con::Command& command) {
  auto created = std::make_unique<ObjectTemplate>();
  created->className = std::move(className);
  created->name = std::move(name);
  created->file = command.file;
  created->line = command.line;

  ObjectTemplate* pointer = created.get();
  order_.push_back(std::move(created));
  templates_[toLower(pointer->name)] = pointer;
  return *pointer;
}

void Registry::applyProperty(PropertyMap& target, const std::string& key,
                             const con::Command& command) {
  Property property;
  property.args = command.args;
  property.file = command.file;
  property.line = command.line;
  target[key].push_back(std::move(property));
  ++stats_.propertiesSet;
}

void Registry::feed(const con::Command& command) {
  if (command.path.empty()) return;
  if (toLower(command.path.front()) != "objecttemplate") return;

  // ObjectTemplate.<component>.<property> addresses a sub-object.
  // The component may not have been created yet: a .tweak often simply reopens
  // it, without a createComponent.
  if (command.path.size() >= 3) {
    if (active_ == nullptr) { ++stats_.orphanCommands; return; }

    const std::string componentName = command.path[1];
    Component* component = nullptr;
    for (Component& candidate : active_->components) {
      if (toLower(candidate.name) == toLower(componentName)) { component = &candidate; break; }
    }
    if (component == nullptr) {
      active_->components.push_back(Component{componentName, {}});
      component = &active_->components.back();
    }

    // A tail longer than one segment is glued back together: such paths occur,
    // and part of the name must not be lost.
    std::string key = toLower(command.path[2]);
    for (std::size_t i = 3; i < command.path.size(); ++i) key += "." + toLower(command.path[i]);
    applyProperty(component->properties, key, command);
    return;
  }

  if (command.path.size() != 2) return;
  const std::string method = toLower(command.path[1]);

  if (method == "create" || method == "activesafe") {
    if (command.args.size() < 2) { ++stats_.orphanCommands; return; }
    const std::string& className = command.args[0];
    const std::string& name = command.args[1];

    // activeSafe reopens an existing template — that is exactly how a .tweak
    // adds properties to what the .con created.
    const auto existing = templates_.find(toLower(name));
    if (existing != templates_.end()) {
      active_ = existing->second;
      ++stats_.reopened;
    } else {
      active_ = &createTemplate(className, name, command);
      ++stats_.created;
    }
    lastChild_ = nullptr;
    return;
  }

  if (active_ == nullptr) { ++stats_.orphanCommands; return; }

  if (method == "createcomponent") {
    if (command.args.empty()) { ++stats_.orphanCommands; return; }
    if (active_->component(command.args[0]) == nullptr) {
      active_->components.push_back(Component{command.args[0], {}});
      ++stats_.componentsCreated;
    }
    return;
  }

  if (method == "addtemplate") {
    if (command.args.empty()) { ++stats_.orphanCommands; return; }
    active_->children.push_back(ChildTemplate{command.args[0], {}, {}, false, false});
    lastChild_ = &active_->children.back();
    ++stats_.childrenAdded;
    return;
  }

  // setPosition / setRotation after addTemplate apply to the last child added,
  // not to the template itself. That is exactly how a vehicle's hierarchy is
  // assembled: hull -> turret -> barrel.
  if ((method == "setposition" || method == "setrotation") && lastChild_ != nullptr) {
    if (const auto vector = command.argVec3(0)) {
      if (method == "setposition") {
        lastChild_->position = *vector;
        lastChild_->hasPosition = true;
      } else {
        lastChild_->rotation = *vector;
        lastChild_->hasRotation = true;
      }
      return;
    }
  }

  applyProperty(active_->properties, method, command);
}

const ObjectTemplate* Registry::find(std::string_view name) const {
  const auto it = templates_.find(toLower(name));
  return it == templates_.end() ? nullptr : it->second;
}

std::vector<const ObjectTemplate*> Registry::all() const {
  std::vector<const ObjectTemplate*> out;
  out.reserve(order_.size());
  for (const auto& entry : order_) out.push_back(entry.get());
  return out;
}

}  // namespace obf2::game
