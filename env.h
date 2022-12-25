/* Get system information
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

/// Get information about the operating system / environment
class Env {
 public:
  static void systemMemory(float& totalKb, float& freeKb);
  static void memoryUsage(float& vmKb, float& workingSetKb);

  /**
   * @brief set calling process to lowest/idle priority
   */
  static void setIdleProcessPriority();

 private:
  Env();
  virtual ~Env();
};

