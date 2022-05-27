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
