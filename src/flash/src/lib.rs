//! The Flash player for our client: a thin C interface over Ruffle.
//!
//! Battlefield 2's menu is Flash (`mainMenu.swf`), and reproducing it
//! ourselves makes no sense: `docs/research/11-ruffle-menu.md`.
//! Ruffle plays the same movie the real game opens, so our job here is only
//! to give C++ three things: a frame step, the pixels and the size.
//!
//! We draw **into a texture**, not into a window: all our graphics go through
//! `obf2::gfx`, and Flash has to be an ordinary image we put on screen
//! ourselves. That is exactly what Ruffle's `exporter` does, and the way a
//! frame is taken comes from there.

use std::ffi::CStr;
use std::os::raw::c_char;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use ruffle_core::backend::locale::DeterministicLocaleBackend;
use ruffle_core::backend::navigator::{
    ErrorResponse, NavigationMethod, NavigatorBackend, NullExecutor, NullNavigatorBackend,
    OwnedFuture, Request, SuccessResponse,
};
use ruffle_core::indexmap::IndexMap;
use ruffle_core::async_channel;
use ruffle_core::socket::{SocketAction, SocketHandle};
use ruffle_core::url::{ParseError, Url};
use ruffle_core::loader::Error as NavigatorError;
use ruffle_core::events::{KeyDescriptor, KeyLocation, LogicalKey, MouseButton, PhysicalKey, PlayerEvent, TextControlCode};
use ruffle_core::limits::ExecutionLimit;
use ruffle_core::tag_utils::movie_from_path;
use ruffle_core::{FloatDuration, Player, PlayerBuilder};
use ruffle_render_wgpu::backend::{WgpuRenderBackend, create_wgpu_instance, request_adapter_and_device};
use ruffle_render_wgpu::descriptors::Descriptors;
use ruffle_render_wgpu::target::TextureTarget;
use ruffle_render_wgpu::wgpu;

// --- images ------------------------------------------------------------
//
// Nearly every `.png` the menu loads is in fact a DDS: map previews,
// flags, award icons. The original player decodes them because the game
// hands it engine textures rather than files.
//
// The decoder is ours and lives in `obf2::texture`, so we do not write a
// second one here — C++ hands the function over at start-up. Linking to
// it directly would be circular: the executable links this library.

type DdsDecoder = extern "C" fn(
    data: *const u8,
    len: usize,
    out: *mut u8,
    capacity: usize,
    width: *mut u32,
    height: *mut u32,
) -> usize;

static DDS_DECODER: Mutex<Option<DdsDecoder>> = Mutex::new(None);

/// Hand the DDS decoder over. Called once before the first movie opens.
#[unsafe(no_mangle)]
pub extern "C" fn obf2_flash_set_dds_decoder(decoder: DdsDecoder) {
    *DDS_DECODER.lock().unwrap() = Some(decoder);
}

/// DDS bytes -> PNG bytes, or `None` when this is not a DDS we can read.
///
/// PNG is what Ruffle's loader expects; re-encoding is cheap next to
/// decoding, and it keeps the whole detour inside this function.
fn dds_to_png(body: &[u8]) -> Option<Vec<u8>> {
    if !body.starts_with(b"DDS ") {
        return None;
    }
    let decoder = (*DDS_DECODER.lock().unwrap())?;

    let (mut width, mut height) = (0u32, 0u32);
    let needed = decoder(body.as_ptr(), body.len(), std::ptr::null_mut(), 0, &mut width, &mut height);
    if needed == 0 || width == 0 || height == 0 {
        return None;
    }
    let mut pixels = vec![0u8; needed];
    let written = decoder(
        body.as_ptr(),
        body.len(),
        pixels.as_mut_ptr(),
        pixels.len(),
        &mut width,
        &mut height,
    );
    if written != needed {
        return None;
    }

    let image = image::RgbaImage::from_raw(width, height, pixels)?;
    let mut out = Vec::new();
    image
        .write_to(&mut std::io::Cursor::new(&mut out), image::ImageFormat::Png)
        .ok()?;
    Some(out)
}

/// A response whose body we replaced.
struct Decoded {
    url: String,
    body: Vec<u8>,
}

impl SuccessResponse for Decoded {
    fn url(&self) -> std::borrow::Cow<'_, str> {
        std::borrow::Cow::Borrowed(&self.url)
    }

    fn set_url(&mut self, url: String) {
        self.url = url;
    }

    fn body(self: Box<Self>) -> OwnedFuture<Vec<u8>, NavigatorError> {
        Box::pin(async move { Ok(self.body) })
    }

    fn text_encoding(&self) -> Option<&'static ruffle_core::encoding_rs::Encoding> {
        None
    }

    fn status(&self) -> u16 {
        0
    }

    fn redirected(&self) -> bool {
        false
    }

    fn next_chunk(&mut self) -> OwnedFuture<Option<Vec<u8>>, NavigatorError> {
        let chunk = std::mem::take(&mut self.body);
        Box::pin(async move { Ok(if chunk.is_empty() { None } else { Some(chunk) }) })
    }

    fn expected_length(&self) -> Result<Option<u64>, NavigatorError> {
        Ok(Some(self.body.len() as u64))
    }
}

/// Navigator that understands the game's own `$` path prefix.
///
/// The menu builds image paths as `$/Levels/<map>/Info/<mode>_<size>_menuMap.png`
/// (`loadImage` on the map panel). In the original the player hands such
/// a path to the engine's file system, where `$` is the mod root. We do
/// the same and let the plain file navigator take it from there.
struct GamePaths {
    inner: NullNavigatorBackend,
    mod_root: PathBuf,
}

impl GamePaths {
    fn rewrite(&self, url: &str) -> String {
        let Some(rest) = url.strip_prefix('$') else { return url.to_string() };
        let rest = rest.trim_start_matches('/');
        match Url::from_file_path(self.mod_root.join(rest)) {
            Ok(full) => full.to_string(),
            Err(()) => url.to_string(),
        }
    }
}

impl NavigatorBackend for GamePaths {
    fn navigate_to_url(
        &self,
        url: &str,
        target: &str,
        vars_method: Option<(NavigationMethod, IndexMap<String, String>)>,
    ) {
        self.inner.navigate_to_url(&self.rewrite(url), target, vars_method);
    }

    fn fetch(&self, mut request: Request) -> OwnedFuture<Box<dyn SuccessResponse>, ErrorResponse> {
        let rewritten = self.rewrite(request.url());
        if rewritten != request.url() {
            request = Request::get(rewritten);
        }
        let inner = self.inner.fetch(request);
        Box::pin(async move {
            let response = inner.await?;
            let url = response.url().into_owned();
            let body = response.body().await.map_err(|error| ErrorResponse {
                url: url.clone(),
                error,
            })?;
            match dds_to_png(&body) {
                Some(png) => Ok(Box::new(Decoded { url, body: png }) as Box<dyn SuccessResponse>),
                None => Ok(Box::new(Decoded { url, body }) as Box<dyn SuccessResponse>),
            }
        })
    }

    fn resolve_url(&self, url: &str) -> Result<Url, ParseError> {
        self.inner.resolve_url(&self.rewrite(url))
    }

    fn spawn_future(&mut self, future: OwnedFuture<(), NavigatorError>) {
        self.inner.spawn_future(future);
    }

    fn pre_process_url(&self, url: Url) -> Url {
        self.inner.pre_process_url(url)
    }

    fn connect_socket(
        &mut self,
        host: String,
        port: u16,
        timeout: std::time::Duration,
        handle: SocketHandle,
        receiver: async_channel::Receiver<Vec<u8>>,
        sender: async_channel::Sender<SocketAction>,
    ) {
        self.inner.connect_socket(host, port, timeout, handle, receiver, sender);
    }
}

/// One movie's state, opaque to C++.
pub struct Movie {
    player: Arc<Mutex<Player>>,
    width: u32,
    height: u32,
    // The movie pulls external files — pictures through `loadMovie` and the
    // translation XML. Those are deferred actions, and somebody has to drive
    // them; that is what the executor is.
    executor: NullExecutor,
}

/// Ruffle's log: `trace()` from the movie and the loader's complaints. Enabled
/// by the variable `OBF2_FLASH_LOG` (e.g. `OBF2_FLASH_LOG=info`), because in
/// an ordinary run it only gets in the way.
fn start_log() {
    use std::sync::Once;
    static ONCE: Once = Once::new();
    ONCE.call_once(|| {
        let Ok(filter) = std::env::var("OBF2_FLASH_LOG") else { return };
        let _ = tracing_subscriber::fmt()
            .with_env_filter(tracing_subscriber::EnvFilter::new(filter))
            .with_writer(std::io::stderr)
            .try_init();
    });
}

fn open(path: &str, want_width: u32, want_height: u32) -> Option<Movie> {
    start_log();
    let movie = movie_from_path(&PathBuf::from(path), None).ok()?;

    // Zero means "take the stage's size from the movie itself".
    let width = if want_width > 0 { want_width } else { movie.width().to_pixels().round() as u32 };
    let height = if want_height > 0 { want_height } else { movie.height().to_pixels().round() as u32 };

    // `all()` — let wgpu pick whatever the machine has (Metal for us).
    let backends = wgpu::Backends::all();
    let instance = create_wgpu_instance(backends, wgpu::BackendOptions::default(), None);
    let (adapter, device, queue) = futures::executor::block_on(request_adapter_and_device(
        backends,
        &instance,
        None,
        wgpu::PowerPreference::default(),
    ))
    .ok()?;
    let descriptors = Arc::new(Descriptors::new(instance, adapter, device, queue));
    let target = TextureTarget::new(&descriptors.device, (width, height)).ok()?;
    let renderer = WgpuRenderBackend::new(descriptors, target).ok()?;

    // The path the movie looks for its files relative to is the movie's own
    // directory. Without it `loadMovie("images/…png")` leads nowhere.
    let executor = NullExecutor::new();
    let base = PathBuf::from(path).parent().map(|p| p.to_path_buf()).unwrap_or_default();
    let navigator = NullNavigatorBackend::with_base_path(&base, &executor).ok()?;
    // `$` in a menu path means the mod root: the movie sits four levels
    // below it (`mods/bf2/Menu_client/External/FlashMenu`).
    // `Url::from_file_path` needs an absolute path, and the game is
    // usually launched with a relative one.
    let absolute = std::fs::canonicalize(&base).unwrap_or_else(|_| base.clone());
    let mod_root = absolute
        .parent()
        .and_then(|p| p.parent())
        .and_then(|p| p.parent())
        .map(|p| p.to_path_buf())
        .unwrap_or(absolute);
    let navigator = GamePaths { inner: navigator, mod_root };

    let player = PlayerBuilder::new()
        .with_renderer(renderer)
        .with_navigator(navigator)
        .with_locale(DeterministicLocaleBackend::default())
        .with_movie(movie)
        .with_viewport_dimensions(width, height, 1.0)
        // Without this the player stays paused and `tick` does nothing.
        .with_autoplay(true)
        .build();

    // The menu's background is drawn by the engine, not by the movie: in
    // `mainMenu.swf` there is not a single reference to `images/background/`,
    // while the game puts `background_2.png` there
    // (docs/research/03-startup-and-menu.md). So the stage has to be transparent —
    // otherwise the movie floods the frame with its own colour.
    player.lock().unwrap().set_window_mode("transparent");

    Some(Movie { player, width, height, executor })
}

/// Open a movie. Zero `width`/`height` mean taking the size from the stage.
///
/// # Safety
/// `path` has to be a valid C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_open(
    path: *const c_char,
    width: u32,
    height: u32,
) -> *mut Movie {
    if path.is_null() {
        return std::ptr::null_mut();
    }
    let Ok(text) = (unsafe { CStr::from_ptr(path) }).to_str() else {
        return std::ptr::null_mut();
    };
    match open(text, width, height) {
        Some(movie) => Box::into_raw(Box::new(movie)),
        None => std::ptr::null_mut(),
    }
}

/// # Safety
/// `movie` is what `obf2_flash_open` returned, and it is closed once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_close(movie: *mut Movie) {
    if !movie.is_null() {
        drop(unsafe { Box::from_raw(movie) });
    }
}

/// The frame's size in pixels.
///
/// # Safety
/// All three pointers have to be valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_size(movie: *const Movie, width: *mut u32, height: *mut u32) {
    let Some(movie) = (unsafe { movie.as_ref() }) else { return };
    unsafe {
        if !width.is_null() {
            *width = movie.width;
        }
        if !height.is_null() {
            *height = movie.height;
        }
    }
}

/// One step of the movie.
///
/// # Safety
/// `movie` is what `obf2_flash_open` returned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_advance(movie: *mut Movie) {
    let Some(movie) = (unsafe { movie.as_mut() }) else { return };
    {
        let mut player = movie.player.lock().unwrap();
        player.preload(&mut ExecutionLimit::none());
        // We drive the player by **time**, not by calling `run_frame`: the menu
        // rests on `setInterval` (the update manager registers its calls there:
        // "UM: Register callback for … freq: 1"), and timers are counted from
        // time. We give exactly one movie frame, so one frame of our loop
        // corresponds to one frame of the menu.
        let rate = player.frame_rate();
        let frame_time = if rate > 0.0 { 1000.0 / rate } else { 1000.0 / 30.0 };
        player.tick(FloatDuration::from_millis(frame_time));
    }
    // Loading external files is driven only here.
    movie.executor.run();
}

/// Tell the movie what the host calls itself.
///
/// The menu shows it where the original shows the game build
/// (`Logic.getModVersion`, see the bridge).
///
/// # Safety
/// `text` must be a valid C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_set_host_version(text: *const c_char) {
    if text.is_null() {
        return;
    }
    if let Ok(text) = (unsafe { CStr::from_ptr(text) }).to_str() {
        *ruffle_core::bf2_host_version().lock().unwrap() = text.to_string();
    }
}

/// Take one queued order from the menu, if any.
///
/// Writes a NUL-terminated string into `buffer` and returns its length
/// without the terminator; 0 means the queue is empty. Orders are lines
/// like `level dalian_plant gpm_cq 16` or `quit`.
///
/// # Safety
/// `buffer` must be writable for `len` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_take_command(buffer: *mut u8, len: usize) -> usize {
    if buffer.is_null() || len == 0 {
        return 0;
    }
    let Some(command) = ruffle_core::bf2_take_command() else { return 0 };
    let bytes = command.as_bytes();
    let count = bytes.len().min(len - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), buffer, count);
        *buffer.add(count) = 0;
    }
    count
}

/// Draw a frame and hand it back as RGBA.
///
/// Returns the number of bytes written, or 0. The buffer has to hold
/// `width * height * 4`.
///
/// # Safety
/// `pixels` has to point at no fewer than `len` writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_render(movie: *mut Movie, pixels: *mut u8, len: usize) -> usize {
    let Some(movie) = (unsafe { movie.as_mut() }) else { return 0 };
    let needed = (movie.width as usize) * (movie.height as usize) * 4;
    if pixels.is_null() || len < needed {
        return 0;
    }

    movie.player.lock().unwrap().render();

    let mut player = movie.player.lock().unwrap();
    let renderer = <dyn std::any::Any>::downcast_mut::<WgpuRenderBackend<TextureTarget>>(
        player.renderer_mut(),
    );
    let Some(renderer) = renderer else { return 0 };
    let Some(image) = renderer.capture_frame() else { return 0 };

    let bytes = image.as_raw();
    let count = bytes.len().min(needed);
    unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), pixels, count) };
    count
}

/// The cursor moved. The coordinates are in stage pixels.
///
/// # Safety
/// `movie` is what `obf2_flash_open` returned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_mouse_move(movie: *mut Movie, x: f64, y: f64) {
    let Some(movie) = (unsafe { movie.as_mut() }) else { return };
    let mut player = movie.player.lock().unwrap();
    player.handle_event(PlayerEvent::MouseMove { x, y });
}

/// The left button was pressed or released.
///
/// # Safety
/// `movie` is what `obf2_flash_open` returned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_mouse_button(movie: *mut Movie, x: f64, y: f64, down: i32) {
    let Some(movie) = (unsafe { movie.as_mut() }) else { return };
    let mut player = movie.player.lock().unwrap();
    let event = if down != 0 {
        PlayerEvent::MouseDown { x, y, button: MouseButton::Left, index: None }
    } else {
        PlayerEvent::MouseUp { x, y, button: MouseButton::Left }
    };
    player.handle_event(event);
}

/// One typed character (UTF-32). Needed for the menu's input fields.
///
/// # Safety
/// `movie` is what `obf2_flash_open` returned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_text(movie: *mut Movie, codepoint: u32) {
    let Some(movie) = (unsafe { movie.as_mut() }) else { return };
    let Some(character) = char::from_u32(codepoint) else { return };
    let mut player = movie.player.lock().unwrap();
    player.handle_event(PlayerEvent::TextInput { codepoint: character });
}

/// The control keys an input field understands apart from characters.
///
/// `code`: 1 Backspace, 2 Delete, 3 left, 4 right, 5 Enter.
///
/// # Safety
/// `movie` is what `obf2_flash_open` returned.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn obf2_flash_key(movie: *mut Movie, code: i32, down: i32) {
    let Some(movie) = (unsafe { movie.as_mut() }) else { return };
    let mut player = movie.player.lock().unwrap();

    // Text control (deleting, moving the caret) Ruffle takes as a separate
    // event, not as a character.
    let control = match code {
        1 => Some(TextControlCode::Backspace),
        2 => Some(TextControlCode::Delete),
        3 => Some(TextControlCode::MoveLeft),
        4 => Some(TextControlCode::MoveRight),
        _ => None,
    };
    if let Some(control) = control {
        if down != 0 {
            player.handle_event(PlayerEvent::TextControl { code: control });
        }
        return;
    }

    let key = match code {
        5 => KeyDescriptor {
            physical_key: PhysicalKey::Enter,
            logical_key: LogicalKey::Named(ruffle_core::events::NamedKey::Enter),
            key_location: KeyLocation::Standard,
        },
        6 => KeyDescriptor {
            physical_key: PhysicalKey::Escape,
            logical_key: LogicalKey::Named(ruffle_core::events::NamedKey::Escape),
            key_location: KeyLocation::Standard,
        },
        _ => return,
    };
    let event = if down != 0 { PlayerEvent::KeyDown { key } } else { PlayerEvent::KeyUp { key } };
    player.handle_event(event);
}
