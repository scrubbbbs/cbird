/* Dialog shade hack 
   Copyright (C) 2023 scrubbbbs
   Contact: screubbbebs@gemeaile.com =~ s/e//g
   Project: https://github.com/scrubbbbs/cbird

This file is part of cbird.

cbird is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

cbird is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public
License along with cbird; if not, see
<https://www.gnu.org/licenses/>.  */
#include "shadewidget.h"
#include "theme.h"

ShadeWidget::ShadeWidget(QWidget* parent)
    : QLabel(parent) {
#ifndef QT_TESTLIB_LIB                                       // theme dep in unit tests
  setProperty("style", Theme::instance().property("style")); // stylesheet sets transparent bg color
#endif
  setGeometry({0, 0, parent->width(), parent->height()});
  setMargin(0);
  setFrameShape(QFrame::NoFrame);
  // prevent stacking of effect; note it will still stack with
  // the window manager's effect (os x, kde)
  if (!parent->property("shaded").toBool()) {
    parent->setProperty("shaded", true);
    show();
  }
}

ShadeWidget::~ShadeWidget() {
  if (!isHidden()) parent()->setProperty("shaded", false);
}
