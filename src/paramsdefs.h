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

#define SET_ENUM(arg, member, values)                    \
  [this](const QVariant& v) {                            \
    return Value::setEnum(v, values, arg, this->member); \
  }

#define SET_FLAGS(arg, member, values)                    \
  [this](const QVariant& v) {                             \
    return Value::setFlags(v, values, arg, this->member); \
  }

#define SET_INT(member)       \
  [this](const QVariant& v) { \
    this->member = v.toInt(); \
    return true;              \
  }

#define SET_BOOL(member)       \
  [this](const QVariant& v) {  \
    this->member = v.toBool(); \
    return true;               \
  }

#define GET(member) [this]() { return this->member; }

#define GET_CONST(global) []() -> const decltype(global)& { return global; }

#define NO_NAMES GET_CONST(emptyValues)

#define NO_RANGE GET_CONST(emptyRange)

#define ADD_GLOB(member) \
  [this](const QVariant& v) { \
    this->member.append(v.toString()); \
    return true; \
  }
