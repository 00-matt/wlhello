// SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-xdg-shell-client-protocol.h>

#include <EGL/egl.h>
#include <GLES3/gl31.h>

#include <cstdint>
#include <stdexcept>
#include <string_view>

static const char *k_title = "wlhello";
static const std::int32_t k_width = 800;
static const std::int32_t k_height = 600;

class context {
  wl_display *m_wl_display;
  wl_registry *m_registry;
  wl_compositor *m_compositor;
  xdg_wm_base *m_xdg_base;
  wl_surface *m_wl_surface;
  xdg_surface *m_xdg_surface;
  xdg_toplevel *m_xdg_toplevel;
  bool m_wants_close;
  std::int32_t m_width;
  std::int32_t m_height;
  wl_region *m_region;
  wl_egl_window *m_egl_window;
  EGLDisplay m_egl_display;
  EGLSurface m_egl_surface;
  EGLConfig m_egl_context;

  static void cb_registry_global_add(void *context_ptr, wl_registry *registry,
                                     std::uint32_t name, const char *interface,
                                     std::uint32_t version);

  static void cb_xdg_base_ping(void *context_ptr, xdg_wm_base *base,
                               std::uint32_t serial);

  static void cb_xdg_surface_configure(void *context_ptr,
                                       xdg_surface *xdg_surface,
                                       std::uint32_t serial);

  static void cb_xdg_toplevel_configure(void *context_ptr,
                                        xdg_toplevel *toplevel,
                                        std::int32_t width, std::int32_t height,
                                        wl_array *states);

  static void cb_xdg_toplevel_close(void *context_ptr, xdg_toplevel *toplevel);

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
  wl_egl_window_destroy(m_egl_window);
  wl_region_destroy(m_region);
  xdg_toplevel_destroy(m_xdg_toplevel);
  xdg_surface_destroy(m_xdg_surface);
  xdg_wm_base_destroy(m_xdg_base);
  wl_surface_destroy(m_wl_surface);
  wl_compositor_destroy(m_compositor);
  wl_registry_destroy(m_registry);
  wl_display_disconnect(m_wl_display);
}

void context::cb_registry_global_add(void *context_ptr, wl_registry *registry,
                                     std::uint32_t id, const char *interface_cs,
                                     std::uint32_t) {
  context *thiz = reinterpret_cast<context *>(context_ptr);
  std::string_view interface = interface_cs;
  if (interface == "wl_compositor") {
    thiz->m_compositor = reinterpret_cast<wl_compositor *>(
        wl_registry_bind(registry, id, &wl_compositor_interface, 1));
  } else if (interface == xdg_wm_base_interface.name) {
    thiz->m_xdg_base = reinterpret_cast<xdg_wm_base *>(
        wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
    static const xdg_wm_base_listener xdg_base_listener{cb_xdg_base_ping};
    xdg_wm_base_add_listener(thiz->m_xdg_base, &xdg_base_listener, context_ptr);
  }
}

void context::cb_xdg_base_ping(void *, xdg_wm_base *base,
                               std::uint32_t serial) {
  xdg_wm_base_pong(base, serial);
}

void context::cb_xdg_surface_configure(void *, xdg_surface *xdg_surface,
                                       std::uint32_t serial) {
  xdg_surface_ack_configure(xdg_surface, serial);
}

void context::cb_xdg_toplevel_configure(void *context_ptr, xdg_toplevel *,
                                        std::int32_t width, std::int32_t height,
                                        wl_array *) {
  context *thiz = reinterpret_cast<context *>(context_ptr);
  thiz->resize(width, height);
}

void context::cb_xdg_toplevel_close(void *context_ptr, xdg_toplevel *) {
  context *thiz = reinterpret_cast<context *>(context_ptr);
  thiz->m_wants_close = true;
}

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
