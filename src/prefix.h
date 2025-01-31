/* Global header
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

#define CBIRD_PROGNAME "cbird"
#define CBIRD_HOMEPAGE "https://github.com/scrubbbbs/cbird"
#define INDEX_DIRNAME "_index"
#define CBIRD_DIALOG_MODS (Qt::ControlModifier) // modifier to force-show suppressed dialogs
#define CBIRD_DIALOG_KEYS "Control"

#ifdef __WIN32__
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define __STDC_FORMAT_MACROS 1
#endif

#include <QtCore/QtCore>
Q_STATIC_ASSERT_X(QT_VERSION_MAJOR == 6, "Qt " QT_VERSION_STR " is not a supported qt version");

#include <QtConcurrent/QtConcurrent>
#include <QtSql/QtSql>
#include <QtGui/QtGui>
#include <QtWidgets/QtWidgets>

using ll = QLatin1String;
using lc = QLatin1Char;

// rename QStringLiteral
#define qq(str) (QString(QtPrivate::qMakeStringPrivate(QT_UNICODE_LITERAL(str))))

// make sure asserts are always enabled, for now
#if defined(QT_NO_DEBUG) && !defined(QT_FORCE_ASSERTS)
#error QT_ASSERT() must be enabled!
#endif

// same for assert(), though I've tried to eradicate it
#ifdef NDEBUG
#error please do not disable assert()
#endif

// most classes are qobject subclasses and should not have
// copy/move constructors or default constructors
// typedefs are useful for overloads and signal/slot connections
#define NO_COPY(className, superClassName) \
  Q_DISABLE_COPY_MOVE(className)           \
  typedef superClassName super;            \
  typedef className self;

#define NO_COPY_NO_DEFAULT(className, superClassName) \
  NO_COPY(className, superClassName)                  \
  className() = delete;

// malloc/realloc that uses size of the pointer and count for allocation
#define strict_malloc(ptr, count) \
  reinterpret_cast<decltype(ptr)>(malloc(uint(count) * sizeof(*ptr)))

#define strict_realloc(ptr, count) \
  reinterpret_cast<decltype(ptr)>(realloc(ptr, uint(count) * sizeof(*ptr)))

// these types are used all over, make them available globally
typedef uint64_t dcthash_t; // 64-bit dct hash
typedef uint32_t mediaid_t; // item id in the database

// sizeof cv::Mat
#define CVMAT_SIZE(x) (x.total() * x.elemSize())

// sizeof std::vector
#define VECTOR_SIZE(x) (x.capacity() * sizeof(decltype(x)::value_type))

// experimenting with different file i/o implementations
//class SimpleIO_Stdio;
//typedef class SimpleIO_Stdio SimpleIO;

class SimpleIO_QFile;
typedef class SimpleIO_QFile SimpleIO;
