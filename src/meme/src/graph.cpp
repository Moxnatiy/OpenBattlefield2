#include "obf2/meme/graph.h"

#include <cmath>

namespace obf2::meme {

void approachVariable(float& value, float target, float speed, float brakingDistance, float dt) {
  if (value == target) return;
  const float distance = std::fabs(target - value);
  float step = speed * dt;
  if (brakingDistance > 0.0f && distance < brakingDistance) {
    // The constant in the binary is exactly 1.57075, not a full pi/2 (0x100010c8).
    constexpr float kQuarterTurn = 1.57075f;
    step *= std::cos(kQuarterTurn - distance / brakingDistance * kQuarterTurn);
  }
  if (value < target) {
    value = value + step > target ? target : value + step;
  } else {
    value = value - step < target ? target : value - step;
  }
}

float Variables::get(std::string_view name) const {
  const auto found = values_.find(name);
  return found == values_.end() ? 0.0f : found->second;
}

void Variables::set(std::string_view name, float value) {
  if (name.empty()) return;
  values_[std::string(name)] = value;
}

bool Variables::has(std::string_view name) const { return values_.find(name) != values_.end(); }

bool Graph::load(const std::vector<std::byte>& data, std::string* error) {
  if (!file_.load(data, error)) return false;
  seed();
  return true;
}

void Graph::seed() {
  // The variables' initial values lie in the file itself: a named
  // `FloatData`/`BoolData` is the variable, and its "Value <do not edit>" is
  // what it starts from. That is why the data shows -295 (the left region's
  // hidden position) and 503 (the right one's).
  for (const Object& object : file_.objects()) {
    if (object.name.empty()) continue;
    const Value* value = File::field(object, "Value <do not edit>");
    if (value == nullptr) continue;
    if (variables_.has(object.name)) continue;
    variables_.set(object.name, value->number);
  }
}

const Object* Graph::named(int index) const {
  const Object* object = file_.at(index);
  if (object == nullptr || object->name.empty()) return nullptr;
  return object;
}

float Graph::evaluate(int index) const {
  const Object* object = file_.at(index);
  if (object == nullptr) return 0.0f;

  const std::string_view type = object->type();

  // **Only a leaf** turns a name into a variable: `FloatData`, `BoolData` and
  // `FloatRefData`. It is those the engine binds to the HUD object's fields
  // (`BF2.exe`, 0x789480 registers fields under such names), and it is their
  // value that comes from the store rather than from the file.
  //
  // On a composite node the name is just a label: `ToggleData
  // "BottomRight/BottomRight_NextPos"` still has to be computed rather than
  // read from the store. Until now we read it — and the right region went to zero.
  const bool isLeaf = type == "FloatData" || type == "BoolData" || type == "FloatRefData";
  if (isLeaf && !object->name.empty()) return variables_.get(object->name);
  const auto sub = [&](const char* field) {
    const Value* value = File::field(*object, field);
    return value == nullptr ? -1 : value->object;
  };

  if (type == "FloatData" || type == "BoolData") {
    const Value* value = File::field(*object, "Value <do not edit>");
    return value == nullptr ? 0.0f : value->number;
  }
  if (type == "NotData") return evaluate(sub("Data")) == 0.0f ? 1.0f : 0.0f;
  if (type == "AndData") {
    return (evaluate(sub("Data 1")) != 0.0f && evaluate(sub("Data 2")) != 0.0f) ? 1.0f : 0.0f;
  }
  if (type == "OrData") {
    return (evaluate(sub("Data 1")) != 0.0f || evaluate(sub("Data 2")) != 0.0f) ? 1.0f : 0.0f;
  }
  if (type == "EqualData") {
    return evaluate(sub("Data 1")) == evaluate(sub("Data 2")) ? 1.0f : 0.0f;
  }
  if (type == "ToggleData") {
    // The switch between two values — the right region picks between its
    // extended and hidden position with exactly this.
    //
    // The order is exactly this, and it is the reverse of what one expects:
    // `ToggleData::value` (`MemeDll.dll`, 0x100032a6) returns **"Data 1" when
    // the switch is zero**, and "Data 2" when it is non-zero or when there is
    // no switch at all. We had it the other way round, and the right region
    // picked the wrong position and the wrong alpha.
    const int toggle = sub("Toggle data");
    const bool takeFirst = file_.at(toggle) != nullptr && evaluate(toggle) == 0.0f;
    return takeFirst ? evaluate(sub("Data 1")) : evaluate(sub("Data 2"));
  }
  if (type == "FloatRefData") {
    // A reference to a variable with no value of its own. Unnamed means zero.
    return 0.0f;
  }
  return 0.0f;
}

void Graph::run(int action, float dt) {
  const Object* object = file_.at(action);
  if (object == nullptr) return;
  const std::string_view type = object->type();
  const auto sub = [&](const char* field) {
    const Value* value = File::field(*object, field);
    return value == nullptr ? -1 : value->object;
  };
  const auto number = [&](const char* field) {
    const Value* value = File::field(*object, field);
    return value == nullptr ? 0.0f : value->number;
  };

  if (type == "ActionListAction") {
    const Value* list = File::field(*object, "Action list");
    if (list == nullptr) return;
    for (const int item : list->list) run(item, dt);
    return;
  }

  const Object* target = named(sub("Variable"));
  if (target == nullptr) return;
  const float wanted = evaluate(sub("Data"));

  if (type == "SetVariableAction") {
    variables_.set(target->name, wanted);
    ++actions_;
    return;
  }
  if (type == "SetVariableSoftAction" || type == "SetVariableSineAction") {
    float value = variables_.get(target->name);
    const float braking = type == "SetVariableSineAction" ? number("Braking distance") : 0.0f;
    approachVariable(value, wanted, number("Speed"), braking, dt);
    variables_.set(target->name, value);
    ++actions_;
    return;
  }
}

void Graph::walk(int index, float dt) {
  const Object* object = file_.at(index);
  if (object == nullptr) return;
  const std::string_view type = object->type();
  const auto sub = [&](const char* field) {
    const Value* value = File::field(*object, field);
    return value == nullptr ? -1 : value->object;
  };

  // `Next node` is not a sibling but the **continuation of the chain**: every
  // node wraps the next one. A branch comes from a separate field (`Split node`,
  // `Transformed node`).
  if (type == "CullNode") {
    // 0x10004a57: zero data — we do not go on at all.
    const Value* data = File::field(*object, "Data");
    if (data != nullptr && data->object >= 0 && evaluate(data->object) == 0.0f) return;
    walk(sub("Next node"), dt);
    return;
  }
  if (type == "CullVariableActionNode") {
    // 0x10004e99: there is a condition and it is zero — the action is not run.
    // The chain still continues: the walk carries the event, not the node itself.
    const int condition = sub("Variable");
    if (condition < 0 || evaluate(condition) != 0.0f) run(sub("Action"), dt);
    walk(sub("Next node"), dt);
    return;
  }
  if (type == "ActionNode") {
    run(sub("Action"), dt);
    walk(sub("Next node"), dt);
    return;
  }

  walk(sub("Next node"), dt);
  walk(sub("Split node"), dt);
  walk(sub("Transformed node"), dt);
}

void Graph::collectLayers(int index, std::vector<Layer>& out) const {
  const Object* object = file_.at(index);
  if (object == nullptr) return;
  const auto sub = [&](const char* field) {
    const Value* value = File::field(*object, field);
    return value == nullptr ? -1 : value->object;
  };
  const auto number = [&](const Object& owner, const char* field) {
    const Value* value = File::field(owner, field);
    return value == nullptr ? 0.0f : value->number;
  };

  if (object->type() == "BfTransformNode") {
    Layer layer;
    layer.x = evaluate(sub("X"));
    layer.y = evaluate(sub("Y"));
    layer.width = number(*object, "Width");
    layer.height = number(*object, "Height");
    if (const Object* bound = file_.at(sub("X")); bound != nullptr) layer.variable = bound->name;
    if (const Object* twin = file_.at(sub("Next node"));
        twin != nullptr && twin->type() == "TransformNode") {
      layer.hasTwin = true;
      layer.twinX = number(*twin, "X");
      layer.twinY = number(*twin, "Y");
      layer.twinWidth = number(*twin, "Width");
      layer.twinHeight = number(*twin, "Height");
    }
    out.push_back(std::move(layer));
  }

  collectLayers(sub("Next node"), out);
  collectLayers(sub("Split node"), out);
  collectLayers(sub("Transformed node"), out);
}

std::vector<Graph::Layer> Graph::layers() const {
  std::vector<Layer> out;
  collectLayers(file_.root(), out);
  return out;
}

void Graph::update(float dt) {
  actions_ = 0;
  walk(file_.root(), dt);
}

}  // namespace obf2::meme
