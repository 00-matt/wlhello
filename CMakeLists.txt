# SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.29)
project(wlhello C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(OpenGL REQUIRED COMPONENTS EGL GLES3)
find_package(Wayland REQUIRED COMPONENTS client egl protocols scanner)

add_executable(wlhello main.cc)
wayland_client_protocol_add(wlhello
  PROTOCOL "${Wayland_protocols_dir}/stable/xdg-shell/xdg-shell.xml"
  BASENAME xdg-shell)
target_link_libraries(wlhello PRIVATE
  OpenGL::EGL
  OpenGL::GLES3
  Wayland::client
  Wayland::egl)
