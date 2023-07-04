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
#include "nleutil.h"

#include <QtXml/QtXml>

KdenEdit::KdenEdit(const QString &templateFile) {
  _doc = new QDomDocument;
  QFile file(templateFile);
  if (!file.open(QFile::ReadOnly)) qFatal("failed to open template file");
  if (!_doc->setContent(&file)) qFatal("failed to load dom document");

  _producerCount = 0;  // increment when we add a producer
}

KdenEdit::~KdenEdit() { delete _doc; }

QDomElement KdenEdit::newProperty(const QString &name, const QString &value) {
  QDomElement el = _doc->createElement("property");
  el.setAttribute("name", name);
  el.appendChild(_doc->createTextNode(value));
  return el;
}

QDomElement KdenEdit::newEntry(int id, int in, int out) {
  QDomElement el = _doc->createElement("entry");
  el.setAttribute("producer", id);
  if (in >= 0) el.setAttribute("in", in);
  if (out >= 0) el.setAttribute("out", out);
  return el;
}

QDomElement KdenEdit::newBlank(int length) {
  QDomElement el = _doc->createElement("blank");
  el.setAttribute("length", length);
  return el;
}

QDomElement KdenEdit::findProperty(const QDomElement &parent, const QString &name) {
  auto list = parent.elementsByTagName("property");
  for (int i = 0; i < list.count(); i++) {
    auto el = list.at(i).toElement();
    if (el.attribute("name") == name) return el;
  }
  qCritical() << "no property named:" << name;
  return QDomElement();
}

//  QString KdenEdit::findProperty(const QDomElement& parent, const QString& name) {
//    auto list = parent.elementsByTagName("property");
//    for (int i = 0; i < list.count(); i++) {
//      auto el = list.at(i).toElement();
//      if (el.attribute("name") == name) return el.text();
//    }
//    return "";
//  }

QDomElement KdenEdit::findElementWithProperty(const QString &element, const QString &property,
                                              const QString &value) {
  auto list = _doc->documentElement().elementsByTagName(element);
  for (int i = 0; i < list.count(); i++) {
    auto el = list.at(i).toElement();
    if (findProperty(el, property).text() == value) return el;
  }
  qCritical() << element << property << value;
  qFatal("uncreoverable");
}

QDomElement KdenEdit::findElementWithAttribute(const QString &element, const QString &attribute,
                                               const QString &value) {
  auto list = _doc->documentElement().elementsByTagName(element);
  for (int i = 0; i < list.count(); i++) {
    auto el = list.at(i).toElement();
    if (el.hasAttribute(attribute) && el.attribute(attribute) == value) return el;
  }
  qCritical() << element << attribute << value;
  qFatal("uncreoverable");
}

QDomElement KdenEdit::findPlaylist(const QString &name, bool audio) {
  // return findElementWithProperty("playlist", "kdenlive:track_name", name);
  QString newName;
  if (name == "Video 1")
    newName = audio ? "playlist4" : "playlist6";
  else if (name == "Video 2")
    newName = audio ? "playlist2" : "playlist8";
  else if (name == "Video 3")
    newName = audio ? "playlist0" : "playlist10";
  Q_ASSERT(!newName.isEmpty());
  return findElementWithAttribute("playlist", "id", newName);
}

int KdenEdit::findTractor(const QString &name, bool audio) {
  int id = -1;
  if (name == "Video 1")
    id = audio ? 2 : 3;
  else if (name == "Video 2")
    id = audio ? 1 : 4;
  else if (name == "Video 3")
    id = audio ? 0 : 5;
  Q_ASSERT(id >= 0);
  return id;
}

void KdenEdit::addTrack(const QString &track) {
  // we don't actually add the track, it must be in the template
  (void)findPlaylist(track, true);
  (void)findPlaylist(track, false);
  _end[track] = 1;
}

int KdenEdit::addProducer(const QString &path) {
  QDomElement producer = _doc->createElement("producer");
  int id = 1 + _producerCount++;
  // qDebug() << "id:" << id << path;
  _producers[id] = path;
  producer.setAttribute("id", id);
  //        producer.setAttribute("title", QFileInfo(path).fileName());
  // producer.setAttribute("mlt_service", "avformat-novalidate");
  //        producer.setAttribute("threads", 3);
  producer.appendChild(newProperty("resource", QFileInfo(path).absoluteFilePath()));

  _doc->documentElement().insertBefore(producer, _doc->documentElement().firstChild());

  // first playlist is the project bin
  // would prefer to use elementById but isn't available
  _doc->documentElement().firstChildElement("playlist").appendChild(newEntry(id));

  return id;
}

void KdenEdit::addBlank(const QString &track, int length) {
  QDomElement tracks[2] = {findPlaylist(track, true), findPlaylist(track, false)};

  for (auto el : tracks) el.appendChild(newBlank(length));

  _end[track] += length;
}

void KdenEdit::insertBlank(const QString &track, int pos, int len) {
  // QDomElement pl = findPlaylist(track);
  QDomElement tracks[2] = {findPlaylist(track, true), findPlaylist(track, false)};
  for (auto &pl : tracks) {
    QDomElement clip = pl.lastChildElement();
    Q_ASSERT(clip.tagName() == "entry");

    int oldIn = clip.attribute("in", "-1").toInt();
    if (pos <= oldIn) qFatal("position < clip start");
    int oldOut = clip.attribute("out", "-1").toInt();
    if (oldOut >= pos) qFatal("position > clip end");

    qInfo() << oldIn << oldOut << "=>" << pos << oldOut;
    clip.setAttribute("out", pos - 1);
    addBlank(track, len);
    addClip(track, clip.attribute("producer").toInt(), pos, oldOut);
  }
  _end[track] = pos;
}

void KdenEdit::addClip(const QString &track, int producerId, int in, int out) {
  // QDomElement el = findPlaylist(track);
  QDomElement tracks[2] = {findPlaylist(track, true), findPlaylist(track, false)};

  for (auto &el : tracks) el.appendChild(newEntry(producerId, in, out - 1));  // out frame inclusive

  // link clips via groups property of main bin
  QVariantList children;
  int tractorIds[2] = {findTractor(track, false), findTractor(track, true)};
  for (auto i : tractorIds) {
    QVariantHash hash;
    hash["leaf"] = "clip";
    hash["type"] = "Leaf";
    hash["data"] = QString("%1:%2").arg(i).arg(_end[track] - 1);  // tractor id + in-frame
    children.append(hash);
  }
  QVariantHash link;
  link["type"] = "AVSplit";
  link["children"] = children;

  QDomElement mainBin = findElementWithAttribute("playlist", "id", "main_bin");
  QDomElement prop = findProperty(mainBin, "kdenlive:docproperties.groups");
  QJsonDocument doc = QJsonDocument::fromJson(prop.text().toUtf8());
  Q_ASSERT(doc.isArray());
  QJsonArray groups = doc.array();
  groups.append(QJsonObject::fromVariantHash(link));
  doc.setArray(groups);
  prop.firstChild().toText().setData(doc.toJson());
  _end[track] += out - in;
}

void KdenEdit::extendClip(const QString &track, int out) {
  // QDomElement el = findPlaylist(track);
  QDomElement tracks[2] = {findPlaylist(track, true), findPlaylist(track, false)};
  int oldOut;
  for (auto &el : tracks) {
    QDomElement clip = el.lastChildElement();
    oldOut = clip.attribute("out").toInt() + 1;
    clip.setAttribute("out", out - 1);
  }
  _end[track] += out - oldOut;
}

int KdenEdit::clipEnd(const QString &track) {
  QDomElement el = findPlaylist(track, false);
  QDomElement clip = el.lastChildElement();
  Q_ASSERT(clip.tagName() == "entry" && !clip.isNull());
  return clip.attribute("out").toInt();
}

void KdenEdit::addMarker(int producerId, int frame, const QString &text, int type) {
  QVariantHash marker;
  marker["comment"] = text;
  marker["pos"] = frame;
  marker["type"] = type;

  QDomElement producer = findElementWithAttribute("producer", "id", QString::number(producerId));
  QDomElement prop = findProperty(producer, "kdenlive:markers");
  if (prop.isNull()) prop = newProperty("kdenlive:markers", "[]");
  producer.appendChild(prop);

  QJsonDocument doc = QJsonDocument::fromJson(prop.text().toUtf8());
  Q_ASSERT(doc.isArray());
  QJsonArray groups = doc.array();
  groups.append(QJsonObject::fromVariantHash(marker));
  doc.setArray(groups);
  prop.firstChild().toText().setData(doc.toJson());
}

void KdenEdit::saveXml(const QString &xmlFile) {
  QFile file(xmlFile);
  if (!file.open(QFile::WriteOnly)) qFatal("failed to open output");

  file.write(_doc->toByteArray());

  // saveEDL(xmlFile);
}

QString KdenEdit::framesToTimeCode(int num) {
  const int fps = 30;
  const int fph = fps * 60 * 60;
  const int fpm = fps * 60;

  int hours = num / fph;
  int minutes = (num - hours * fph) / fpm;
  int seconds = (num - hours * fph - minutes * fpm) / fps;
  int frames = num - hours * fph - minutes * fpm - seconds * fps;

  return QString("%1:%2:%3:%4")
      .arg(hours, 2, 10, QChar('0'))
      .arg(minutes, 2, 10, QChar('0'))
      .arg(seconds, 2, 10, QChar('0'))
      .arg(frames, 2, 10, QChar('0'));
}

#if 0
void KdenEdit::saveEDL(const QString &edlFileBase) {
  // for each track
  // edl playlist header
  // for each clip/blank
  // blank: count frames
  // clip: output edl, count frames

  (void)edlFileBase;
  // QTextStream file(stdout);

  int edlPlaylist = 0;
  FCPExport fcp;

  QDomElement tracks[3] = {findPlaylist("Video 1", false),
                           findPlaylist("Video 2", false),
                           findPlaylist("Video 3", false)};

  for (int i = 0; i < 3; ++i) {
    auto pl = tracks[i].toElement();
    auto cl = pl.childNodes();

    qInfo() << "edl playlist" << i + 1 << pl.attribute("id");
    // if (i < 4) continue;

    QFile fp(QString("%1.%2.edl").arg(edlFileBase).arg(edlPlaylist));
    fp.open(QFile::WriteOnly | QFile::Truncate);
    QTextStream edl(&fp);

    QTextStream xml(stdout);

    // file << QString("\n ===playlist%1===").arg(edlPlaylist++);
    edl << QString("\nTITLE: video%1\n\n").arg(edlPlaylist);
    edlPlaylist++;

    int pos = 0;
    int edlClip = 1;
    for (int j = 0; j < cl.count(); j++) {
      auto el = cl.at(j).toElement();

      if (el.tagName() == "entry") {
        int pid = el.attribute("producer").toInt();
        int in = el.attribute("in").toInt();
        int out = el.attribute("out").toInt();

        if (out == 0) {
          VideoContext ctx;
          VideoContext::DecodeOptions opt;
          if (0 == ctx.open(producerPath(pid), opt))
            out = int(ctx.metadata().duration * ctx.metadata().frameRate) + 1;
          else
            qFatal("unable to open source, can't determine video duration");
        }

        edl << QString("%1 AX AA/V  C         %2 %3 %4 %5\n")
               .arg(edlClip++, 3, 10, QChar('0'))
               .arg(framesToTimeCode(in))
               .arg(framesToTimeCode(out))
               .arg(framesToTimeCode(pos))
               .arg(framesToTimeCode(out - in + pos));

        edl << "* FROM CLIP NAME: " << QFileInfo(producerPath(pid)).fileName()
            << "\n";
        edl << "* FROM FILE: "
            << QFileInfo(producerPath(pid)).absoluteFilePath() << "\n\n";

        fcp.addClipItem(edlPlaylist - 1, edlClip - 1, producerPath(pid), pos,
                        in, out);

        pos += out - in;
      }
      if (el.tagName() == "blank") {
        int length = el.attribute("length").toInt();
        edl << QString("%1 BL V  C         %2 %3 %4 %5\n\n")
               .arg(edlClip++, 3, 10, QChar('0'))
               .arg(framesToTimeCode(pos))
               .arg(framesToTimeCode(pos + length))
               .arg(framesToTimeCode(pos))
               .arg(framesToTimeCode(pos + length));
        pos += length;
      }
    }
  }

  fcp.saveXml(edlFileBase + ".xml");
}
#endif
