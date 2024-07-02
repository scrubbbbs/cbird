/* File system utilities
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

#ifdef Q_OS_WIN
#include <fileapi.h>
class FileId {
 public:
  bool valid = false;
  BY_HANDLE_FILE_INFORMATION info;

  FileId() = delete;
  FileId(const QString& path) {
    auto handle = CreateFileW((wchar_t*)path.utf16(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) return;

    valid = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);

    if (!valid)
      qWarning() << "GetFileInformationByHandle failed, link tracking disabled for:" << path;

    // if (valid)
    //   qDebug() << Qt::hex << info.dwVolumeSerialNumber
    //            << info.nFileIndexHigh << info.nFileIndexLow << path;
  }
  bool isValid() const { return valid; }
  bool operator==(const FileId& other) const {
    return info.nFileIndexLow == other.info.nFileIndexLow &&
           info.nFileIndexHigh == other.info.nFileIndexHigh &&
           info.dwVolumeSerialNumber == other.info.dwVolumeSerialNumber;
  }
};

Q_ALWAYS_INLINE uint qHash(const FileId& id) {
  return qHash(id.info.dwVolumeSerialNumber ^ id.info.nFileIndexHigh ^ id.info.nFileIndexLow);
}
#else
#include <sys/stat.h>
#include <sys/types.h>
class FileId {
 public:
  struct stat st;
  FileId() = delete;
  FileId(const QString& path) {
    if (stat(qUtf8Printable(path), &st) < 0) st.st_ino = 0;
    // qDebug() << Qt::hex << st.st_dev << st.st_ino << path;
  }
  bool isValid() const { return st.st_ino > 0; }
  bool operator==(const FileId& other) const {
    return st.st_ino == other.st.st_ino && st.st_dev == other.st.st_dev;
  }
};

Q_ALWAYS_INLINE uint qHash(const FileId& id) { return qHash(id.st.st_ino ^ id.st.st_dev); }
#endif

#ifdef Q_OS_WIN
typedef struct _REPARSE_DATA_BUFFER {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  } DUMMYUNIONNAME;
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;

static QString resolveJunction(const QString& path) {
  auto handle =
      CreateFileW((wchar_t*)path.utf16(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle == INVALID_HANDLE_VALUE) {
    qCritical() << "CreateFile failed" << Qt::hex << GetLastError();
    return "";
  }

  _REPARSE_DATA_BUFFER* rdb;
  char buf[sizeof(*rdb) + PATH_MAX * 2 * 2];  // 2 paths returned, + 2 bytes/character
  DWORD outSize = 0;
  auto ok =
      DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, buf, sizeof(buf), &outSize, NULL);
  auto err = GetLastError();
  CloseHandle(handle);

  if (!ok) {
    qCritical() << "DeviceIoControl failed" << Qt::hex << err;
    return "";
  }
  rdb = (decltype(rdb))buf;
  Q_ASSERT(rdb->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT);
  auto& mp = rdb->MountPointReparseBuffer;
  auto name =
      QString::fromWCharArray(mp.PathBuffer + mp.PrintNameOffset / 2, mp.PrintNameLength / 2);
  return QFileInfo(name).absoluteFilePath();
}
#endif
