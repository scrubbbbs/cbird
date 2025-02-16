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
#pragma once

#include <QtWidgets/QLabel>

/// obscure parent to emphasize foreground
class ShadeWidget : public QLabel {
  NO_COPY_NO_DEFAULT(ShadeWidget, QLabel);
  Q_OBJECT
 public:
  ShadeWidget(QWidget* parent);
  virtual ~ShadeWidget();
};
