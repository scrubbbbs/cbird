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

void Params::setValue(const QString& key, const QVariant& val) {
  auto it = _params.find(key);
  if (it == _params.end())
    qWarning() << "invalid param:" << key;
  else if (!it->set(val))
    qWarning() << "failed to set:" << key << "to:" << val;
  else {
    _wasSet.insert(key);
    for (const auto& l : it->link)
      if (l.value == it->get() && !_wasSet.contains(l.target))
        setValue(l.target, l.targetValue);
  }
}

void Params::print() const {
  auto keys = _params.keys();
  keys.sort();
  for (auto& k : qAsConst(keys)) {
    const auto& p = _params.value(k);
    qInfo().noquote() << qSetFieldWidth(6) << p.key << qSetFieldWidth(0) << p.toString();
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

QString Params::Value::toString() const {
  switch (type) {
    case Enum: {
      const auto& nv = namedValues();
      int currentValue = get().toInt();
      auto it = std::find_if(nv.begin(), nv.end(),
                             [=](const NamedValue& v) { return v.value == currentValue; });
      if (it == nv.end())
        return "invalid enum";
      else
        return QString("%1(%2)").arg(it->shortName).arg(currentValue);
    }
    case Flags: {
      const auto& nv = namedValues();
      const int value = get().toInt();
      int bits = value;
      QStringList set;
      for (auto& v : nv) {
        if (bits & v.value) {
          set += v.shortName;
          bits &= ~v.value;
        }
      }
      if (bits) qWarning() << "invalid flags in" << key;
      return QString("%1(%2)").arg(set.join("+")).arg(value);
    }
    default:
      return get().toString();
  }
}

const char* Params::Value::typeName() const {
  const char* name = "Unknown type";
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
    int flags = intVal;
    for (auto& n : nv) flags &= ~n.value;
    if (!flags) {
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
    if (symbols.count() == 0) {
      member = flags;
      return true;
    }
  }

  QStringList vals;
  for (auto& v : nv) vals.append(QString("%1(%2)").arg(v.shortName).arg(v.value));
  qWarning() << "invalid flags for" << arg << ":" << symbols << ", options are" << vals;
  return false;
}
