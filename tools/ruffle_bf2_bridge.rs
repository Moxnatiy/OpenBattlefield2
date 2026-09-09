//! A stub of the "menu -> game" bridge for Ruffle.
//!
//! It goes into `core/src/avm1/globals/bf2_bridge.rs` of the Ruffle tree;
//! how it is wired up is described in `tools/ruffle_bf2_bridge.patch`.
//!
//! The Battlefield 2 menu is Flash, and it talks to the engine through
//! seventeen named objects (`Logic`, `Multiplay`, `Options`…).
//! The bridge is taken apart in `docs/functions/menu-bridge.md`. They are
//! not in the Flash itself: they are what DICE added in `SwiffPlayer.cpp`,
//! and without them the movie stands on its first frame.
//!
//! This is not an implementation but reconnaissance. Every object answers
//! **any** name through `__resolve` — the ActionScript 2 built-in hook for
//! "property not found". A stub function is returned, it gives `false`,
//! and the name is printed into the log. That way the "object -> method"
//! list comes from the movie itself rather than from a guess.

use crate::avm1::activation::Activation;
use crate::avm1::error::Error;
use crate::avm1::function::FunctionObject;
use crate::avm1::property::Attribute;
use crate::avm1::property_decl::DeclContext;
use crate::avm1::{Object, Value};
use crate::string::AvmString;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::OnceLock;

/// The seventeen names — from `SwiffPlayer.dll`, 0xd7a74..0xd7b18.
pub const OBJECTS: [&str; 17] = [
    "ControlSettings",
    "EndOfRound",
    "Player",
    "Mod",
    "Cursor",
    "Sound",
    "Client",
    "Options",
    "Profile",
    "Clans",
    "Multiplay",
    "Singleplay",
    "Render",
    "Logic",
    "Locale",
    "MessageHandler",
    "General",
];

/// Settings objects the movie also expects from the host.
///
/// They are not in the seventeen registered in `SwiffPlayer.dll`, yet
/// `mainMenu.swf` carries a fallback class for each of them
/// (`__Packages.dice.bf2.*`) guarded by `if (_global.dice.bf2.X) skip` —
/// the same pattern as `dice.General` and `mx.lang.Locale`, which means
/// the host is expected to provide them. Without them the movie keeps
/// its own stubs and settings go nowhere: `GeneralSettings.setUseBots`
/// is what decides whether the map list is restricted to levels with AI.
pub const SETTINGS_OBJECTS: [&str; 6] = [
    "GeneralSettings",
    "ServerSettings",
    "GlobalSettings",
    "VideoSettings",
    "AudioSettings",
    "HapticSettings",
];

/// Where the game lives and where the player's documents live.
///
/// The game keeps profiles not beside itself but in `Documents/Battlefield 2`
/// (on Windows, "My Documents"). We take the same files from there:
/// `Profiles/Global.con` says which profile is the default, and
/// `Profiles/<number>/Profile.con` gives its name.
fn game_dir() -> PathBuf {
    std::env::var("BF2_GAME_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("Game Files"))
}

fn docs_dir() -> PathBuf {
    std::env::var("BF2_DOCS_DIR").map(PathBuf::from).unwrap_or_else(|_| {
        let home = std::env::var("HOME").unwrap_or_default();
        PathBuf::from(home).join("Documents").join("Battlefield 2")
    })
}

/// `.utxt` is UTF-16LE with a BOM; a line looks like
/// `KEY<spaces>\\x1B\\x1B value \\x1B\\x1B`.
/// The layout is already taken apart in `src/loc/include/obf2/loc/lexicon.h`.
fn parse_utxt(bytes: &[u8], out: &mut HashMap<String, String>) {
    let start = if bytes.starts_with(&[0xFF, 0xFE]) { 2 } else { 0 };
    let units: Vec<u16> = bytes[start..]
        .chunks_exact(2)
        .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
        .collect();
    let text = String::from_utf16_lossy(&units);
    for line in text.split('\n') {
        let line = line.trim_end_matches('\r');
        let Some(at) = line.find('\x1B') else { continue };
        let key = line[..at].trim();
        if key.is_empty() {
            continue;
        }
        let value = line[at..].trim_matches('\x1B').trim();
        out.insert(key.to_string(), value.to_string());
    }
}

/// The English localisation dictionary — read once.
fn lexicon() -> &'static HashMap<String, String> {
    static ONCE: OnceLock<HashMap<String, String>> = OnceLock::new();
    ONCE.get_or_init(|| {
        let mut map = HashMap::new();
        let dir = game_dir().join("mods/bf2/Localization/English");
        let mut files: Vec<PathBuf> = std::fs::read_dir(&dir)
            .map(|entries| {
                entries
                    .flatten()
                    .map(|e| e.path())
                    .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("utxt")))
                    .collect()
            })
            .unwrap_or_default();
        // The game's order: the main file, then patches and add-ons on top.
        files.sort();
        for file in &files {
            if let Ok(bytes) = std::fs::read(file) {
                parse_utxt(&bytes, &mut map);
            }
        }
        tracing::info!(target: "bf2_bridge", "localisation: {} lines from {} files", map.len(), files.len());
        map
    })
}

/// The current player profile from `Documents/Battlefield 2/Profiles`.
struct Profile {
    /// The folder number. Not used yet: the `PREFIX` caption takes not it
    /// but `GlobalSettings.setNamePrefix` (see `name_prefix`).
    #[allow(dead_code)]
    id: String,
    name: String,
    count: usize,
}

/// One row of the `localProfiles` list.
pub struct ProfileEntry {
    /// The profile folder number — the same thing `Global.con` names it by
    /// (`GlobalSettings.setDefaultUser "0001"`).
    id: String,
    /// The local name, `LocalProfile.setNick`.
    name: String,
    /// The network name, `LocalProfile.setGamespyNick`. Empty means a
    /// non-network account; by that the original picks the icon and the text
    /// (`SwiffPlayer.dll`, 0x10026cb0..0x10026e40).
    gamespy_nick: String,
    /// The `password` field (profile +0x8c). It is not in `Profile.con` —
    /// the game keeps the account password elsewhere, and where exactly we
    /// have not established. So an empty string for now, not a made-up value.
    password: String,
}

/// Every profile in `Documents/Battlefield 2/Profiles` — numbered folders.
///
/// Read once: profiles do not appear while the menu runs (the menu itself
/// creates them, and after creating one it re-reads the list itself).
fn profiles() -> &'static Vec<ProfileEntry> {
    static ONCE: OnceLock<Vec<ProfileEntry>> = OnceLock::new();
    ONCE.get_or_init(|| {
        let root = docs_dir().join("Profiles");
        let mut ids: Vec<String> = std::fs::read_dir(&root)
            .map(|entries| {
                entries
                    .flatten()
                    .map(|e| e.file_name().to_string_lossy().to_string())
                    .filter(|n| !n.is_empty() && n.chars().all(|c| c.is_ascii_digit()))
                    .collect()
            })
            .unwrap_or_default();
        ids.sort();
        ids.into_iter()
            .map(|id| {
                let text = std::fs::read_to_string(root.join(&id).join("Profile.con"))
                    .unwrap_or_default();
                let pick = |what: &str| {
                    text.lines()
                        .find_map(|line| line.split_once(what))
                        .map(|(_, rest)| rest.trim().trim_matches('"').to_string())
                };
                let name = pick("setNick")
                    .filter(|n| !n.is_empty())
                    .or_else(|| pick("setName"))
                    .unwrap_or_default();
                let gamespy_nick = pick("setGamespyNick").unwrap_or_default();
                ProfileEntry { id, name, gamespy_nick, password: String::new() }
            })
            .collect()
    })
}

/// The current profile: the one named in `Global.con`, or the first.
///
/// Reading from disk lives in one place — `profiles()`; here only the choice.
fn profile() -> &'static Profile {
    static ONCE: OnceLock<Profile> = OnceLock::new();
    ONCE.get_or_init(|| {
        // `Global.con` names the default: GlobalSettings.setDefaultUser "0001".
        let global = std::fs::read_to_string(docs_dir().join("Profiles").join("Global.con"))
            .unwrap_or_default();
        let default_user = global
            .lines()
            .find_map(|line| line.split_once("setDefaultUser"))
            .map(|(_, rest)| rest.trim().trim_matches('"').to_string())
            .unwrap_or_default();

        let all = profiles();
        let chosen = all
            .iter()
            .find(|entry| entry.id == default_user)
            .or_else(|| all.first());
        let (id, name) = match chosen {
            Some(entry) => (entry.id.clone(), entry.name.clone()),
            None => (String::new(), String::new()),
        };

        tracing::info!(target: "bf2_bridge", "profile: {} ({}), {} in total", name, id, all.len());
        Profile { id, name, count: all.len() }
    })
}

/// What `__resolve` gives back: a method that does nothing.
///
/// We return `false`, not `undefined`: in ActionScript 2 `undefined` is
/// treacherous in comparisons and arithmetic, while `false` gives the movie
/// an honest "no" — no servers, no profile, no battle running.
fn stub<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(false.into())
}

/// `Locale.getString(key)` -> a line from `Localization/English/*.utxt`.
///
/// With no such key we give the key back: that is how a missing string
/// shows in the game, and it is useful — what is missing is visible at once.
fn locale_get_string<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let key = args
        .first()
        .unwrap_or(&Value::Undefined)
        .coerce_to_string(activation)?
        .to_string();
    let found = lexicon().get(&key).cloned();
    tracing::debug!(target: "bf2_bridge", "getString({}) -> {:?}", key, found);
    let text = found.unwrap_or(key);
    Ok(AvmString::new_utf8(activation.gc(), text).into())
}

// --- `mx.lang.Locale`: deferred captions ----------------------------
//
// The menu movie carries a **fallback** implementation of `mx.lang.Locale`
// (`__Packages.mx.lang.Locale`, the DoInitAction block of symbol 179), and
// it begins with `if (_global.mx.lang.Locale) { do nothing }`. That is, in
// the game the class is given by the player itself: `SwiffPlayer.dll` has an
// object with exactly the stock set of methods — `addDelayedInstance`,
// `checkXMLStatus`, `getLanguage`, `initialize`, `loadString`,
// `setDefaultLang`, `setFlaName` (0xd7af4, docs/functions/menu-bridge.md).
//
// The key word here is **delayed**. The `TextContainer` component is built
// like this (symbol 117, constructor 0x945):
//
//     init(); draw(); createChildren(); arrange(); setEvents();
//
// The last line of `createChildren()` calls `setLocID(m_localizationId)`,
// and that one calls `mx.lang.Locale.addDelayedInstance(field, key)`. The
// very next `arrange()` writes **the key itself** into that same field:
//
//     if (_parent._parent.m_localizationId.length > 0)
//         text = _parent._parent.m_localizationId;
//
// The stock components are written on the assumption that
// `addDelayedInstance` only **remembers** the field and the text is handed
// out later by `assignDelayedInstances`. While we set the text at once,
// `arrange()` wiped it and the keys stayed on screen. So the queue here is a
// real one, flushed after the frame (`Player::run_frame`, see the patch).

/// The queue of deferred captions on the `Locale` object itself.
///
/// We keep it as a plain object — "`o<number>` -> field, `i<number>` -> key"
/// — with a counter `n`. The stock `instanceObjects`/`instanceIds` are
/// arrays, but all we need out of them is the pair "to whom" and "what".
fn queue<'gc>(
    activation: &mut Activation<'_, 'gc>,
    this: Object<'gc>,
) -> Result<Object<'gc>, Error<'gc>> {
    // `get_stored`, not `get`: an ordinary read of a missing property would
    // fall into our own `__resolve`, and that would give back a stub
    // function — which we would take for the queue.
    let key = AvmString::new_utf8(activation.gc(), "__bf2delayed");
    if let Value::Object(existing) = this.get_stored(key, activation)? {
        return Ok(existing);
    }
    let proto = activation.prototypes().object;
    let object = Object::new(activation.strings(), Some(proto));
    let counter = AvmString::new_utf8(activation.gc(), "n");
    object.set(counter, Value::from(0), activation)?;
    this.define_value(
        activation.gc(),
        key,
        object.into(),
        Attribute::DONT_ENUM | Attribute::DONT_DELETE,
    );
    Ok(object)
}

/// `mx.lang.Locale.addDelayedInstance(field, key)` — remember, do not set.
fn locale_add_delayed<'gc>(
    activation: &mut Activation<'_, 'gc>,
    this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let Some(Value::Object(instance)) = args.first().cloned() else {
        return Ok(Value::Undefined);
    };
    let id = args.get(1).unwrap_or(&Value::Undefined).coerce_to_string(activation)?;
    if id.is_empty() {
        return Ok(Value::Undefined);
    }

    let queue = queue(activation, this)?;
    let counter = AvmString::new_utf8(activation.gc(), "n");
    let index = queue.get(counter, activation)?.coerce_to_f64(activation)? as i32;
    let slot_object = AvmString::new_utf8(activation.gc(), format!("o{index}"));
    let slot_id = AvmString::new_utf8(activation.gc(), format!("i{index}"));
    queue.set(slot_object, Value::from(instance), activation)?;
    queue.set(slot_id, Value::from(id), activation)?;
    queue.set(counter, Value::from(index + 1), activation)?;
    tracing::debug!(target: "bf2_bridge", "caption deferred {} ({})", id, index);
    Ok(Value::Undefined)
}

/// `mx.lang.Locale.assignDelayedInstances()` — hand out translated captions.
fn locale_assign_delayed<'gc>(
    activation: &mut Activation<'_, 'gc>,
    this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let queue = queue(activation, this)?;
    let counter = AvmString::new_utf8(activation.gc(), "n");
    let count = queue.get(counter, activation)?.coerce_to_f64(activation)? as i32;
    if count == 0 {
        return Ok(Value::Undefined);
    }

    let text_key = AvmString::new_utf8(activation.gc(), "text");
    for index in 0..count {
        let slot_object = AvmString::new_utf8(activation.gc(), format!("o{index}"));
        let slot_id = AvmString::new_utf8(activation.gc(), format!("i{index}"));
        let Value::Object(instance) = queue.get(slot_object, activation)? else { continue };
        let id = queue.get(slot_id, activation)?.coerce_to_string(activation)?.to_string();
        let text = lexicon().get(&id).cloned().unwrap_or(id);
        let value = AvmString::new_utf8(activation.gc(), text);
        instance.set(text_key, Value::from(value), activation)?;
        queue.delete(activation, slot_object);
        queue.delete(activation, slot_id);
    }
    queue.set(counter, Value::from(0), activation)?;
    tracing::debug!(target: "bf2_bridge", "captions handed out: {}", count);
    Ok(Value::Undefined)
}

/// Flush the caption queue after the frame.
///
/// Called from `Player::run_frame` right after `Avm1::run_frame`: by then the
/// components are built, that is `arrange()` has already written its key into
/// the field, and the translation lands on top — in exactly the order the
/// stock components assume.
pub fn flush_delayed_strings(context: &mut crate::context::UpdateContext<'_>) {
    let Some(mut activation) = Activation::try_from_stub(
        context,
        crate::avm1::activation::ActivationIdentifier::root("[bf2 locale]"),
    ) else {
        return;
    };
    let mut object = activation.global_object();
    for step in ["mx", "lang", "Locale"] {
        let key = AvmString::new_utf8(activation.gc(), step);
        match object.get(key, &mut activation) {
            Ok(Value::Object(found)) => object = found,
            _ => return,
        }
    }
    let locale = object;
    if let Err(error) = locale_assign_delayed(&mut activation, locale, &[]) {
        tracing::warn!(target: "bf2_bridge", "captions not handed out: {:?}", error);
    }
}

// --- the menu string storage -----------------------------------------
//
// `Logic.setStorageString` / `getStorageString` / `eraseStorageString`
// (`SwiffPlayer.dll`, 0x1004d0e0, 0x1004db60, 0x1004d170). The wrappers take
// their arguments from the array **in reverse order** — `param_3` there is
// `argc-1`, and the code reads `args[argc-1]` and `args[argc-2]` — and call
// the engine object: put (vtable +0x23c), take with a default value
// (+0x238), erase (+0x240).
//
// This is not settings but memory between menu pages: the movie puts
// `lastFrameLabel`, `currentFrameName`, `BGmovie`, `noobieSP` there. None of
// those names is in `BF2.exe`, in the game's files or in the player profile,
// so the storage lives only in the session's memory, and we keep it the same.
fn storage() -> &'static std::sync::Mutex<HashMap<String, String>> {
    static ONCE: OnceLock<std::sync::Mutex<HashMap<String, String>>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(HashMap::new()))
}

fn storage_set<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let key = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let value = args.get(1).unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    tracing::debug!(target: "bf2_bridge", "storage: {} = {}", key, value);
    storage().lock().unwrap().insert(key, value);
    Ok(Value::Undefined)
}

fn storage_get<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let key = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let stored = storage().lock().unwrap().get(&key).cloned();
    let text = match stored {
        Some(found) => found,
        // The default value is the second argument. The movie does not always
        // supply it; then we give an empty string back, not `undefined`: the
        // string goes on into comparisons and into paths to images.
        None => match args.get(1) {
            Some(value) => value.coerce_to_string(activation)?.to_string(),
            None => String::new(),
        },
    };
    Ok(AvmString::new_utf8(activation.gc(), text).into())
}

fn storage_erase<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let key = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    storage().lock().unwrap().remove(&key);
    Ok(Value::Undefined)
}

// --- menu lists: `General` -------------------------------------------
//
// Lists in the menu are filled not by "give me entry number N" but by a
// triple on the `General` object (`SwiffPlayer.dll`, registration 0x100316d0):
// `getListRevision(name)` says whether the list changed, `getListEntries(name)`
// gives the rows back, `flushList(name)` drops the cache. Taken apart in
// docs/functions/menu-bridge.md.

/// `General.getListRevision(name)` (0x10019d80).
///
/// The original knows only three names — `serverList`, `serverListLAN`,
/// `favouriteServers` — and gives **−1** for the rest. We have no server
/// lists yet, so −1 is what we always give: for the movie that means "the
/// list is not versioned", and it re-reads it every time.
fn list_revision<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let name = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?;
    tracing::debug!(target: "bf2_bridge", "getListRevision({})", name);
    Ok((-1.0f64).into())
}

/// `General.flushList(name)` (0x10019ba0) — the same −1 for non-server ones.
fn list_flush<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let name = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?;
    tracing::debug!(target: "bf2_bridge", "flushList({})", name);
    Ok((-1.0f64).into())
}

/// Rows of the `installedMaps` list: one per `<maptype>` that matches
/// the current game-mode filter.
///
/// Field names come from what the movie reads off the row object:
/// `selectMap(mapObject)` on the singleplayer page uses `id`, `path` and
/// `mapSize` (DoAction of the page, 0x22c..0x2f9).
fn installed_maps<'gc>(activation: &mut Activation<'_, 'gc>) -> Result<Value<'gc>, Error<'gc>> {
    let wanted = game_mode().lock().unwrap().clone();
    let proto = activation.prototypes().object;

    // With bots on only AI-capable entries are listed — that is what
    // makes the singleplayer list one row per map instead of one per size.
    let bots = *use_bots().lock().unwrap();
    let mut rows = Vec::new();
    for map in maps() {
        for mode in map.modes.iter().filter(|mode| mode.mode == wanted && (!bots || mode.ai)) {
            let meta = Object::new(activation.strings(), Some(proto));
            for (field, text) in [("id", &map.id), ("path", &map.path)] {
                let key = AvmString::new_utf8(activation.gc(), field);
                let value = AvmString::new_utf8(activation.gc(), text.clone());
                meta.set(key, Value::from(value), activation)?;
            }
            let key = AvmString::new_utf8(activation.gc(), "mapSize");
            meta.set(key, Value::from(mode.size as f64), activation)?;
            let key = AvmString::new_utf8(activation.gc(), "disabled");
            meta.set(key, Value::from(false), activation)?;

            let shown = AvmString::new_utf8(activation.gc(), map.name.clone());
            rows.push(Value::from(
                crate::avm1::ArrayBuilder::new(activation)
                    .with([Value::from(meta), Value::from(shown)]),
            ));
        }
    }

    let total = rows.len();
    let list_object = Object::new(activation.strings(), Some(proto));
    let key = AvmString::new_utf8(activation.gc(), "id");
    let value = AvmString::new_utf8(activation.gc(), "installedMaps");
    list_object.set(key, Value::from(value), activation)?;
    let header = crate::avm1::ArrayBuilder::new(activation).with([Value::from(list_object)]);
    let key = AvmString::new_utf8(activation.gc(), "totalCount");
    header.set(key, Value::from(total as f64), activation)?;

    let mut all = vec![Value::from(header)];
    all.extend(rows);
    tracing::debug!(target: "bf2_bridge", "installedMaps ({}): {} rows", wanted, total);
    Ok(Value::from(crate::avm1::ArrayBuilder::new(activation).with(all)))
}

/// `General.getListEntries(name)` (0x10022610).
///
/// So far we can do one list — `localProfiles` (branch 0x10026baa). The row
/// fields: `profileName` (profile field +0x1c, written at 0x10026cf8) and
/// `password` (+0x8c, 0x10026d28). The `gamespy` field and the account icon
/// the original sets from the comparison at 0x10026cd0, whose direction is
/// not established yet, so we do not give them back (rule 6).
fn list_entries<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let name = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    if name == "installedMaps" {
        return installed_maps(activation);
    }
    if name != "localProfiles" {
        tracing::debug!(target: "bf2_bridge", "the list {} is not made yet", name);
        return Ok(Value::from(crate::avm1::ArrayBuilder::empty(activation)));
    }

    // A row is an **array**, not an object: `ColumnList.fillList` reads
    // `listArray[i][0]` as the entry object (`id` and `disabled` come from
    // there) and `listArray[i][1..]` as the column values (symbol 42, 0x3a24).
    // For `localProfiles` there are two columns: "Online" and "Playername"
    // (instance parameters, `PlaceObject2`: `columnTexts`).
    let proto = activation.prototypes().object;
    let total = profiles().len();
    let mut rows = Vec::new();
    for entry in profiles() {
        let meta = Object::new(activation.strings(), Some(proto));
        let online = !entry.gamespy_nick.is_empty();
        for (field, text) in [
            ("profileName", entry.name.clone()),
            ("password", entry.password.clone()),
            ("id", entry.id.clone()),
        ] {
            let key = AvmString::new_utf8(activation.gc(), field);
            let value = AvmString::new_utf8(activation.gc(), text);
            meta.set(key, Value::from(value), activation)?;
        }
        for (field, flag) in [("gamespy", online), ("disabled", false)] {
            let key = AvmString::new_utf8(activation.gc(), field);
            meta.set(key, Value::from(flag), activation)?;
        }

        // The "Online" column is not text but the account icon
        // (0x10026d6a / 0x10026d8e), and "Playername" is the network name
        // when there is one, otherwise the local one (0x10026e20).
        // The key goes into the column **already translated**: the original
        // runs it through the engine's lexicon before putting it into the row
        // (`FUN_10013600`, call 0x10026db3 — a non-empty string goes into the
        // engine method +0xa8 and comes back as a wide string).
        let icon = if online { "ACCOUNT_online" } else { "ACCOUNT_offline" };
        let icon = lexicon().get(icon).cloned().unwrap_or_else(|| icon.to_string());
        let shown = if online { entry.gamespy_nick.clone() } else { entry.name.clone() };
        let row = crate::avm1::ArrayBuilder::new(activation).with([
            Value::from(meta),
            Value::from(AvmString::new_utf8(activation.gc(), icon.clone())),
            Value::from(AvmString::new_utf8(activation.gc(), shown)),
        ]);
        rows.push(Value::from(row));
    }

    // The first entry is **not a row but a header**: `fillList` takes rows
    // from index 1 (`listArray[reg4 - reg5]`, where the counter starts at
    // `m_listStartIndex + 1`, symbol 42, 0x3d40), and puts `listArray[0][0]`
    // into every row as "the list object". The header itself carries
    // `totalCount` — how many rows there are in all, not only in this batch
    // (`dice.lists.List.flush`, symbol 5, 0x63a).
    let list_object = Object::new(activation.strings(), Some(proto));
    let key = AvmString::new_utf8(activation.gc(), "id");
    let value = AvmString::new_utf8(activation.gc(), "localProfiles");
    list_object.set(key, Value::from(value), activation)?;
    let header = crate::avm1::ArrayBuilder::new(activation).with([Value::from(list_object)]);
    let key = AvmString::new_utf8(activation.gc(), "totalCount");
    header.set(key, Value::from(total as f64), activation)?;

    let mut all = vec![Value::from(header)];
    all.extend(rows);
    tracing::debug!(target: "bf2_bridge", "localProfiles: {} rows", total);
    Ok(Value::from(crate::avm1::ArrayBuilder::new(activation).with(all)))
}

// --- map catalogue ------------------------------------------------------
//
// Everything the menu shows about a level comes from its own
// `Levels/<dir>/Info/<dir>.desc`, an XML file:
//
//     <map gsid="101">
//       <name> Dalian Plant </name>
//       <briefing locid="LOADINGSCREEN_MAPDESCRIPTION_dalianplant">…</briefing>
//       <modes>
//         <mode type="gpm_cq">
//           <maptype players="16" type="doubleassault" locid="…">…</maptype>
//
// `gsid` is the map id the menu passes around, the directory name is the
// path (`maplist.append "dalian_plant" "gpm_cq" 16`), and every
// `<maptype>` is one row of the `installedMaps` list — the list is
// filtered by game mode, see `Multiplay.setFilterGameMode`.

/// One playable combination: game mode plus map size.
pub struct MapMode {
    mode: String,
    size: u32,
    /// Layout name (`doubleassault`, `headon`) — kept for completeness,
    /// not shown by the singleplayer screen yet.
    #[allow(dead_code)]
    kind: String,
    /// Localisation key of the mode description on this map
    /// (`GAMEMODE_DESCRIPTION_<layout>`). Not used yet: the selected-map
    /// panel shows the level briefing, and which of the two the original
    /// puts there is not established.
    #[allow(dead_code)]
    locid: String,
    /// `ai="1"` — the level ships bot navigation for this entry.
    ai: bool,
}

pub struct MapInfo {
    id: String,
    path: String,
    name: String,
    /// Localisation key of the briefing text.
    briefing: String,
    modes: Vec<MapMode>,
}

/// Value of the first `<tag> … </tag>` pair, trimmed.
fn tag_value(xml: &str, tag: &str) -> String {
    let open = format!("<{tag}");
    let Some(from) = xml.find(&open) else { return String::new() };
    let rest = &xml[from..];
    let Some(head) = rest.find('>') else { return String::new() };
    let close = format!("</{tag}>");
    let Some(to) = rest.find(&close) else { return String::new() };
    if to < head {
        return String::new();
    }
    rest[head + 1..to].trim().to_string()
}

/// Value of `name="…"` inside the tag opened at `at`.
fn attribute(chunk: &str, name: &str) -> String {
    let key = format!("{name}=\"");
    let Some(from) = chunk.find(&key) else { return String::new() };
    let rest = &chunk[from + key.len()..];
    let Some(to) = rest.find('"') else { return String::new() };
    rest[..to].to_string()
}

fn parse_desc(xml: &str, directory: &str) -> MapInfo {
    let head = xml.find('>').map(|end| &xml[..end]).unwrap_or("");
    let name = tag_value(xml, "name");
    let briefing = xml
        .find("<briefing")
        .map(|at| attribute(&xml[at..], "locid"))
        .unwrap_or_default();

    // Walk `<mode type="…">` blocks and the `<maptype>` entries in each.
    let mut modes = Vec::new();
    let mut cursor = 0;
    while let Some(at) = xml[cursor..].find("<mode ") {
        let start = cursor + at;
        let mode = attribute(&xml[start..], "type");
        let end = xml[start..].find("</mode>").map(|e| start + e).unwrap_or(xml.len());
        let mut inner = start;
        while let Some(rel) = xml[inner..end].find("<maptype") {
            let tag_start = inner + rel;
            let tag_end = xml[tag_start..end].find('>').map(|e| tag_start + e).unwrap_or(end);
            let chunk = &xml[tag_start..tag_end];
            modes.push(MapMode {
                mode: mode.clone(),
                size: attribute(chunk, "players").parse().unwrap_or(0),
                kind: attribute(chunk, "type"),
                locid: attribute(chunk, "locid"),
                ai: attribute(chunk, "ai") == "1",
            });
            inner = tag_end;
        }
        cursor = end + 1;
    }

    MapInfo {
        id: attribute(head, "gsid"),
        // The engine spells level paths in lower case: `maplist.append
        // "dalian_plant" "gpm_cq" 16`.
        path: directory.to_lowercase(),
        name: if name.is_empty() { directory.to_string() } else { name },
        briefing,
        modes,
    }
}

/// Every installed level, read once, sorted by display name like the
/// original menu shows them.
fn maps() -> &'static Vec<MapInfo> {
    static ONCE: OnceLock<Vec<MapInfo>> = OnceLock::new();
    ONCE.get_or_init(|| {
        let root = game_dir().join("mods/bf2/Levels");
        let mut out: Vec<MapInfo> = std::fs::read_dir(&root)
            .map(|entries| {
                entries
                    .flatten()
                    .filter(|e| e.path().is_dir())
                    .filter_map(|e| {
                        let directory = e.file_name().to_string_lossy().to_string();
                        let desc = e.path().join("Info").join(format!("{directory}.desc"));
                        let xml = std::fs::read_to_string(&desc).ok()?;
                        Some(parse_desc(&xml, &directory))
                    })
                    .collect()
            })
            .unwrap_or_default();
        out.sort_by(|a, b| a.name.cmp(&b.name));
        tracing::info!(target: "bf2_bridge", "levels found: {}", out.len());
        out
    })
}

/// Game mode the map list is filtered by. The singleplayer screen sets
/// it to `gpm_cq` on entry (`setGamemode("gpm_cq")`, DoAction of the
/// page, 0x35e).
fn game_mode() -> &'static std::sync::Mutex<String> {
    static ONCE: OnceLock<std::sync::Mutex<String>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(String::from("gpm_cq")))
}

/// `GeneralSettings.setUseBots(flag)` — bots on for this game.
///
/// It also decides what the map list shows: the engine keeps only
/// AI-capable `<maptype>` entries while bots are on (`SwiffPlayer.dll`,
/// 0x1002b280 — the `GameServerSettings` flag at vtable +0xa8 gates the
/// per-entry byte at +0x58). The multiplayer pages set it to `false`
/// before listing (page DoAction 0x9f), the singleplayer one to `true`.
fn use_bots() -> &'static std::sync::Mutex<bool> {
    static ONCE: OnceLock<std::sync::Mutex<bool>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(false))
}

fn set_use_bots<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let flag = args.first().unwrap_or(&Value::Undefined).as_bool(activation.swf_version());
    tracing::debug!(target: "bf2_bridge", "bots: {}", flag);
    *use_bots().lock().unwrap() = flag;
    Ok(Value::Undefined)
}

fn get_use_bots<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(Value::from(*use_bots().lock().unwrap()))
}

/// Find a level by the path the menu passes around (`dalian_plant`).
fn map_by_path(path: &str) -> Option<&'static MapInfo> {
    let wanted = path.to_lowercase();
    maps().iter().find(|map| map.path == wanted)
}

/// `Multiplay.getMapNameFromId(path)` — display name of the level.
///
/// The argument is the level folder, not a number: the selected-map
/// panel calls it with `Logic.getStorageString("currentMapFolder")`
/// (page DoAction 0x165, 0x2b0).
fn map_name_from_id<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let path = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let name = map_by_path(&path).map(|map| map.name.clone()).unwrap_or(path);
    Ok(AvmString::new_utf8(activation.gc(), name).into())
}

/// `Multiplay.getMapDescriptionFromId(path)` — the briefing text.
fn map_description_from_id<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let path = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let text = map_by_path(&path)
        .map(|map| lexicon().get(&map.briefing).cloned().unwrap_or_else(|| map.briefing.clone()))
        .unwrap_or_default();
    Ok(AvmString::new_utf8(activation.gc(), text).into())
}

/// `Multiplay.getMapPathFromName(name)` — reverse of the above.
fn map_path_from_name<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let name = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let path = maps()
        .iter()
        .find(|map| map.name.eq_ignore_ascii_case(&name))
        .map(|map| map.path.clone())
        .unwrap_or(name);
    Ok(AvmString::new_utf8(activation.gc(), path).into())
}

/// `Multiplay.getGameModeFromId(id)` — readable name of a game mode.
///
/// The selected-map panel builds `<size>, <mode name>` out of it (page
/// DoAction 0x2e2). Map layouts follow the `GAMEMODE_<type>` convention
/// in the lexicon (`GAMEMODE_headon` -> "Head-on"), so we look the id up
/// the same way. **Not established:** there is no `GAMEMODE_gpm_cq` key
/// in any localisation file, so where the engine takes that name from is
/// still unknown; until then a missing key shows the id itself.
fn game_mode_from_id<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let id = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let text = lexicon().get(&format!("GAMEMODE_{id}")).cloned().unwrap_or(id);
    Ok(AvmString::new_utf8(activation.gc(), text).into())
}

// --- the map list a game starts with ------------------------------------
//
// `selectMap` clears the list and appends the chosen level:
// `Multiplay.clearMapList()`, then `addMapToMapList(path, "", size)`
// (singleplayer page DoAction 0x2d3..0x2f9). It is the same list the
// game writes to `mapList.con`: `maplist.append "dalian_plant" "gpm_cq" 16`.

pub struct MapListEntry {
    pub path: String,
    pub mode: String,
    pub size: u32,
}

fn map_list() -> &'static std::sync::Mutex<Vec<MapListEntry>> {
    static ONCE: OnceLock<std::sync::Mutex<Vec<MapListEntry>>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(Vec::new()))
}

fn clear_map_list<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    map_list().lock().unwrap().clear();
    Ok(Value::Undefined)
}

fn add_map_to_map_list<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let path = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let mode = args.get(1).unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    let size = args.get(2).unwrap_or(&Value::Undefined).coerce_to_f64(activation)? as u32;
    // An empty mode means "the one the list is filtered by".
    let mode = if mode.is_empty() { game_mode().lock().unwrap().clone() } else { mode };
    tracing::info!(target: "bf2_bridge", "map in the list: {} {} {}", path, mode, size);
    map_list().lock().unwrap().push(MapListEntry { path, mode, size });
    Ok(Value::Undefined)
}

// --- commands back to the host -----------------------------------------
//
// Some menu actions are not questions but orders: start a game, quit.
// In the original they land in the engine directly; here the movie runs
// inside a library, so we queue them and the C++ side picks them up once
// per frame (`obf2_flash_take_command`).

fn commands() -> &'static std::sync::Mutex<Vec<String>> {
    static ONCE: OnceLock<std::sync::Mutex<Vec<String>>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(Vec::new()))
}

/// Take the oldest queued command, if any.
pub fn take_command() -> Option<String> {
    let mut queue = commands().lock().unwrap();
    if queue.is_empty() { None } else { Some(queue.remove(0)) }
}

/// `Multiplay.createServer()` — start the game the menu has set up.
///
/// The singleplayer page calls it right after `setSingleplayerSettings`
/// (page DoAction 0x1f8..0x22a), by which point the chosen level is the
/// only entry of the map list.
fn create_server<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let list = map_list().lock().unwrap();
    let Some(entry) = list.first() else {
        tracing::warn!(target: "bf2_bridge", "createServer with no map chosen");
        return Ok(false.into());
    };
    let command = format!("level {} {} {}", entry.path, entry.mode, entry.size);
    tracing::info!(target: "bf2_bridge", "{}", command);
    commands().lock().unwrap().push(command);
    Ok(true.into())
}

/// `Logic.quit()` — leave the game.
fn quit<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    commands().lock().unwrap().push(String::from("quit"));
    Ok(Value::Undefined)
}

fn set_filter_mode<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let mode = args.first().unwrap_or(&Value::Undefined).coerce_to_string(activation)?.to_string();
    tracing::debug!(target: "bf2_bridge", "game mode: {}", mode);
    *game_mode().lock().unwrap() = mode;
    Ok(Value::Undefined)
}

fn get_filter_mode<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let mode = game_mode().lock().unwrap().clone();
    Ok(AvmString::new_utf8(activation.gc(), mode).into())
}

// --- the mod -----------------------------------------------------------
//
// Everything about a mod lies in its own `mod.desc` — plain XML with the
// fields `title`, `desc`, `url`, `logo`, `icon`, `music`, `version`. The menu
// asks for them through `Mod.*` and `Logic.getModVersion`.

/// The value of one tag from `mods/<mod>/mod.desc`.
fn mod_field(name: &str) -> String {
    static ONCE: OnceLock<String> = OnceLock::new();
    let text = ONCE.get_or_init(|| {
        std::fs::read_to_string(game_dir().join("mods/bf2/mod.desc")).unwrap_or_default()
    });
    let open = format!("<{name}>");
    let close = format!("</{name}>");
    let Some(from) = text.find(&open) else { return String::new() };
    let rest = &text[from + open.len()..];
    let Some(to) = rest.find(&close) else { return String::new() };
    rest[..to].trim().to_string()
}

fn mod_version<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), mod_field("version")).into())
}

/// `Logic.getModVersion()` — version of the **player**, not of the mod.
///
/// Despite the name it does not go near `mod.desc`: it asks the engine
/// interface at vtable +0x1a0 (`SwiffPlayer.dll`, 0x1004d520), the same
/// call `General.getHostVersion` uses. That is why the original shows a
/// build number there (`1.5.3153-802.0`) and not `BF2 1.5`. We answer
/// with our own, since ours is the engine running the movie.
fn host_version<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let text = host_version_text().lock().unwrap().clone();
    Ok(AvmString::new_utf8(activation.gc(), text).into())
}

/// What we call ourselves. The C++ side sets it at start-up: this file is
/// compiled inside Ruffle, so its own crate version is Ruffle's, not ours.
pub fn host_version_text() -> &'static std::sync::Mutex<String> {
    static ONCE: OnceLock<std::sync::Mutex<String>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(String::from("OpenBattlefield2")))
}

fn mod_title<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), mod_field("title")).into())
}

fn mod_desc<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), mod_field("desc")).into())
}

fn mod_url<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), mod_field("url")).into())
}

fn mod_logo<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), mod_field("logo")).into())
}

/// The path to the mod — the way the game itself writes it in `Init.con`
/// (`game.setActiveModId "mods/bf2"`).
fn mod_path<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), "mods/bf2").into())
}

fn mod_name<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), "bf2").into())
}

// --- logging into a profile --------------------------------------------
//
// `Profile.getLoginStatus()` (`SwiffPlayer.dll`, 0x1005f390) gives back
// **2** when a network profile is logged in (`ProfileManager`), **1** when a
// local one (`LocalProfileManager`), and **0** when neither. While it is
// zero, the movie's main dispatcher drives to the `login` page; as soon as
// it is 1 or 2 it shows `Logic.getStorageString("currentFrameName")`.
//
// `Profile.loginOfflineAccount(number)` (0x1005e850) takes **a number** —
// the profile number — tells `LocalProfileManager` to log in and returns
// `true`/`false`.

/// The number of the profile logged into; empty means not logged in.
fn login() -> &'static std::sync::Mutex<Option<String>> {
    static ONCE: OnceLock<std::sync::Mutex<Option<String>>> = OnceLock::new();
    ONCE.get_or_init(|| std::sync::Mutex::new(None))
}

fn login_offline<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let number = args.first().unwrap_or(&Value::Undefined).coerce_to_f64(activation)?;
    let wanted = number.round() as i64;
    let found = profiles()
        .iter()
        .find(|entry| entry.id.parse::<i64>().ok() == Some(wanted))
        .map(|entry| entry.id.clone());
    tracing::info!(target: "bf2_bridge", "login to profile {}: {:?}", wanted, found);
    let ok = found.is_some();
    *login().lock().unwrap() = found;
    Ok(ok.into())
}

fn login_status<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    // We have no network accounts, so 2 never happens: either 1 or 0.
    let status = if login().lock().unwrap().is_some() { 1.0 } else { 0.0 };
    Ok(Value::from(status))
}

fn logout<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    *login().lock().unwrap() = None;
    Ok(Value::Undefined)
}

/// The player's name from the profile in `Documents`.
fn player_name<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), profile().name.clone()).into())
}

/// How many profiles lie in `Documents/Battlefield 2/Profiles`.
fn local_profiles<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok((profile().count as f64).into())
}

/// `Profile.getNamePrefix()` — the prefix to the player's name.
///
/// This is **not** the profile number: the game keeps it as its own line in
/// `Documents/Battlefield 2/Profiles/Global.con`
/// (`GlobalSettings.setNamePrefix ""`), next to `setDefaultUser`.
/// It shows right on the login screen: the PREFIX field showed "0001" while
/// we were giving the folder number there.
fn name_prefix<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    static ONCE: OnceLock<String> = OnceLock::new();
    let prefix = ONCE.get_or_init(|| {
        let text = std::fs::read_to_string(docs_dir().join("Profiles").join("Global.con"))
            .unwrap_or_default();
        text.lines()
            .find_map(|line| line.split_once("setNamePrefix"))
            .map(|(_, rest)| rest.trim().trim_matches('"').to_string())
            .unwrap_or_default()
    });
    Ok(AvmString::new_utf8(activation.gc(), prefix.clone()).into())
}

/// `Locale.getLanguage()` — the game gives the localisation folder's name.
fn locale_language<'gc>(
    activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(AvmString::new_utf8(activation.gc(), "english").into())
}

/// `Logic.active()` — "the game is really running".
///
/// This is the first thing the menu asks the engine. While the answer is
/// "no", components stay in author mode (`m_inAuthorMode`): they show their
/// own keys as captions and load no external images.
fn logic_active<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(true.into())
}

/// `Locale.checkXMLStatus()` — "the translation is ready".
///
/// The menu keeps its strings not only in `getString`: it also asks whether
/// the translation XML is loaded yet, and while the answer is "no" it draws
/// the keys themselves. Our dictionary is ready at once, so we say "yes".
fn locale_ready<'gc>(
    _activation: &mut Activation<'_, 'gc>,
    _this: Object<'gc>,
    _args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    Ok(true.into())
}

/// What we really can do already. `__resolve` catches the rest.
fn real_methods(object: &str) -> &'static [(&'static str, crate::avm1::function::NativeFunction)] {
    match object {
        "Locale" => &[
            ("getString", locale_get_string),
            ("loadString", locale_get_string),
            ("getLanguage", locale_language),
            ("checkXMLStatus", locale_ready),
        ],
        "Client" => &[("getPlayerName", player_name)],
        "GeneralSettings" => &[("setUseBots", set_use_bots), ("getUseBots", get_use_bots)],
        "Multiplay" => &[
            ("setFilterGameMode", set_filter_mode),
            ("getFilterGameMode", get_filter_mode),
            ("getSelectedFilterGameMode", get_filter_mode),
            ("getMapNameFromId", map_name_from_id),
            ("getMapDescriptionFromId", map_description_from_id),
            ("getMapPathFromName", map_path_from_name),
            ("getGameModeFromId", game_mode_from_id),
            ("clearMapList", clear_map_list),
            ("addMapToMapList", add_map_to_map_list),
            ("createServer", create_server),
        ],
        "General" => &[
            ("getListRevision", list_revision),
            ("getListEntries", list_entries),
            ("flushList", list_flush),
        ],
        "Mod" => &[
            ("getModVersion", mod_version),
            ("getModTitle", mod_title),
            ("getModDesc", mod_desc),
            ("getModUrl", mod_url),
            ("getModLogo", mod_logo),
            ("getModPath", mod_path),
            ("getModPathShort", mod_name),
            ("getCurrentModName", mod_name),
        ],
        "Logic" => &[
            ("active", logic_active),
            ("getModVersion", host_version),
            ("setStorageString", storage_set),
            ("getStorageString", storage_get),
            ("eraseStorageString", storage_erase),
            ("quit", quit),
        ],
        "Profile" => &[
            ("getNumLocalProfiles", local_profiles),
            ("getStatPlayerName", player_name),
            ("getActivePlayer", player_name),
            ("getNamePrefix", name_prefix),
            ("loginOfflineAccount", login_offline),
            ("getLoginStatus", login_status),
            ("logout", logout),
        ],
        _ => &[],
    }
}

/// `__resolve(name)`: the movie asked for something the object does not have.
fn resolve<'gc>(
    activation: &mut Activation<'_, 'gc>,
    this: Object<'gc>,
    args: &[Value<'gc>],
) -> Result<Value<'gc>, Error<'gc>> {
    let name = args
        .first()
        .unwrap_or(&Value::Undefined)
        .coerce_to_string(activation)?;
    let key = AvmString::new_utf8(activation.gc(), "__bf2name");
    let owner = this
        .get(key, activation)
        .unwrap_or(Value::Undefined)
        .coerce_to_string(activation)
        .unwrap_or_else(|_| AvmString::new_utf8(activation.gc(), "?"));
    tracing::info!(target: "bf2_bridge", "{}.{}", owner, name);

    // The stub is made in advance and put on the object itself: Ruffle does
    // not let a function prototype be fetched during the call.
    let slot = AvmString::new_utf8(activation.gc(), "__bf2stub");
    Ok(this.get_stored(slot, activation).unwrap_or(Value::Undefined))
}

/// Make one bridge object with a name.
fn make<'gc>(context: &mut DeclContext<'_, 'gc>, name: &str) -> Object<'gc> {
    let object = Object::new(context.strings, Some(context.object_proto));
    let fn_proto = context.fn_proto;
    let handler =
        FunctionObject::native(resolve).build(context.strings, Some(fn_proto.into()), None);
    object.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "__resolve"),
        handler.into(),
        Attribute::DONT_ENUM | Attribute::DONT_DELETE,
    );
    let stub_fn = FunctionObject::native(stub).build(context.strings, Some(fn_proto.into()), None);
    object.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "__bf2stub"),
        stub_fn.into(),
        Attribute::DONT_ENUM | Attribute::DONT_DELETE,
    );
    object.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "__bf2name"),
        AvmString::new_utf8(context.gc(), name).into(),
        Attribute::DONT_ENUM | Attribute::DONT_DELETE,
    );

    // Real methods go in as ordinary properties — they win over
    // `__resolve`, because that only fires when the property is missing.
    for (method, native) in real_methods(name) {
        let function = FunctionObject::native(*native).build(context.strings, Some(fn_proto.into()), None);
        object.define_value(
            context.gc(),
            AvmString::new_utf8(context.gc(), *method),
            function.into(),
            Attribute::DONT_ENUM,
        );
    }
    object
}

/// The stock `mx.lang.Locale` set — the same one as in `SwiffPlayer.dll`
/// (0xd7af4). The movie checks this class is there and, when it is, does not
/// declare its own fallback implementation at all.
fn locale_class_methods() -> &'static [(&'static str, crate::avm1::function::NativeFunction)] {
    &[
        ("addDelayedInstance", locale_add_delayed),
        ("assignDelayedInstances", locale_assign_delayed),
        // `initialize()` and `start()` in the stock class kick off loading
        // the XML and hand out the captions when it finishes. Our dictionary
        // is ready at once, so both of them are just the handing out.
        ("initialize", locale_assign_delayed),
        ("start", locale_assign_delayed),
        ("checkXMLStatus", locale_ready),
        ("loadString", locale_get_string),
        ("getString", locale_get_string),
        ("getLanguage", locale_language),
    ]
}

/// Put `_global.mx.lang.Locale` in place.
///
/// On the way we create `mx` and `mx.lang`: the movie goes by exactly that
/// path, not by the short name.
fn install_locale_class<'gc>(context: &mut DeclContext<'_, 'gc>, globals: Object<'gc>) {
    let locale = Object::new(context.strings, Some(context.object_proto));
    let fn_proto = context.fn_proto;
    for (method, native) in locale_class_methods() {
        let function =
            FunctionObject::native(*native).build(context.strings, Some(fn_proto.into()), None);
        locale.define_value(
            context.gc(),
            AvmString::new_utf8(context.gc(), *method),
            function.into(),
            Attribute::DONT_ENUM,
        );
    }
    // The rest of the stock names (`setFlaName`, `setDefaultLang`,
    // `addXMLPath`, `setXMLLang`, `onXMLLoad`, `checkDelayArray`) are caught
    // by `__resolve`: the movie calls them, but nothing depends on the answer.
    let handler =
        FunctionObject::native(resolve).build(context.strings, Some(fn_proto.into()), None);
    let stub_fn = FunctionObject::native(stub).build(context.strings, Some(fn_proto.into()), None);
    for (name, value) in [
        ("__resolve", Value::from(handler)),
        ("__bf2stub", Value::from(stub_fn)),
        ("__bf2name", AvmString::new_utf8(context.gc(), "mx.lang.Locale").into()),
    ] {
        locale.define_value(
            context.gc(),
            AvmString::new_utf8(context.gc(), name),
            value,
            Attribute::DONT_ENUM | Attribute::DONT_DELETE,
        );
    }

    let lang = Object::new(context.strings, Some(context.object_proto));
    lang.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "Locale"),
        locale.into(),
        Attribute::empty(),
    );
    let mx = Object::new(context.strings, Some(context.object_proto));
    mx.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "lang"),
        lang.into(),
        Attribute::empty(),
    );
    globals.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "mx"),
        mx.into(),
        Attribute::DONT_ENUM,
    );
}

/// Put the bridge into `_global`: both as `Logic` and as `dice.bf2.Logic`.
///
/// Both forms occur in the game's movies, so the object is the same one and
/// there are two names on it.
pub fn install<'gc>(context: &mut DeclContext<'_, 'gc>, globals: Object<'gc>) {
    let dice = Object::new(context.strings, Some(context.object_proto));
    let bf2 = Object::new(context.strings, Some(context.object_proto));

    // Settings objects live only under `dice.bf2`, not in `_global`.
    for name in SETTINGS_OBJECTS {
        let object = make(context, name);
        let key = AvmString::new_utf8(context.gc(), name);
        bf2.define_value(context.gc(), key, object.into(), Attribute::empty());
    }

    let mut general = None;
    for name in OBJECTS {
        let object = make(context, name);
        let key = AvmString::new_utf8(context.gc(), name);
        globals.define_value(context.gc(), key, object.into(), Attribute::DONT_ENUM);
        bf2.define_value(context.gc(), key, object.into(), Attribute::empty());
        if name == "General" {
            general = Some(object);
        }
    }

    dice.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "bf2"),
        bf2.into(),
        Attribute::empty(),
    );

    // `General` lives **not** in `dice.bf2` but plainly in `dice`: lists in
    // the movie call `dice.General.getListEntries(...)` (class
    // `__Packages.dice.General`, the DoInitAction block of symbol 30, and its
    // fallback is switched off by a `_global.dice.General` check).
    if let Some(general) = general {
        dice.define_value(
            context.gc(),
            AvmString::new_utf8(context.gc(), "General"),
            general.into(),
            Attribute::empty(),
        );
    }
    globals.define_value(
        context.gc(),
        AvmString::new_utf8(context.gc(), "dice"),
        dice.into(),
        Attribute::DONT_ENUM,
    );

    install_locale_class(context, globals);
}
