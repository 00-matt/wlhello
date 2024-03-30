// SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.hh"

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-util.h>
#include <wayland-xdg-decoration-client-protocol.h>
#include <wayland-xdg-shell-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <EGL/egl.h> // must be included after wayland-egl.h

#include <span>
#include <stdexcept>
#include <string_view> // IWYU pragma: no_include <string>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

// TODO: Make parameter to Window::Window.
static const char *k_title = "wlhello";
static const std::int32_t k_width = 800;
static const std::int32_t k_height = 600;

Window::Window() {
  // Connect to display.
  m_display = wl_display_connect(nullptr);
  if (!m_display) {
    throw std::runtime_error(
        "wl_display_connect: failed to connect to display");
  }

  // Get registry and bind globals.
  m_registry = wl_display_get_registry(m_display);
  static const wl_registry_listener registry_listener{
      on_registry_global, on_registry_global_remove};
  wl_registry_add_listener(m_registry, &registry_listener, this);
  wl_display_dispatch(m_display);
  wl_display_roundtrip(m_display);

  // Check for required globals.
  if (!m_compositor) {
    throw std::runtime_error("wl_compositor: failed to bind global");
  }
  if (!m_seat) {
    throw std::runtime_error("wl_seat: failed to bind global");
  }
  if (!m_wm_base) {
    throw std::runtime_error("xdg_wm_base: failed to bind global");
  }
  // zxdg_decoration_manager_v1 is optional.

  // Create surface.
  m_surface = wl_compositor_create_surface(m_compositor);
  if (!m_surface) {
    throw std::runtime_error("wl_surface: failed to create surface");
  }
  m_xdg_surface = xdg_wm_base_get_xdg_surface(m_wm_base, m_surface);
  if (!m_xdg_surface) {
    throw std::runtime_error("xdg_surface: failed to get surface");
  }
  static const xdg_surface_listener xdg_surface_listener{
      on_xdg_surface_configure};
  xdg_surface_add_listener(m_xdg_surface, &xdg_surface_listener, this);
  m_xdg_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
  if (!m_xdg_toplevel) {
    throw std::runtime_error("xdg_toplevel: failed to get toplevel");
  }
  xdg_toplevel_set_title(m_xdg_toplevel, k_title);
  static const xdg_toplevel_listener xdg_toplevel_listener{
      on_xdg_toplevel_configure, on_xdg_toplevel_close, nullptr, nullptr};
  xdg_toplevel_add_listener(m_xdg_toplevel, &xdg_toplevel_listener, this);

  // If decoration manager protocol is supported, enable server-side
  // decoration.
  if (m_decoration_manager) {
    m_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
        m_decoration_manager, m_xdg_toplevel);
    zxdg_toplevel_decoration_v1_set_mode(
        m_toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }

  wl_surface_commit(m_surface);

  // Create a window.
  m_width = k_width;
  m_height = k_height;
  m_region = wl_compositor_create_region(m_compositor);
  if (!m_region) {
    throw std::runtime_error("wl_region: failed to create region");
  }
  wl_region_add(m_region, 0, 0, m_width, m_height);
  wl_surface_set_opaque_region(m_surface, m_region);

  // Create an xkb context.
  m_xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!m_xkb_context) {
    throw std::runtime_error("xkb_context_new: failed to create context");
  }

  // Create EGL context.
  m_egl_window = wl_egl_window_create(m_surface, m_width, m_height);
  if (!m_egl_window) {
    throw std::runtime_error("wl_egl_window: failed to create window");
  }
  m_egl_display = eglGetDisplay(m_display);
  if (!m_egl_display) {
    throw std::runtime_error("egl_display: failed to get display");
  }
  EGLint egl_major;
  EGLint egl_minor;
  if (!eglInitialize(m_egl_display, &egl_major, &egl_minor)) {
    throw std::runtime_error("egl: failed to initialise");
  }
  static const EGLint egl_attrs[] = {
      EGL_RED_SIZE,  8, EGL_GREEN_SIZE,      8,
      EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE};
  EGLint num_configs;
  EGLConfig egl_config;
  if (!eglChooseConfig(m_egl_display, egl_attrs, &egl_config, 1,
                       &num_configs)) {
    throw std::runtime_error("egl_config: failed to choose config");
  }
  m_egl_surface =
      eglCreateWindowSurface(m_egl_display, egl_config, m_egl_window, nullptr);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  m_egl_context =
      eglCreateContext(m_egl_display, egl_config, EGL_NO_CONTEXT, ctx_attrs);
  if (!m_egl_context) {
    throw std::runtime_error("egl_context: failed to create context");
  }
}

Window::~Window() {
  // EGL
  eglDestroyContext(m_egl_display, m_egl_context);
  eglDestroySurface(m_egl_display, m_egl_surface);
  eglTerminate(m_egl_display);
  wl_egl_window_destroy(m_egl_window);

  // xkbcommon
  xkb_keymap_unref(m_xkb_keymap);
  xkb_state_unref(m_xkb_state);
  xkb_context_unref(m_xkb_context);

  // other wayland objects
  zxdg_toplevel_decoration_v1_destroy(m_toplevel_decoration);
  xdg_toplevel_destroy(m_xdg_toplevel);
  xdg_surface_destroy(m_xdg_surface);
  wl_surface_destroy(m_surface);
  wl_keyboard_release(m_keyboard);
  wl_region_destroy(m_region);

  // wayland globals
  zxdg_decoration_manager_v1_destroy(m_decoration_manager);
  xdg_wm_base_destroy(m_wm_base);
  wl_seat_destroy(m_seat);
  wl_compositor_destroy(m_compositor);
  wl_registry_destroy(m_registry);

  wl_display_disconnect(m_display);
}

void Window::on_registry_global(void *window_ptr, wl_registry *registry,
                                std::uint32_t id, const char *interface_ptr,
                                std::uint32_t /* name */) noexcept {
  auto &window = *static_cast<Window *>(window_ptr);
  std::string_view interface = interface_ptr;

  if (interface == wl_compositor_interface.name) {
    window.m_compositor = static_cast<wl_compositor *>(
        wl_registry_bind(registry, id, &wl_compositor_interface, 1));
  } else if (interface == xdg_wm_base_interface.name) {
    window.m_wm_base = static_cast<xdg_wm_base *>(
        wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
    static const xdg_wm_base_listener xdg_base_listener{on_wm_base_ping};
    xdg_wm_base_add_listener(window.m_wm_base, &xdg_base_listener, window_ptr);
  } else if (interface == wl_seat_interface.name) {
    static const wl_seat_listener wl_seat_listener{on_seat_capabilities,
                                                   on_seat_name};
    window.m_seat = static_cast<wl_seat *>(
        wl_registry_bind(registry, id, &wl_seat_interface, 7));
    wl_seat_add_listener(window.m_seat, &wl_seat_listener, window_ptr);
  } else if (interface == zxdg_decoration_manager_v1_interface.name) {
    window.m_decoration_manager =
        static_cast<zxdg_decoration_manager_v1 *>(wl_registry_bind(
            registry, id, &zxdg_decoration_manager_v1_interface, 1));
  }
}

void Window::on_registry_global_remove(void * /* window_ptr */,
                                       wl_registry * /* registry */,
                                       std::uint32_t /* name */) noexcept {}

void Window::on_seat_capabilities(void *window_ptr, wl_seat *seat,
                                  std::uint32_t capabilities) noexcept {
  auto &window = *static_cast<Window *>(window_ptr);
  const bool had_keyboard = window.m_keyboard != nullptr;
  const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
  if (has_keyboard && !had_keyboard) {
    window.m_keyboard = wl_seat_get_keyboard(seat);
    static const wl_keyboard_listener wl_keyboard_listener{
        on_keyboard_map, on_keyboard_enter, on_keyboard_leave,
        on_keyboard_key, on_keyboard_mod,   on_keyboard_repeat_info};
    wl_keyboard_add_listener(window.m_keyboard, &wl_keyboard_listener,
                             window_ptr);
  } else if (!has_keyboard && had_keyboard) {
    wl_keyboard_release(std::exchange(window.m_keyboard, nullptr));
  }
}

void Window::on_xdg_surface_configure(void *, xdg_surface *xdg_surface,
                                      std::uint32_t serial) noexcept {
  xdg_surface_ack_configure(xdg_surface, serial);
}

void Window::on_xdg_toplevel_configure(void *, xdg_toplevel *, std::int32_t,
                                       std::int32_t, wl_array *) noexcept {}

void Window::on_xdg_toplevel_close(void *window_ptr, xdg_toplevel *) noexcept {
  auto &window = *static_cast<Window *>(window_ptr);
  window.m_wants_close = true;
}

void Window::on_keyboard_map(void *window_ptr, wl_keyboard * /* keyboard */,
                             std::uint32_t /* format */, std::int32_t fd,
                             std::uint32_t size) noexcept {
  // TODO(correctness): Check format is WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1.
  // TODO(correctness): Check mmap success.

  auto &window = *static_cast<Window *>(window_ptr);

  void *shm = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
      window.m_xkb_context, static_cast<const char *>(shm),
      XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(shm, size);
  close(fd);

  xkb_state *xkb_state = xkb_state_new(xkb_keymap);
  xkb_keymap_unref(window.m_xkb_keymap);
  xkb_state_unref(window.m_xkb_state);
  window.m_xkb_keymap = xkb_keymap;
  window.m_xkb_state = xkb_state;
}

void Window::on_keyboard_enter(void *window_ptr, wl_keyboard *,
                               std::uint32_t /* serial */,
                               wl_surface * /* surface */,
                               wl_array *keys_array) noexcept {
  auto &window = *static_cast<Window *>(window_ptr);

  const std::span<std::uint32_t> keys(
      static_cast<std::uint32_t *>(keys_array->data),
      keys_array->size / sizeof(std::uint32_t));
  for (auto key : keys) {
    // Add 8 to convert from an evdev scancode to an xkb scancode.
    const xkb_keysym_t sym =
        xkb_state_key_get_one_sym(window.m_xkb_state, key + 8);

    // TODO:
    (void)sym;
  }
}

void Window::on_keyboard_leave(void * /* window_ptr */,
                               wl_keyboard * /* keyboard */,
                               std::uint32_t /* serial */,
                               wl_surface * /* surface */) noexcept {
  // TODO: Mark all keys as released.
}

void Window::on_keyboard_key(void *window_ptr, wl_keyboard *,
                             std::uint32_t /* serial */, std::uint32_t,
                             std::uint32_t key, std::uint32_t state) noexcept {
  // Add 8 to convert from an evdev scancode to an xkb scancode.
  auto &window = *static_cast<Window *>(window_ptr);

  const xkb_keysym_t sym =
      xkb_state_key_get_one_sym(window.m_xkb_state, key + 8);
  const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;

  // TODO:
  (void)sym;
  (void)pressed;
}

void Window::on_keyboard_mod(void *window_ptr, wl_keyboard * /* keyboard */,
                             std::uint32_t /* serial */,
                             std::uint32_t mods_depressed,
                             std::uint32_t mods_latched,
                             std::uint32_t mods_locked,
                             std::uint32_t group) noexcept {
  auto &window = *static_cast<Window *>(window_ptr);
  xkb_state_update_mask(window.m_xkb_state, mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);
}

void Window::on_keyboard_repeat_info(void * /* window_ptr */,
                                     wl_keyboard * /* keyboard */,
                                     std::int32_t /* rate */,
                                     std::int32_t /* delay */) noexcept {
  // TODO: Store rate and delay for application use.
}

void Window::on_seat_name(void * /* window_ptr */, wl_seat * /* seat */,
                          const char * /* name */) noexcept {}

void Window::on_wm_base_ping(void * /* window_ptr */, xdg_wm_base *wm_base,
                             std::uint32_t serial) noexcept {
  xdg_wm_base_pong(wm_base, serial);
}

void Window::make_current() {
  if (!eglMakeCurrent(m_egl_display, m_egl_surface, m_egl_surface,
                      m_egl_context)) {
    throw std::runtime_error("eglMakeCurrent");
  }
}

void Window::update() {
  wl_display_dispatch_pending(m_display);
  eglSwapBuffers(m_egl_display, m_egl_surface);
}
