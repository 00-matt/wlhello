// SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

struct wl_array;
struct wl_compositor;
struct wl_display;
struct wl_egl_window;
struct wl_keyboard;
struct wl_region;
struct wl_registry;
struct wl_seat;
struct wl_surface;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;

using EGLContext = void *;
using EGLDisplay = void *;
using EGLSurface = void *;

class Window {
  wl_display *m_display{nullptr};

  // wayland globals
  wl_registry *m_registry{nullptr};
  wl_compositor *m_compositor{nullptr};
  wl_seat *m_seat{nullptr};
  xdg_wm_base *m_wm_base{nullptr};
  zxdg_decoration_manager_v1 *m_decoration_manager{nullptr};

  // other wayland objects
  wl_region *m_region{nullptr};
  wl_keyboard *m_keyboard{nullptr};
  wl_surface *m_surface{nullptr};
  xdg_surface *m_xdg_surface{nullptr};
  xdg_toplevel *m_xdg_toplevel{nullptr};
  zxdg_toplevel_decoration_v1 *m_toplevel_decoration{nullptr};

  // xkbcommon
  xkb_state *m_xkb_state{nullptr};
  xkb_context *m_xkb_context{nullptr};
  xkb_keymap *m_xkb_keymap{nullptr};

  // EGL
  wl_egl_window *m_egl_window{nullptr};
  EGLDisplay m_egl_display{nullptr};
  EGLSurface m_egl_surface{nullptr};
  EGLContext m_egl_context{nullptr};

  std::int32_t m_width{0};
  std::int32_t m_height{0};
  bool m_wants_close{false};

  // wl_registry callbacks
  static void on_registry_global(void *, wl_registry *, std::uint32_t,
                                 const char *, std::uint32_t) noexcept;
  static void on_registry_global_remove(void *, wl_registry *,
                                        std::uint32_t) noexcept;

  // wl_seat callbacks
  static void on_seat_capabilities(void *, wl_seat *, std::uint32_t) noexcept;
  static void on_seat_name(void *, wl_seat *, const char *) noexcept;

  // wl_xdg_surface callbacks
  static void on_xdg_surface_configure(void *, xdg_surface *,
                                       std::uint32_t) noexcept;

  // wl_xdg_toplevel callbacks
  static void on_xdg_toplevel_configure(void *, xdg_toplevel *, std::int32_t,
                                        std::int32_t, wl_array *) noexcept;
  static void on_xdg_toplevel_close(void *, xdg_toplevel *) noexcept;

  // wl_keyboard callbacks
  static void on_keyboard_map(void *, wl_keyboard *, std::uint32_t,
                              std::int32_t, std::uint32_t) noexcept;
  static void on_keyboard_enter(void *, wl_keyboard *, std::uint32_t,
                                wl_surface *, wl_array *) noexcept;
  static void on_keyboard_leave(void *, wl_keyboard *, std::uint32_t,
                                wl_surface *) noexcept;
  static void on_keyboard_key(void *, wl_keyboard *, std::uint32_t,
                              std::uint32_t, std::uint32_t,
                              std::uint32_t) noexcept;
  static void on_keyboard_mod(void *, wl_keyboard *, std::uint32_t,
                              std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t) noexcept;
  static void on_keyboard_repeat_info(void *, wl_keyboard *, std::int32_t,
                                      std::int32_t) noexcept;

  // xdg_wm_base_interface callbacks
  static void on_wm_base_ping(void *, xdg_wm_base *, std::uint32_t) noexcept;

public:
  Window();
  Window(const Window *) = delete;
  Window(Window &&) = delete;
  ~Window();

  void make_current();
  void update();

  std::int32_t width() const { return m_width; };
  std::int32_t height() const { return m_height; };
  bool wants_close() const { return m_wants_close; }
};
