/* Binding for command-line options
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

class Params {
 public:
  /// enum or flags value
  class NamedValue {
   public:
    int value;
    const char* shortName;
    const char* description;
  };

  /// how one Value should change another one
  class Link {
   public:
    QVariant value;
    QString target;  // other Value name
    QVariant targetValue;
  };

  /// Binds a struct member to a getter/setter and other useful things
  class Value {
   public:
    QString key;    /// name
    int category;   /// category
    QString label;  /// ui label

    enum { Bool = 1, Int, Enum, Flags, Glob, List } type; /// data type

    int sort;  /// sort order for ui

    std::function<bool(Params*, const QVariant&)> set;
    std::function<QVariant(const Params*)> get;

    std::function<const QVector<NamedValue>&()> namedValues;
    std::function<const QVector<int>&()> range;

    QList<Link> link = {};

    QString toString(const Params* p) const;
    const char* typeName() const;
    bool operator<(const Value& other) const { return sort < other.sort; }

    static bool setEnum(const QVariant& v, const QVector<NamedValue>& namedValues, const char* arg,
                        int& member);
    static bool setFlags(const QVariant& v, const QVector<NamedValue>& namedValues, const char* arg,
                         int& member);

    static bool setInt(const QVariant& v, std::vector<int> range, const char* arg, int& member);
  };

  QStringList keys() const;

  Value getValue(const QString& key) const;
  bool setValue(const QString& key, const QVariant& val);
  QString toString(const QString& key) const;
  virtual QString categoryLabel(int category) const;
  void print() const;

 protected:
  QString _valueLabel; // what a param is for, e.g. "Search Parameter"
  QHash<QString, Value> _params;
  Value _invalid;
  QSet<QString> _wasSet; // keys successful in setValue()

  void add(const Value&& v);

  /// set another parameter if user set the first one (setValue()),
  /// but only if user never set the second one
  void link(const QString& keyA, const QVariant& valueA, const QString& keyB,
            const QVariant& valueB);
};
