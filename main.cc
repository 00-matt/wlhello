// SPDX-FileCopyrightText: 2024 Matthew Smith <matthew@matthew.as>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "window.hh"

#include <GLES3/gl31.h>

int main() {
  Window window;
  window.make_current();

  while (!window.wants_close()) {
    window.update();
    glClearColor(1.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
  }
}
