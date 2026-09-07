#include "obf2/meme/graph.h"

#include <cmath>

namespace obf2::meme {

void approachVariable(float& value, float target, float speed, float brakingDistance, float dt) {
  if (value == target) return;
  const float distance = std::fabs(target - value);
  float step = speed * dt;
  if (brakingDistance > 0.0f && distance < brakingDistance) {
    // Стала в бінарі саме 1.57075, а не повне pi/2 (0x100010c8).
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
  // Початкові значення змінних лежать у самому файлі: іменований
  // `FloatData`/`BoolData` — це і є змінна, а його «Value <do not edit>»
  // — те, з чого вона починає. Саме тому в даних видно -295 (сховане
  // положення лівої ділянки) і 503 (сховане правої).
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

  // Іменований вузол-дані — це змінна, і його значення беруть зі
  // сховища, а не з файлу: файл дає лише те, з чого вона почала.
  if (!object->name.empty()) return variables_.get(object->name);

  const std::string_view type = object->type();
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
    // Перемикач між двома значеннями — саме ним права ділянка вибирає
    // між висунутим і схованим положенням.
    return evaluate(sub("Toggle data")) != 0.0f ? evaluate(sub("Data 1")) : evaluate(sub("Data 2"));
  }
  if (type == "FloatRefData") {
    // Посилання на змінну без власного значення. Безіменне — нуль.
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

  // `Next node` — це не сусід, а **продовження ланцюжка**: кожен вузол
  // загортає наступний. Гілку дає окреме поле (`Split node`,
  // `Transformed node`).
  if (type == "CullNode") {
    // 0x10004a57: нульові дані — далі не йдемо зовсім.
    const Value* data = File::field(*object, "Data");
    if (data != nullptr && data->object >= 0 && evaluate(data->object) == 0.0f) return;
    walk(sub("Next node"), dt);
    return;
  }
  if (type == "CullVariableActionNode") {
    // 0x10004e99: є умова і вона нуль — дію не виконуємо. Ланцюжок при
    // цьому йде далі: подію розносить обхід, а не сам вузол.
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

void Graph::update(float dt) {
  actions_ = 0;
  walk(file_.root(), dt);
}

}  // namespace obf2::meme
