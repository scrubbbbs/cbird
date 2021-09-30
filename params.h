#pragma once

/// binding for command-line options with values
class Params {
 public:
  /// enum/mask value
  class NamedValue {
   public:
    int value;
    const char* shortName;
    const char* description;
  };

  /// parameter
  class Value {
   public:
    QString key;   /// property name
    QString label; /// ui label

    enum {
      Bool=1, Int, Enum, Flags
    } type; /// data type

    int sort; /// sort order for ui

    std::function<bool(const QVariant&)> set;
    std::function<QVariant(void)> get;

    std::function<const QVector<NamedValue>&()> namedValues;
    std::function<const QVector<int>&()> range;

    QString toString() const;
    const char* typeName() const;
    bool operator<(const Value& other) const { return sort < other.sort; }

    static bool setEnum(const QVariant& v,
                        const QVector<NamedValue>& namedValues,const char *arg,
                        int& member);
    static bool setFlags(const QVariant& v,
                        const QVector<NamedValue>& namedValues, const char *arg,
                        int& member);
  };

  QHash<QString, Value> _params;
  Value _invalid;

  QStringList keys() const;

  Value getValue(const QString& key) const {
    auto it = _params.find(key);
    if (it != _params.end()) return *it;
    return _invalid;
  }

  void setValue(const QString& key, const QVariant& val) {
    auto it = _params.find(key);
    if (it == _params.end()) qWarning() << "invalid param:" << key;
    if (!it->set(val)) qWarning() << "failed to set:" << key << "to:" << val;
  }

  void print() const;
  //QStringList completions(const char* key);

 protected:
  void add(const Value&& v);
};
