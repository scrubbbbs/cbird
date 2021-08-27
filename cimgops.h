#pragma once

#include "cimg_fwd.h"
class Media;

void qImageToCImg(const QImage& src, CImg<uint8_t>& dst);
void cImgToQImage(const CImg<uint8_t>& src, QImage& dst);

int qualityScore(const Media& m, QVector<QImage>* visuals = nullptr);
