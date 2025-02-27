/* Trying to cleanup CLI loop WIP
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

#include "database.h"
#include "index.h"
#include "media.h"
#include "scanner.h"
class Engine;

class Commands {
  const IndexParams& _indexParams;
  const SearchParams& _searchParams;
  const QString& _switch;
  QStringList& _args;
  MediaGroup& _selection;
  MediaGroupList& _queryResult;
 public:
  typedef std::tuple<QString,QString,bool> Filter; // property,valueExpr,without

  Commands(const IndexParams& ip, const SearchParams& sp, QString& switch_, QStringList& args,
           MediaGroup& selection, MediaGroupList& queryResult)
      : _indexParams(ip),
        _searchParams(sp),
        _switch(switch_),
        _args(args),
        _selection(selection),
        _queryResult(queryResult) {}

  QString nextArg();  // remove next arg and return it
  int intArg();       // .. and also check it is an int
  QStringList optionList(); // all args up to next switch or eol

  void filter(const std::vector<Filter>&) const;
  void rename(Database* db, const QString& srcPat, const QString& dstPat, const QString& option);
  MediaGroup selectFiles();
  void verify(Database* db, const QString& jpegFixPath);

  void testVideoDecoder();
  void testImageSearch(Engine& engine);
  void testVideoIndex(Engine& engine, const QString& path);
  void testUpdate(Engine& engine);
  void testCsv(Engine& engine, const QString& path);
};
