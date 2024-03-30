// SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-xdg-decoration-client-protocol.h>
#include <wayland-xdg-shell-client-protocol.h>

#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

static const char *k_title = "wlhello";
static const std::int32_t k_width = 800;
static const std::int32_t k_height = 600;

class context {
  wl_display *m_wl_display{nullptr};
  wl_registry *m_registry{nullptr};
  wl_compositor *m_compositor{nullptr};
  xdg_wm_base *m_xdg_base{nullptr};
  wl_seat *m_seat{nullptr};
  zxdg_decoration_manager_v1 *m_xdg_dm{nullptr};
  wl_surface *m_wl_surface{nullptr};
  xdg_surface *m_xdg_surface{nullptr};
  xdg_toplevel *m_xdg_toplevel{nullptr};
  zxdg_toplevel_decoration_v1 *m_xdg_toplevel_decor{nullptr};
  wl_keyboard *m_wl_keyboard{nullptr};
  bool m_wants_close{false};
  std::int32_t m_width{k_width};
  std::int32_t m_height{k_height};
  wl_region *m_region{nullptr};
  wl_egl_window *m_egl_window{nullptr};
  EGLDisplay m_egl_display{nullptr};
  EGLSurface m_egl_surface{nullptr};
  EGLConfig m_egl_context{nullptr};
  xkb_state *m_xkb_state{nullptr};
  xkb_context *m_xkb_context{nullptr};
  xkb_keymap *m_xkb_keymap{nullptr};

  static void cb_registry_global_add(void *context_ptr, wl_registry *registry,
                                     std::uint32_t name, const char *interface,
                                     std::uint32_t version);

  static void cb_xdg_base_ping(void *context_ptr, xdg_wm_base *base,
                               std::uint32_t serial);

  static void cb_seat_capabilities(void *context_ptr, wl_seat *seat,
                                   std::uint32_t capabilities);

  static void cb_seat_name(void *context_ptr, wl_seat *seat, const char *name);

  static void cb_xdg_surface_configure(void *context_ptr,
                                       xdg_surface *xdg_surface,
                                       std::uint32_t serial);

  static void cb_xdg_toplevel_configure(void *context_ptr,
                                        xdg_toplevel *toplevel,
                                        std::int32_t width, std::int32_t height,
                                        wl_array *states);

  static void cb_xdg_toplevel_close(void *context_ptr, xdg_toplevel *toplevel);

  static void cb_kb_map(void *context_ptr, wl_keyboard *keyboard,
                        std::uint32_t format, std::int32_t fd,
                        std::uint32_t size);

  static void cb_kb_enter(void *context_ptr, wl_keyboard *keyboard,
                          std::uint32_t serial, wl_surface *surface,
                          wl_array *keys);

  static void cb_kb_leave(void *context_ptr, wl_keyboard *keyboard,
                          std::uint32_t serial, wl_surface *surface);

  static void cb_kb_key(void *context_ptr, wl_keyboard *keyboard,
                        std::uint32_t serial, std::uint32_t time,
                        std::uint32_t key, std::uint32_t state);

  static void cb_kb_modifiers(void *context_ptr, wl_keyboard *keyboard,
                              std::uint32_t serial,
                              std::uint32_t mods_depressed,
                              std::uint32_t mods_latched,
                              std::uint32_t mods_locked, std::uint32_t group);

  static void cb_kb_repeat_info(void *context_ptr, wl_keyboard *keyboard,
                                std::int32_t rate, std::int32_t delay);

  void resize(std::int32_t width, std::int32_t height);

public:
  context();
  context(const context &) = delete;
  context(context &&) = delete;
  ~context();

  void update();

  bool wants_close() const { return m_wants_close; }
  std::int32_t width() const { return m_width; }
  std::int32_t height() const { return m_height; }
};

context::context() {
  // Connect to server.
  m_wl_display = wl_display_connect(nullptr);
  if (!m_wl_display) {
    throw std::runtime_error("wl_display_connect");
  }

  // Bind globals.
  m_registry = wl_display_get_registry(m_wl_display);
  if (!m_registry) {
    throw std::runtime_error("wl_display_get_registry");
  }
  static const wl_registry_listener registry_listener = {cb_registry_global_add,
                                                         nullptr};
  wl_registry_add_listener(m_registry, &registry_listener, this);
  wl_display_dispatch(m_wl_display);
  wl_display_roundtrip(m_wl_display);
  if (!m_compositor) {
    throw std::runtime_error("wl_registry_bind(wl_compositor)");
  }
  if (!m_xdg_base) {
    throw std::runtime_error("wl_registry_bind(xdg_wm_base)");
  }

  //
  m_wl_surface = wl_compositor_create_surface(m_compositor);
  if (!m_wl_surface) {
    throw std::runtime_error("wl_compositor_create_surface");
  }
  m_xdg_surface = xdg_wm_base_get_xdg_surface(m_xdg_base, m_wl_surface);
  if (!m_xdg_surface) {
    throw std::runtime_error("xdg_wm_base_get_xdg_surface");
  }
  static const xdg_surface_listener xdg_surface_listener = {
      cb_xdg_surface_configure};
  xdg_surface_add_listener(m_xdg_surface, &xdg_surface_listener, this);
  m_xdg_toplevel = xdg_surface_get_toplevel(m_xdg_surface);
  if (!m_xdg_toplevel) {
    throw std::runtime_error("xdg_surface_get_toplevel");
  }
  xdg_toplevel_set_title(m_xdg_toplevel, k_title);
  static const xdg_toplevel_listener xdg_toplevel_listener = {
      cb_xdg_toplevel_configure, cb_xdg_toplevel_close, nullptr, nullptr};
  xdg_toplevel_add_listener(m_xdg_toplevel, &xdg_toplevel_listener, this);
  if (m_xdg_dm) {
    // If this interface isn't present, do nothing.
    // TODO: Log a warning.
    m_xdg_toplevel_decor = zxdg_decoration_manager_v1_get_toplevel_decoration(
        m_xdg_dm, m_xdg_toplevel);
    zxdg_toplevel_decoration_v1_set_mode(
        m_xdg_toplevel_decor, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }
  wl_surface_commit(m_wl_surface);

  // Create window.
  m_width = k_width;
  m_height = k_height;
  m_region = wl_compositor_create_region(m_compositor);
  if (!m_region) {
    throw std::runtime_error("wl_compositor_create_region");
  }
  wl_region_add(m_region, 0, 0, m_width, m_height);
  wl_surface_set_opaque_region(m_wl_surface, m_region);
  m_egl_window = wl_egl_window_create(m_wl_surface, m_width, m_height);
  if (!m_egl_window) {
    throw std::runtime_error("wl_egl_window_create");
  }

  // Create xkb context.
  m_xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!m_xkb_context) {
    throw std::runtime_error("xkb_context_new");
  }

  // Create EGL context.
  m_egl_display = eglGetDisplay(m_wl_display);
  if (!m_egl_display) {
    throw std::runtime_error("eglGetDisplay");
  }
  EGLint egl_major;
  EGLint egl_minor;
  if (!eglInitialize(m_egl_display, &egl_major, &egl_minor)) {
    throw std::runtime_error("eglInitialize");
  }
  static const EGLint egl_attrs[] = {
      EGL_RED_SIZE,  8, EGL_GREEN_SIZE,      8,
      EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE};
  EGLint num_configs;
  EGLConfig egl_config;
  if (!eglChooseConfig(m_egl_display, egl_attrs, &egl_config, 1,
                       &num_configs)) {
    throw std::runtime_error("eglChooseConfig");
  }
  m_egl_surface =
      eglCreateWindowSurface(m_egl_display, egl_config, m_egl_window, nullptr);
  static const EGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  m_egl_context =
      eglCreateContext(m_egl_display, egl_config, EGL_NO_CONTEXT, ctx_attrs);
  if (!m_egl_context) {
    throw std::runtime_error("eglCreateContext");
  }
  if (!eglMakeCurrent(m_egl_display, m_egl_surface, m_egl_surface,
                      m_egl_context)) {
    throw std::runtime_error("eglMakeCurrent");
  }
}

context::~context() {
  eglDestroyContext(m_egl_display, m_egl_context);
  eglDestroySurface(m_egl_display, m_egl_surface);
  eglTerminate(m_egl_display);
  xkb_keymap_unref(m_xkb_keymap);
  xkb_state_unref(m_xkb_state);
  xkb_context_unref(m_xkb_context);
  wl_egl_window_destroy(m_egl_window);
  wl_region_destroy(m_region);
  wl_keyboard_release(m_wl_keyboard);
  zxdg_toplevel_decoration_v1_destroy(m_xdg_toplevel_decor);
  xdg_toplevel_destroy(m_xdg_toplevel);
  xdg_surface_destroy(m_xdg_surface);
  zxdg_decoration_manager_v1_destroy(m_xdg_dm);
  wl_seat_destroy(m_seat);
  xdg_wm_base_destroy(m_xdg_base);
  wl_surface_destroy(m_wl_surface);
  wl_compositor_destroy(m_compositor);
  wl_registry_destroy(m_registry);
  wl_display_disconnect(m_wl_display);
}

void context::cb_registry_global_add(void *context_ptr, wl_registry *registry,
                                     std::uint32_t id, const char *interface_cs,
                                     std::uint32_t) {
  auto thiz = static_cast<context *>(context_ptr);
  std::string_view interface = interface_cs;
  if (interface == wl_compositor_interface.name) {
    thiz->m_compositor = static_cast<wl_compositor *>(
        wl_registry_bind(registry, id, &wl_compositor_interface, 1));
  } else if (interface == xdg_wm_base_interface.name) {
    thiz->m_xdg_base = static_cast<xdg_wm_base *>(
        wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
    static const xdg_wm_base_listener xdg_base_listener{cb_xdg_base_ping};
    xdg_wm_base_add_listener(thiz->m_xdg_base, &xdg_base_listener, context_ptr);
  } else if (interface == wl_seat_interface.name) {
    static const wl_seat_listener wl_seat_listener{cb_seat_capabilities,
                                                   cb_seat_name};
    thiz->m_seat = static_cast<wl_seat *>(
        wl_registry_bind(registry, id, &wl_seat_interface, 7));
    wl_seat_add_listener(thiz->m_seat, &wl_seat_listener, context_ptr);
  } else if (interface == zxdg_decoration_manager_v1_interface.name) {
    thiz->m_xdg_dm = static_cast<zxdg_decoration_manager_v1 *>(wl_registry_bind(
        registry, id, &zxdg_decoration_manager_v1_interface, 1));
  }
}

void context::cb_xdg_base_ping(void *, xdg_wm_base *base,
                               std::uint32_t serial) {
  xdg_wm_base_pong(base, serial);
}

void context::cb_seat_capabilities(void *context_ptr, wl_seat *,
                                   std::uint32_t caps) {
  auto thiz = static_cast<context *>(context_ptr);
  const bool has_keyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;

  if (has_keyboard && thiz->m_wl_keyboard == nullptr) {
    thiz->m_wl_keyboard = wl_seat_get_keyboard(thiz->m_seat);
    static const wl_keyboard_listener wl_keyboard_listener{
        cb_kb_map, cb_kb_enter,     cb_kb_leave,
        cb_kb_key, cb_kb_modifiers, cb_kb_repeat_info};
    wl_keyboard_add_listener(thiz->m_wl_keyboard, &wl_keyboard_listener,
                             context_ptr);
  } else if (!has_keyboard && thiz->m_wl_keyboard != nullptr) {
    wl_keyboard_release(std::exchange(thiz->m_wl_keyboard, nullptr));
  }
}

void context::cb_seat_name(void *, wl_seat *, const char *) {}

void context::cb_xdg_surface_configure(void *, xdg_surface *xdg_surface,
                                       std::uint32_t serial) {
  xdg_surface_ack_configure(xdg_surface, serial);
}

void context::cb_xdg_toplevel_configure(void *context_ptr, xdg_toplevel *,
                                        std::int32_t width, std::int32_t height,
                                        wl_array *) {
  auto thiz = static_cast<context *>(context_ptr);
  thiz->resize(width, height);
}

void context::cb_xdg_toplevel_close(void *context_ptr, xdg_toplevel *) {
  auto thiz = static_cast<context *>(context_ptr);
  thiz->m_wants_close = true;
}

void context::cb_kb_map(void *context_ptr, wl_keyboard *,
                        std::uint32_t /* format */, std::int32_t fd,
                        std::uint32_t size) {
  // TODO(correctness): Check format is WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1.
  // TODO(correctness): Check mmap success.
  auto thiz = static_cast<context *>(context_ptr);

  void *shm = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
      thiz->m_xkb_context, static_cast<const char *>(shm),
      XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(shm, size);
  close(fd);

  xkb_state *xkb_state = xkb_state_new(xkb_keymap);
  xkb_keymap_unref(thiz->m_xkb_keymap);
  xkb_state_unref(thiz->m_xkb_state);
  thiz->m_xkb_keymap = xkb_keymap;
  thiz->m_xkb_state = xkb_state;
}

void context::cb_kb_enter(void *context_ptr, wl_keyboard *, std::uint32_t,
                          wl_surface *, wl_array *key_array) {
  auto thiz = static_cast<context *>(context_ptr);

  const std::span<std::uint32_t> keys(
      static_cast<std::uint32_t *>(key_array->data),
      key_array->size / sizeof(std::uint32_t));
  for (auto &key : keys) {
    // Add 8 to convert from an evdev scancode to an xkb scancode.
    const xkb_keysym_t sym =
        xkb_state_key_get_one_sym(thiz->m_xkb_state, key + 8);
    char buf[128];
    xkb_keysym_get_name(sym, buf, sizeof(buf));
    std::cout << "pressed  " << buf << '\n';
  }
}

void context::cb_kb_leave(void *, wl_keyboard *, std::uint32_t, wl_surface *) {}

void context::cb_kb_key(void *context_ptr, wl_keyboard *, std::uint32_t,
                        std::uint32_t, std::uint32_t key, std::uint32_t state) {
  auto thiz = static_cast<context *>(context_ptr);

  // Add 8 to convert from an evdev scancode to an xkb scancode.
  const xkb_keysym_t sym =
      xkb_state_key_get_one_sym(thiz->m_xkb_state, key + 8);
  char buf[128];
  xkb_keysym_get_name(sym, buf, sizeof(buf));

  const char *action =
      state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed  " : "released ";
  std::cout << action << buf << '\n';
}

void context::cb_kb_modifiers(void *context_ptr, wl_keyboard *, std::uint32_t,
                              std::uint32_t mods_depressed,
                              std::uint32_t mods_latched,
                              std::uint32_t mods_locked, std::uint32_t group) {
  auto thiz = static_cast<context *>(context_ptr);
  xkb_state_update_mask(thiz->m_xkb_state, mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);
}

void context::cb_kb_repeat_info(void *, wl_keyboard *, std::int32_t,
                                std::int32_t) {}

void context::resize(std::int32_t width, std::int32_t height) {
  if (width == m_width && height == m_height) {
    return;
  }

  m_width = width;
  m_height = height;

  if (m_egl_window) {
    wl_egl_window_resize(m_egl_window, m_width, m_height, 0, 0);
  }
  wl_surface_commit(m_wl_surface);
}

void context::update() {
  wl_display_dispatch_pending(m_wl_display);
  eglSwapBuffers(m_egl_display, m_egl_surface);
}

int main() {
  context context;

  while (!context.wants_close()) {
    context.update();
    glClearColor(1.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}
