#include "params.h"

QStringList Params::keys() const {
  auto values = _params.values();
  std::sort(values.begin(), values.end());
  QStringList keys;
  for (auto& v : qAsConst(values)) keys += v.key;
  return keys;
}

Params::Value Params::getValue(const QString& key) const {
  auto it = _params.find(key);
  if (it != _params.end()) return *it;
  return _invalid;
}

bool Params::setValue(const QString& key, const QVariant& val) {
  auto it = _params.find(key);
  if (it == _params.end())
    qWarning() << "invalid name:" << key;
  else if (val.toString() == "?" || val.toString() == "/?" || val.toString() == "help") {
    auto v = *it;
    QString desc = "<NC>";
    desc += qq("\n    ") + _valueLabel + " [" + v.key + "]";
    desc += "\n";
    desc += qq("\n    ") + v.label;
    desc += "\n";
    desc += qq("\n    default:   [") + v.toString(this) + "]";
    desc += qq("\n    type:      <") + v.typeName() + ">";

    desc += qq("\n    range:     [");
    if (v.range().empty())
      desc += "n/a";
    else
      desc += QString("%1 to %2").arg(v.range()[0]).arg(v.range()[1]);
    desc += "]";

    const auto& nv = v.namedValues();
    if (nv.count() > 0) {
      desc += QString("\n\n    options:");
      for (auto& n : nv)
        desc += QString("\n      %1 (%2) %3").arg(n.shortName, -6).arg(n.value).arg(n.description);
    }
    qInfo().noquote().nospace() << desc;
    return false;
  } else if (!it->set(this, val))
    qWarning() << "failed to set:" << key << "to:" << val;
  else {
    _wasSet.insert(key);
    for (const auto& l : it->link)
      if (l.value == it->get(this) && !_wasSet.contains(l.target))
        if (!setValue(l.target, l.targetValue)) return false;

    return true;
  }
  return false;
}

QString Params::toString(const QString& key) const {
  return getValue(key).toString(this);
}

void Params::print() const {
  auto keys = _params.keys();
  keys.sort();
  for (auto& k : qAsConst(keys)) {
    const auto& p = _params.value(k);
    qInfo().noquote() << qSetFieldWidth(7) << p.key << qSetFieldWidth(10) << p.toString(this)
                      << p.label;
  }
}

void Params::add(const Value&& v) {
  Q_ASSERT(!_params.contains(v.key));
  _params.insert(v.key, v);
}

void Params::link(const QString& keyA, const QVariant& valueA, const QString& keyB,
                  const QVariant& valueB) {
  auto a = _params.find(keyA);
  auto b = _params.find(keyB);
  Q_ASSERT(a != _params.end() && b != _params.end());
  a.value().link.append({valueA, keyB, valueB});
}

QString Params::Value::toString(const Params* p) const {
  switch (type) {
    case Enum: {
      const auto& nv = namedValues();
      int currentValue = get(p).toInt();
      auto it = std::find_if(nv.begin(), nv.end(),
                             [=](const NamedValue& v) { return v.value == currentValue; });
      if (it == nv.end())
        return "invalid enum";
      else
        return QString("%1(%2)").arg(it->shortName).arg(currentValue);
    }
    case Flags: {
      const auto& nv = namedValues();
      const int value = get(p).toInt();
      int bits = value;
      QStringList set;
      for (auto& v : nv) {
        if (bits & v.value) {
          set += v.shortName;
          bits &= ~v.value;
        }
      }
      if (bits) qWarning() << "invalid flags in" << key;
      return QString("%1").arg(set.join("+"));
    }
    case Glob:
      return get(p).toStringList().join(';');
    default:
      return get(p).toString();
  }
}

const char* Params::Value::typeName() const {
  const char* name;
  switch (type) {
    case Bool:
      name = "bool";
      break;
    case Int:
      name = "int";
      break;
    case Enum:
      name = "enum";
      break;
    case Flags:
      name = "flags";
      break;
    case Glob:
      name = "glob";
      break;
    default:
      Q_UNREACHABLE();
  }
  return name;
}

bool Params::Value::setEnum(const QVariant& v, const QVector<Params::NamedValue>& nv,
                            const char* memberName, int& member) {
  // set enum with number or symbol
  bool ok;
  const int intVal = v.toInt(&ok);
  if (ok) {
    for (auto& n : nv)
      if (n.value == intVal) {
        member = intVal;
        return true;
      }
  } else {
    const QString strVal = v.toString();
    for (auto& n : nv)
      if (n.shortName == strVal) {
        member = n.value;
        return true;
      }
  }

  QStringList vals;
  for (auto& v : nv) vals.append(QString("%1(%2)").arg(v.shortName).arg(v.value));
  qWarning() << "invalid value for" << memberName << ":" << v.toString() << ", options are" << vals;
  return false;
}

bool Params::Value::setFlags(const QVariant& v, const QVector<Params::NamedValue>& nv,
                             const char* arg, int& member) {
  bool ok;
  const int intVal = v.toInt(&ok);
  QStringList symbols;
  if (ok) {
    int mask = 0;
    for (auto& n : nv)
      mask |= n.value;
    if ((intVal & mask) == intVal) {
      member = intVal;
      return true;
    }
    symbols.append(QString::number(intVal));
  } else {
    const QString strValue = v.toString();
    symbols = strValue.split('+');
    int flags = 0;
    for (auto& n : nv)
      if (symbols.contains(n.shortName)) {
        flags |= n.value;
        symbols.removeAll(n.shortName);
      }
    if (symbols.count() == 0) { // we consumed all flags
      member = flags;
      return true;
    }
  }

  QStringList vals;
  for (auto& v : nv) vals.append(QString("%1(%2)").arg(v.shortName).arg(v.value));
  qWarning() << "invalid flags for" << arg << ":" << symbols << ", options are" << vals;
  return false;
}

bool Params::Value::setInt(const QVariant& v, std::vector<int> range, const char* arg, int& member) {
  bool ok;
  int i = v.toInt(&ok);
  bool inRange = false;
  QString rangeDesc;
  if (range.size() == 2 && (i < range[0] || i > range[1])) {
    inRange = false;
    rangeDesc = QString("between %1 and %2").arg(range[0]).arg(range[1]);
  }
  if (!ok || !inRange) {
    qWarning().noquote() << "invalid value for" << arg << ": expected integer" << rangeDesc;
    return false;
  }
  member = i;
  return true;
}
