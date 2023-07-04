/* Utilities for NLE (Non-Linear-Editors) Integration
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

class QDomDocument;
class QDomElement;
class __InvalidClass;
#if 0
/// Tools for creating a Final-cut Pro project file
class FCPEdit {
 public:
  FCPEdit() {
    QFile file(qApp->applicationDirPath() + "/data/finalcut.xml");
    if (!file.open(QFile::ReadOnly)) qFatal("failed to open template file");

    if (!_doc.setContent(&file)) qFatal("failed to load dom document");
  }

  QDomElement elementByPath(QDomElement el, const QString& path,
                            int index = 0) {
    QStringList tags = path.split("/");
    while (!el.isNull() && tags.count()) {
      el = el.firstChildElement(tags.first());
      tags.removeFirst();
    }
    if (tags.count() == 0)
      while (index-- > 0 && !el.isNull())
        el = el.nextSiblingElement(el.tagName());

    return el;
  }

  QDomElement addProp(QDomElement& parent, const QString& name,
                      const QVariant& value) {
    QDomElement el = _doc.createElement(name);
    el.appendChild(_doc.createTextNode(value.toString()));
    parent.appendChild(el);
    return el;
  }

  QDomElement addProp(QDomElement& parent, const QString& name,
                      const QString& attrName, const QVariant& value) {
    QDomElement el = _doc.createElement(name);
    el.setAttribute(attrName, value.toString());
    parent.appendChild(el);
    return el;
  }

  QDomElement setProp(QDomElement& parent, const QString& path,
                      const QVariant& value) {
    QDomElement el = elementByPath(parent, path);
    Q_ASSERT(!el.isNull());
    QDomText t = el.firstChild().toText();
    Q_ASSERT(!t.isNull());
    t.setData(value.toString());
    return el;
  }

  QDomElement setProp(QDomElement& parent, const QString& path,
                      const QString& attrName, const QVariant& value) {
    QDomElement el = elementByPath(parent, path);
    el.setAttribute(attrName, value.toString());
    return el;
  }

  QDomElement addFile(QDomElement& parent, const QString& path) {
    QString fileId;
    if (_files.contains(path)) {
      fileId = _files[path];
      return addProp(parent, "file", "id", fileId);
    } else {
      fileId = "file-" + QString::number(_fileId++);
      _files[path] = fileId;
    }

    QDomDocument templateDoc;

    QFile fileTemplate(qApp->applicationDirPath() + "/data/finalcut-file.xml");
    fileTemplate.open(QFile::ReadOnly);
    templateDoc.setContent(&fileTemplate);

    QDomElement file = templateDoc.documentElement();
    file.setAttribute("id", fileId);
    setProp(file, "name", "foo");
    setProp(file, "pathurl", "file://" + path);

    VideoContext ctx;
    VideoContext::DecodeOptions opt;
    ctx.open(path, opt);

    QDomElement video =
        elementByPath(file, "media/video/samplecharacteristics");
    setProp(video, "width", ctx.metadata().frameSize.width());
    setProp(video, "height", ctx.metadata().frameSize.height());

    QDomElement sequence = elementByPath(
        _doc.documentElement(),
        "project/children/sequence/media/video/format/samplecharacteristics");
    setProp(sequence, "width", ctx.metadata().frameSize.width());
    setProp(sequence, "height", ctx.metadata().frameSize.height());

    parent.appendChild(file);
    return parent.lastChildElement();
  }

  QDomElement addClipItem(int trackIndex, int clipIndex,
                          const QString& fileName, int trackIn, int clipIn,
                          int clipOut) {
    QDomElement videoTrack = elementByPath(
        _doc.documentElement(), "project/children/sequence/media/video/track",
        trackIndex);
    Q_ASSERT(!videoTrack.isNull());

    // video
    QDomElement ci = _doc.createElement("clipitem");
    ci.setAttribute("id", "clipitem-" + QString::number(_clipItemId++));
    videoTrack.appendChild(ci);
    addProp(ci, "masterclipid", "masterclip-?");  // placeholder - set it later
    // addProp(ci, "name", "name-goes-here");
    addProp(ci, "enabled", "TRUE");
    // addProp(ci, "duration", 18000);  // same as the uncut file duration
    addProp(ci, "start", trackIn);
    addProp(ci, "end", trackIn + clipOut - clipIn);
    addProp(ci, "in", clipIn);
    addProp(ci, "out", clipOut);
    addProp(ci, "alphatype", "none");

    QDomElement file = addFile(ci, fileName);

    // master clip matches file id... so it seems
    QString masterClipId = file.attribute("id").replace("file-", "masterclip-");
    setProp(ci, "masterclipid", masterClipId);

    // audio
    QDomElement audioTrack = elementByPath(
        _doc.documentElement(), "project/children/sequence/media/audio/track",
        trackIndex);

    QDomElement ca = _doc.createElement("clipitem");
    audioTrack.appendChild(ca);
    addProp(ca, "masterclipid", masterClipId);
    // addProp(ca, "name", "name-goes-here");
    addProp(ca, "enabled", "TRUE");
    // addProp(ca, "duration", 18000);
    addProp(ca, "start", trackIn);
    addProp(ca, "end", trackIn + clipOut - clipIn);
    addProp(ca, "in", clipIn);
    addProp(ca, "out", clipOut);
    addProp(ca, "file", "id", file.attribute("id"));

    QDomElement sourceTrack = _doc.createElement("sourcetrack");
    addProp(sourceTrack, "mediatype", "audio");
    addProp(sourceTrack, "trackindex", trackIndex + 1);
    ca.appendChild(sourceTrack);

    // link
    QDomElement link = _doc.createElement("link");
    addProp(link, "linkclipref", ci.attribute("id"));
    addProp(link, "mediatype", "video");
    addProp(link, "trackindex", trackIndex + 1);
    addProp(link, "clipindex", clipIndex + 1);
    ca.appendChild(link);

    return ci;
  }

  void saveXml(const QString& xmlFile) {
    QFile file(xmlFile);
    if (!file.open(QFile::WriteOnly)) qFatal("failed to open output");

    file.write(_doc.toByteArray());
  }

 private:
  QDomDocument _doc;
  int _clipItemId = 1;
  int _fileId = 1;
  QMap<QString, QString> _files;
};
#endif

/// Tools for creating a Kdenlive project file
class KdenEdit {
  NO_COPY_NO_DEFAULT(KdenEdit, __InvalidClass);

 public:
  KdenEdit(const QString& templateFile);
  ~KdenEdit();

 private:
  QDomElement newProperty(const QString& name, const QString& value);

  QDomElement newEntry(int id, int in = -1, int out = -1);
  QDomElement newBlank(int length);

  QDomElement findProperty(const QDomElement& parent, const QString& name);
  QDomElement findElementWithProperty(const QString& element, const QString& property,
                                      const QString& value);
  QDomElement findElementWithAttribute(const QString& element, const QString& attribute,
                                       const QString& value);
  QDomElement findPlaylist(const QString& name, bool audio);

  int findTractor(const QString& name, bool audio);
  QString producerPath(int id) const { return _producers[id]; }
  int clipEnd(const QString& track);

  QString framesToTimeCode(int num);
  int end(const QString& track) const { return _end[track]; }

  // void saveEDL(const QString& edlFileBase);
 public:
  /**
   * @brief addTrack
   * @param track
   */
  void addTrack(const QString& track);

  /**
   * @brief addProducer
   * @param path Path to the file
   * @return Producer ID for use with other calls
   */
  int addProducer(const QString& path);

  void addBlank(const QString& track, int length);

  void insertBlank(const QString& track, int pos, int len);

  /**
   * @brief addClip
   * @param track
   * @param producerId
   * @param in   0-indexed in frame
   * @param out  0-indexed one-past-the-end
   */
  void addClip(const QString& track, int producerId, int in = 0, int out = -1);

  void extendClip(const QString& track, int out = -1);

  void addMarker(int producerId, int frame, const QString& text, int type);

  void saveXml(const QString& xmlFile);

 private:
  QDomDocument* _doc;
  int _producerCount;
  QMap<QString, int> _end;
  QMap<int, QString> _producers;
};
