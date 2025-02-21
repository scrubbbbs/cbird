/* Macros for Params subclasses
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
#pragma once

// macros to return functions to read/write directly into struct members,
// I guess because I like the idea of referencing struct members directly

// TODO: if we give up on accessing struct members directly,
// Q_PROPERTY might be a decent replacement
#define SET_ENUM(arg, member, values) \
  [](Params* p, const QVariant& v) { \
    return Value::setEnum(v, values, arg, ((PARAMS_CLASS*) p)->member); \
  }

#define SET_FLAGS(arg, member, values) \
  [](Params* p, const QVariant& v) { \
    return Value::setFlags(v, values, arg, ((PARAMS_CLASS*) p)->member); \
  }

#define SET_INT(member) \
  [](Params* p, const QVariant& v) { \
    ((PARAMS_CLASS*) p)->member = v.toInt(); \
    return true; \
  }

#define SET_BOOL(member) \
  [](Params* p, const QVariant& v) { \
    ((PARAMS_CLASS*) p)->member = v.toBool(); \
    return true; \
  }

#define GET(member) [](const Params* p) { return ((const PARAMS_CLASS*) p)->member; }

#define GET_CONST(global) []() -> const decltype(global)& { return global; }

#define NO_NAMES GET_CONST(emptyValues)

#define NO_RANGE GET_CONST(emptyRange)

#define ADD_GLOB(member) \
  [](Params* p, const QVariant& v) { \
    ((PARAMS_CLASS*) p)->member.append(v.toString()); \
    return true; \
  }

#define ADD_STRING(member) \
  [](Params* p, const QVariant& v) { \
    ((PARAMS_CLASS*) p)->member.append(v.toString()); \
    return true; \
  }
