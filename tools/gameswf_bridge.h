// A stub of the "menu -> game" bridge for the gameswf trial.
//
// The BF2 menu talks to the engine through seventeen named objects
// (`Logic`, `Multiplay`, `Options`…), and gameswf itself has none: they are
// what DICE added in `SwiffPlayer.cpp`. The bridge is taken apart in
// docs/functions/menu-bridge.md.
//
// This is not an implementation but **reconnaissance**. Every object answers
// any name: it gives back a stub function and records what was asked of it.
// That way the movie stops falling over on "can't find …", and we get the
// "object -> method" list **from the movie itself** rather than from a guess
// about which strings sit next to each other in `.rdata`.
//
// The values returned are zero, so the movie behaves as if the game knew
// nothing: no servers, no profile, no battle running. That is deliberate:
// first we need to see where it goes given those answers.
#ifndef OPENBF2_GAMESWF_BRIDGE_H
#define OPENBF2_GAMESWF_BRIDGE_H

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "gameswf/gameswf_action.h"
#include "gameswf/gameswf_object.h"
#include "gameswf/gameswf_player.h"

namespace openbf2 {

// What the movie asked of whom: "object.method" -> how many times.
inline std::map<std::string, int>& bridgeCalls() {
  static std::map<std::string, int> calls;
  return calls;
}

// The name of the object whose method is being called. gameswf does not pass
// it in `fn_call`, so we remember it when the method is handed out.
inline std::string& bridgePending() {
  static std::string pending;
  return pending;
}

inline void bridgeStub(const gameswf::fn_call& fn) {
  // We return **`false`, not `undefined`**. In ActionScript 2 `undefined`
  // behaves treacherously in comparisons and arithmetic, and the movie easily
  // goes into a loop on it; `false` gives it an honest "no": no servers, no
  // download running, no battle started.
  if (fn.result != NULL) fn.result->set_bool(false);
}

// A bridge object: it knows its own name and answers any request.
struct BridgeObject : public gameswf::as_object {
  std::string name;

  BridgeObject(gameswf::player* owner, const char* objectName)
      : gameswf::as_object(owner), name(objectName) {}

  bool get_member(const tu_stringi& member, gameswf::as_value* value) override {
    // First what is already there (proto and the like) — otherwise we break
    // the object's ordinary behaviour.
    if (gameswf::as_object::get_member(member, value)) return true;
    // The service names must **not** be answered. Give a stub function for
    // `__proto__` or `valueOf` and gameswf's value resolution goes round in a
    // circle and eats the stack: it asks for the prototype, gets a function,
    // asks for its prototype, and so on without end.
    if (isInternal(member.c_str())) return false;
    const std::string key = name + "." + member.c_str();
    // We print at once, not at the end: a large movie does not play through
    // to the end yet, and the gathered list would otherwise vanish with it.
    if (++bridgeCalls()[key] == 1) std::fprintf(stderr, "[bridge] %s\n", key.c_str());
    if (value != NULL) *value = gameswf::as_value(bridgeStub);
    return true;
  }

  // The movie has **substitute** bridge methods of its own: in
  // `mainMenu.swf` at offset ~3179 of an action buffer is a block defining
  // `getDownloadingUrl`, `getDownloadingProgress`, `isDownloadingDemo`,
  // `getNumDemoBookmarks` and the rest and puts them on the object.
  //
  // Forbidding that write (so that the native method would win) **did not
  // help**: the movie still loops. So the write stays an ordinary one — we do
  // not keep behaviour in the code with nothing behind it.

  static bool isInternal(const char* member) {
    if (member == NULL) return true;
    // Only the double underscore: `__proto__`, `__resolve`. The single one
    // must not be touched — `_visible`, `_x`, `_alpha` are perfectly good
    // names, and without them the movie stops before it reaches the bridge.
    if (member[0] == '_' && member[1] == '_') return true;
    static const char* const reserved[] = {
        "prototype", "constructor", "valueOf", "toString", "onLoad",
        "onEnterFrame", "onUnload", "addProperty", "hasOwnProperty",
    };
    for (const char* name : reserved) {
      if (std::strcmp(member, name) == 0) return true;
    }
    return false;
  }
};

// The seventeen names — from `SwiffPlayer.dll`, 0xd7a74..0xd7b18
// (docs/functions/menu-bridge.md).
inline const char* const* bridgeObjectNames(int* count) {
  static const char* const names[] = {
      "ControlSettings", "EndOfRound", "Player", "Mod", "Cursor", "Sound",
      "Client", "Options", "Profile", "Clans", "Multiplay", "Singleplay",
      "Render", "Logic", "Locale", "MessageHandler", "General",
  };
  *count = int(sizeof(names) / sizeof(names[0]));
  return names;
}

// We put the objects in both as `_global.<Name>` and as `_global.dice.bf2.<Name>`:
// both forms occur in the movies (`Logic.quit` in `menu.swf`,
// `dice.bf2.Logic.disconnectGame` in `mainMenu.swf`).
inline void installBridge(gameswf::player* owner) {
  gameswf::as_object* global = owner->get_global();
  if (global == NULL) return;

  // We take the `dice` **that is already there**: in `mainMenu.swf` it is the
  // movie's own package (`dice.UIs.*` — its component library), and creating
  // ours would mean shadowing it.
  gameswf::as_value existing;
  gameswf::as_object* dice = NULL;
  if (global->get_member("dice", &existing)) dice = existing.to_object();
  if (dice == NULL) {
    dice = new gameswf::as_object(owner);
    global->set_member("dice", dice);
  }
  gameswf::as_object* bf2 = new gameswf::as_object(owner);
  dice->set_member("bf2", bf2);

  int count = 0;
  const char* const* names = bridgeObjectNames(&count);
  for (int i = 0; i < count; ++i) {
    BridgeObject* object = new BridgeObject(owner, names[i]);
    global->set_member(names[i], object);
    bf2->set_member(names[i], object);
  }
}

}  // namespace openbf2

#endif  // OPENBF2_GAMESWF_BRIDGE_H
