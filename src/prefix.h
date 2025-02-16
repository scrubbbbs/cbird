/* Precompiled header
   Copyright (C) 2021 scrubbbbs
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
#define HAVE_PCH

#ifdef __WIN32__
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define __STDC_FORMAT_MACROS 1
#endif

#define SLIM_PCH
//#define FAT_PCH

#ifdef SLIM_PCH

#include <QtCore/QElapsedTimer> // qtutil.h
#include <QtCore/QVariant>      // params.h
#include <QtGui/QImage>         // media.h

#endif

#ifdef FAT_PCH

#include <QtCore/QtCore>

#include <QtConcurrent/QtConcurrent>
#include <QtSql/QtSql>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>

#endif

Q_STATIC_ASSERT_X(QT_VERSION_MAJOR == 6, "Qt " QT_VERSION_STR " is not a supported qt version");

#include "global.h"
