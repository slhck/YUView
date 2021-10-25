/*  This file is part of YUView - The YUV player with advanced analytics toolset
 *   <https://github.com/IENT/YUView>
 *   Copyright (C) 2015  Institut für Nachrichtentechnik, RWTH Aachen University, GERMANY
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   In addition, as a special exception, the copyright holders give
 *   permission to link the code of portions of this program with the
 *   OpenSSL library under certain conditions as described in each
 *   individual source file, and distribute linked combinations including
 *   the two.
 *
 *   You must obey the GNU General Public License in all respects for all
 *   of the code used other than OpenSSL. If you modify file(s) with this
 *   exception, you may extend this exception to your version of the
 *   file(s), but you are not obligated to do so. If you do not wish to do
 *   so, delete this exception statement from your version. If you delete
 *   this exception statement from all source files in the program, then
 *   also delete it here.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "videoHandlerRGB.h"

#include <common/EnumMapper.h>
#include <common/FileInfo.h>
#include <common/functions.h>
#include <common/functionsGui.h>
#include <video/videoHandlerRGBCustomFormatDialog.h>

#include <QPainter>
#include <QtGlobal>

namespace video
{

using namespace rgb;
namespace
{

const auto componentShowMapper = EnumMapper<ComponentShow>({{ComponentShow::RGBA, "RGBA", "RGBA"},
                                                            {ComponentShow::RGB, "RGB", "RGB"},
                                                            {ComponentShow::R, "R", "Red Only"},
                                                            {ComponentShow::G, "G", "Green Only"},
                                                            {ComponentShow::B, "B", "Blue Only"},
                                                            {ComponentShow::A, "A", "Alpha Only"}});

}

// Activate this if you want to know when which buffer is loaded/converted to image and so on.
#define VIDEOHANDLERRGB_DEBUG_LOADING 0
#if VIDEOHANDLERRGB_DEBUG_LOADING && !NDEBUG
#include <QDebug>
#define DEBUG_RGB qDebug
#else
#define DEBUG_RGB(fmt, ...) ((void)0)
#endif

// Restrict is basically a promise to the compiler that for the scope of the pointer, the target of
// the pointer will only be accessed through that pointer (and pointers copied from it).
#if __STDC__ != 1
#define restrict __restrict /* use implementation __ format */
#else
#ifndef __STDC_VERSION__
#define restrict __restrict /* use implementation __ format */
#else
#if __STDC_VERSION__ < 199901L
#define restrict __restrict /* use implementation __ format */
#else
#/* all ok */
#endif
#endif
#endif

/* The default constructor of the RGBFormatList will fill the list with all supported RGB file
 * formats. Don't forget to implement actual support for all of them in the conversion functions.
 */
videoHandlerRGB::RGBFormatList::RGBFormatList()
{
  append(rgbPixelFormat(8, DataLayout::Packed, ChannelOrder::RGB));
  append(rgbPixelFormat(10, DataLayout::Packed, ChannelOrder::RGB));
  append(rgbPixelFormat(8, DataLayout::Packed, ChannelOrder::RGB, AlphaMode::First));
  append(rgbPixelFormat(8, DataLayout::Packed, ChannelOrder::BRG));
  append(rgbPixelFormat(10, DataLayout::Packed, ChannelOrder::BRG));
  append(rgbPixelFormat(10, DataLayout::Planar, ChannelOrder::RGB));
}

/* Put all the names of the RGB formats into a list and return it
 */
std::vector<std::string> videoHandlerRGB::RGBFormatList::getFormattedNames() const
{
  std::vector<std::string> l;
  for (int i = 0; i < count(); i++)
    l.push_back(at(i).getName());
  return l;
}

rgbPixelFormat videoHandlerRGB::RGBFormatList::getFromName(const std::string &name) const
{
  for (int i = 0; i < count(); i++)
    if (at(i).getName() == name)
      return at(i);

  // If the format could not be found, we return the "Unknown Pixel Format" format
  return {};
}

// Initialize the static rgbPresetList
videoHandlerRGB::RGBFormatList videoHandlerRGB::rgbPresetList;

videoHandlerRGB::videoHandlerRGB() : videoHandler()
{
  // preset internal values
  this->setSrcPixelFormat(rgbPixelFormat(8, DataLayout::Packed, ChannelOrder::RGB));
}

videoHandlerRGB::~videoHandlerRGB()
{
  // This will cause a "QMutex: destroying locked mutex" warning by Qt.
  // However, here this is on purpose.
  rgbFormatMutex.lock();
}

QStringPairList videoHandlerRGB::getPixelValues(const QPoint &pixelPos,
                                                int           frameIdx,
                                                frameHandler *item2,
                                                const int     frameIdx1)
{
  QStringPairList values;

  const int formatBase = settings.value("ShowPixelValuesHex").toBool() ? 16 : 10;
  if (item2 != nullptr)
  {
    videoHandlerRGB *rgbItem2 = dynamic_cast<videoHandlerRGB *>(item2);
    if (rgbItem2 == nullptr)
      // The second item is not a videoHandlerRGB. Get the values from the frameHandler.
      return frameHandler::getPixelValues(pixelPos, frameIdx, item2, frameIdx1);

    if (currentFrameRawData_frameIndex != frameIdx ||
        rgbItem2->currentFrameRawData_frameIndex != frameIdx1)
      return QStringPairList();

    auto width  = std::min(frameSize.width, rgbItem2->frameSize.width);
    auto height = std::min(frameSize.height, rgbItem2->frameSize.height);

    if (pixelPos.x() < 0 || pixelPos.x() >= int(width) || pixelPos.y() < 0 ||
        pixelPos.y() >= int(height))
      return QStringPairList();

    rgba_t valueThis  = getPixelValue(pixelPos);
    rgba_t valueOther = rgbItem2->getPixelValue(pixelPos);

    const int     R       = int(valueThis.R) - int(valueOther.R);
    const int     G       = int(valueThis.G) - int(valueOther.G);
    const int     B       = int(valueThis.B) - int(valueOther.B);
    const QString RString = ((R < 0) ? "-" : "") + QString::number(std::abs(R), formatBase);
    const QString GString = ((G < 0) ? "-" : "") + QString::number(std::abs(G), formatBase);
    const QString BString = ((B < 0) ? "-" : "") + QString::number(std::abs(B), formatBase);

    values.append(QStringPair("R", RString));
    values.append(QStringPair("G", GString));
    values.append(QStringPair("B", BString));
    if (srcPixelFormat.hasAlpha())
    {
      const int     A       = int(valueThis.A) - int(valueOther.A);
      const QString AString = ((A < 0) ? "-" : "") + QString::number(std::abs(A), formatBase);
      values.append(QStringPair("A", AString));
    }
  }
  else
  {
    int width  = frameSize.width;
    int height = frameSize.height;

    if (currentFrameRawData_frameIndex != frameIdx)
      return QStringPairList();

    if (pixelPos.x() < 0 || pixelPos.x() >= width || pixelPos.y() < 0 || pixelPos.y() >= height)
      return QStringPairList();

    rgba_t value = getPixelValue(pixelPos);

    values.append(QStringPair("R", QString::number(value.R, formatBase)));
    values.append(QStringPair("G", QString::number(value.G, formatBase)));
    values.append(QStringPair("B", QString::number(value.B, formatBase)));
    if (srcPixelFormat.hasAlpha())
      values.append(QStringPair("A", QString::number(value.A, formatBase)));
  }

  return values;
}

void videoHandlerRGB::setFormatFromCorrelation(const QByteArray &, int64_t)
{ /* TODO */
}

bool videoHandlerRGB::setFormatFromString(QString format)
{
  DEBUG_RGB("videoHandlerRGB::setFormatFromString " << format << "\n");

  auto split = format.split(";");
  if (split.length() != 4 || split[2] != "RGB")
    return false;

  if (!frameHandler::setFormatFromString(split[0] + ";" + split[1]))
    return false;

  auto fmt = rgbPixelFormat(split[3].toStdString());
  if (!fmt.isValid())
    return false;

  this->setRGBPixelFormat(fmt, false);
  return true;
}

QLayout *videoHandlerRGB::createVideoHandlerControls(bool isSizeFixed)
{
  // Absolutely always only call this function once!
  assert(!ui.created());

  QVBoxLayout *newVBoxLayout = nullptr;
  if (!isSizeFixed)
  {
    // Our parent (frameHandler) also has controls to add. Create a new vBoxLayout and append the
    // parent controls and our controls into that layout, separated by a line. Return that layout
    newVBoxLayout = new QVBoxLayout;
    newVBoxLayout->addLayout(frameHandler::createFrameHandlerControls(isSizeFixed));

    QFrame *line = new QFrame;
    line->setObjectName(QStringLiteral("line"));
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    newVBoxLayout->addWidget(line);
  }

  ui.setupUi();

  // Set all the values of the properties widget to the values of this class
  ui.rgbFormatComboBox->addItems(functions::toQStringList(rgbPresetList.getFormattedNames()));
  ui.rgbFormatComboBox->addItem("Custom...");
  ui.rgbFormatComboBox->setEnabled(!isSizeFixed);
  int idx = rgbPresetList.indexOf(srcPixelFormat);
  if (idx == -1 && srcPixelFormat.isValid())
  {
    // Custom pixel format (but a known pixel format). Add and select it.
    rgbPresetList.append(srcPixelFormat);
    int nrItems = ui.rgbFormatComboBox->count();
    ui.rgbFormatComboBox->insertItem(nrItems - 1, QString::fromStdString(srcPixelFormat.getName()));
    idx = rgbPresetList.indexOf(srcPixelFormat);
    ui.rgbFormatComboBox->setCurrentIndex(idx);
  }
  else if (idx > 0)
    ui.rgbFormatComboBox->setCurrentIndex(idx);

  ui.RScaleSpinBox->setValue(componentScale[0]);
  ui.RScaleSpinBox->setMaximum(1000);
  ui.GScaleSpinBox->setValue(componentScale[1]);
  ui.GScaleSpinBox->setMaximum(1000);
  ui.BScaleSpinBox->setValue(componentScale[2]);
  ui.BScaleSpinBox->setMaximum(1000);
  ui.AScaleSpinBox->setValue(componentScale[3]);
  ui.AScaleSpinBox->setMaximum(1000);

  ui.RInvertCheckBox->setChecked(this->componentInvert[0]);
  ui.GInvertCheckBox->setChecked(this->componentInvert[1]);
  ui.BInvertCheckBox->setChecked(this->componentInvert[2]);
  ui.AInvertCheckBox->setChecked(this->componentInvert[3]);

  ui.limitedRangeCheckBox->setChecked(this->limitedRange);

  connect(ui.rgbFormatComboBox,
          QOverload<int>::of(&QComboBox::currentIndexChanged),
          this,
          &videoHandlerRGB::slotRGBFormatControlChanged);
  connect(ui.colorComponentsComboBox,
          QOverload<int>::of(&QComboBox::currentIndexChanged),
          this,
          &videoHandlerRGB::slotDisplayOptionsChanged);
  for (auto spinBox : {ui.RScaleSpinBox, ui.GScaleSpinBox, ui.BScaleSpinBox, ui.AScaleSpinBox})
    connect(spinBox,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            &videoHandlerRGB::slotDisplayOptionsChanged);
  for (auto checkBox : {ui.RInvertCheckBox,
                        ui.GInvertCheckBox,
                        ui.BInvertCheckBox,
                        ui.AInvertCheckBox,
                        ui.limitedRangeCheckBox})
    connect(checkBox, &QCheckBox::stateChanged, this, &videoHandlerRGB::slotDisplayOptionsChanged);

  this->updateControlsForNewPixelFormat();

  if (!isSizeFixed && newVBoxLayout)
    newVBoxLayout->addLayout(ui.topVerticalLayout);

  if (isSizeFixed)
    return ui.topVerticalLayout;
  else
    return newVBoxLayout;
}

void videoHandlerRGB::slotDisplayOptionsChanged()
{
  {
    auto selection = ui.colorComponentsComboBox->currentText().toStdString();
    if (auto c = componentShowMapper.getValue(selection, true))
      this->componentDisplayMode = *c;
  }

  componentScale[0]  = ui.RScaleSpinBox->value();
  componentScale[1]  = ui.GScaleSpinBox->value();
  componentScale[2]  = ui.BScaleSpinBox->value();
  componentScale[3]  = ui.AScaleSpinBox->value();
  componentInvert[0] = ui.RInvertCheckBox->isChecked();
  componentInvert[1] = ui.GInvertCheckBox->isChecked();
  componentInvert[2] = ui.BInvertCheckBox->isChecked();
  componentInvert[3] = ui.AInvertCheckBox->isChecked();
  limitedRange       = ui.limitedRangeCheckBox->isChecked();

  // Set the current frame in the buffer to be invalid and clear the cache.
  // Emit that this item needs redraw and the cache needs updating.
  currentImageIndex = -1;
  setCacheInvalid();
  emit signalHandlerChanged(true, RECACHE_CLEAR);
}

void videoHandlerRGB::updateControlsForNewPixelFormat()
{
  if (!ui.created())
    return;

  auto valid         = this->srcPixelFormat.isValid();
  auto hasAlpha      = this->srcPixelFormat.hasAlpha();
  auto validAndAlpha = valid & hasAlpha;

  ui.RScaleSpinBox->setEnabled(valid);
  ui.GScaleSpinBox->setEnabled(valid);
  ui.BScaleSpinBox->setEnabled(valid);
  ui.AScaleSpinBox->setEnabled(validAndAlpha);
  ui.RInvertCheckBox->setEnabled(valid);
  ui.GInvertCheckBox->setEnabled(valid);
  ui.BInvertCheckBox->setEnabled(valid);
  ui.AInvertCheckBox->setEnabled(validAndAlpha);

  QSignalBlocker block(ui.colorComponentsComboBox);
  ui.colorComponentsComboBox->setEnabled(valid);
  ui.colorComponentsComboBox->clear();
  if (valid)
  {
    std::vector<ComponentShow> listItems;
    if (hasAlpha)
      listItems = {ComponentShow::RGBA,
                   ComponentShow::RGB,
                   ComponentShow::R,
                   ComponentShow::G,
                   ComponentShow::B,
                   ComponentShow::A};
    else
      listItems = {ComponentShow::RGB, ComponentShow::R, ComponentShow::G, ComponentShow::B};

    if (!hasAlpha && (this->componentDisplayMode == ComponentShow::A ||
                      this->componentDisplayMode == ComponentShow::RGBA))
    {
      this->componentDisplayMode = ComponentShow::RGB;
    }
    for (const auto &item : listItems)
      ui.colorComponentsComboBox->addItem(
          QString::fromStdString(componentShowMapper.getText(item)));
    ui.colorComponentsComboBox->setCurrentText(
        QString::fromStdString(componentShowMapper.getText(this->componentDisplayMode)));
  }
}

void videoHandlerRGB::slotRGBFormatControlChanged()
{
  // What is the current selection?
  int idx = ui.rgbFormatComboBox->currentIndex();

  // The old format's number bytes per frame
  int64_t nrBytesOldFormat = getBytesPerFrame();

  if (idx == rgbPresetList.count())
  {
    DEBUG_RGB("videoHandlerRGB::slotRGBFormatControlChanged custom format");

    // The user selected the "custom format..." option
    videoHandlerRGBCustomFormatDialog dialog(this->srcPixelFormat);
    if (dialog.exec() == QDialog::Accepted)
      this->srcPixelFormat = dialog.getSelectedRGBFormat();

    // Check if the custom format it in the presets list. If not, add it
    int idx = rgbPresetList.indexOf(srcPixelFormat);
    if (idx == -1 && srcPixelFormat.isValid())
    {
      // Valid pixel format with is not in the list. Add it...
      rgbPresetList.append(srcPixelFormat);
      int                  nrItems = ui.rgbFormatComboBox->count();
      const QSignalBlocker blocker(ui.rgbFormatComboBox);
      ui.rgbFormatComboBox->insertItem(nrItems - 1,
                                       QString::fromStdString(srcPixelFormat.getName()));
      idx = rgbPresetList.indexOf(srcPixelFormat);
    }

    if (idx > 0)
    {
      // Format found. Set it without another call to this function.
      const QSignalBlocker blocker(ui.rgbFormatComboBox);
      ui.rgbFormatComboBox->setCurrentIndex(idx);
    }
  }
  else
  {
    // One of the preset formats was selected
    DEBUG_RGB("videoHandlerRGB::slotRGBFormatControlChanged set preset format");
    setSrcPixelFormat(rgbPresetList.at(idx));
  }

  // Set the current frame in the buffer to be invalid and clear the cache.
  // Emit that this item needs redraw and the cache needs updating.
  currentImageIndex = -1;
  if (nrBytesOldFormat != getBytesPerFrame())
  {
    DEBUG_RGB("videoHandlerRGB::slotRGBFormatControlChanged nr bytes per frame changed");
    invalidateAllBuffers();
  }
  setCacheInvalid();
  emit signalHandlerChanged(true, RECACHE_CLEAR);
}

void videoHandlerRGB::loadFrame(int frameIndex, bool loadToDoubleBuffer)
{
  DEBUG_RGB("videoHandlerRGB::loadFrame %d", frameIndex);

  if (!isFormatValid())
  {
    DEBUG_RGB("videoHandlerRGB::loadFrame invalid pixel format");
    return;
  }

  // Does the data in currentFrameRawData need to be updated?
  if (!loadRawRGBData(frameIndex) || currentFrameRawData.isEmpty())
  {
    DEBUG_RGB("videoHandlerRGB::loadFrame Loading faile or is still running in the background");
    return;
  }

  // The data in currentFrameRawData is now up to date. If necessary
  // convert the data to RGB.
  if (loadToDoubleBuffer)
  {
    QImage newImage;
    convertRGBToImage(currentFrameRawData, newImage);
    doubleBufferImage           = newImage;
    doubleBufferImageFrameIndex = frameIndex;
  }
  else if (currentImageIndex != frameIndex)
  {
    QImage newImage;
    convertRGBToImage(currentFrameRawData, newImage);
    QMutexLocker writeLock(&currentImageSetMutex);
    currentImage      = newImage;
    currentImageIndex = frameIndex;
  }
}

void videoHandlerRGB::savePlaylist(YUViewDomElement &element) const
{
  frameHandler::savePlaylist(element);
  element.appendProperiteChild("pixelFormat", this->getRawRGBPixelFormatName());

  element.appendProperiteChild("componentShow",
                               componentShowMapper.getName(this->componentDisplayMode));

  element.appendProperiteChild("scale.R", QString::number(this->componentScale[0]));
  element.appendProperiteChild("scale.G", QString::number(this->componentScale[1]));
  element.appendProperiteChild("scale.B", QString::number(this->componentScale[2]));
  element.appendProperiteChild("scale.A", QString::number(this->componentScale[3]));

  element.appendProperiteChild("invert.R", functions::boolToString(this->componentInvert[0]));
  element.appendProperiteChild("invert.G", functions::boolToString(this->componentInvert[1]));
  element.appendProperiteChild("invert.B", functions::boolToString(this->componentInvert[2]));
  element.appendProperiteChild("invert.A", functions::boolToString(this->componentInvert[3]));

  element.appendProperiteChild("limitedRange", functions::boolToString(this->limitedRange));
}

void videoHandlerRGB::loadPlaylist(const YUViewDomElement &element)
{
  frameHandler::loadPlaylist(element);
  QString sourcePixelFormat = element.findChildValue("pixelFormat");
  this->setRGBPixelFormatByName(sourcePixelFormat);

  auto showVal = element.findChildValue("componentShow");
  if (auto c = componentShowMapper.getValue(showVal.toStdString()))
    this->componentDisplayMode = *c;

  auto scaleR = element.findChildValue("scale.R");
  if (!scaleR.isEmpty())
    this->componentScale[0] = scaleR.toInt();
  auto scaleG = element.findChildValue("scale.G");
  if (!scaleG.isEmpty())
    this->componentScale[1] = scaleG.toInt();
  auto scaleB = element.findChildValue("scale.B");
  if (!scaleB.isEmpty())
    this->componentScale[2] = scaleB.toInt();
  auto scaleA = element.findChildValue("scale.A");
  if (!scaleA.isEmpty())
    this->componentScale[3] = scaleA.toInt();

  this->componentInvert[0] = (element.findChildValue("invert.R") == "True");
  this->componentInvert[1] = (element.findChildValue("invert.G") == "True");
  this->componentInvert[2] = (element.findChildValue("invert.B") == "True");
  this->componentInvert[3] = (element.findChildValue("invert.A") == "True");

  this->limitedRange = (element.findChildValue("limitedRange") == "True");
}

void videoHandlerRGB::loadFrameForCaching(int frameIndex, QImage &frameToCache)
{
  DEBUG_RGB("videoHandlerRGB::loadFrameForCaching %d", frameIndex);

  // Lock the mutex for the rgbFormat. The main thread has to wait until caching is done
  // before the RGB format can change.
  rgbFormatMutex.lock();

  requestDataMutex.lock();
  emit signalRequestRawData(frameIndex, true);
  tmpBufferRawRGBDataCaching = rawData;
  requestDataMutex.unlock();

  if (frameIndex != rawData_frameIndex)
  {
    // Loading failed
    currentImageIndex = -1;
    rgbFormatMutex.unlock();
    return;
  }

  // Convert RGB to image. This can then be cached.
  convertRGBToImage(tmpBufferRawRGBDataCaching, frameToCache);

  rgbFormatMutex.unlock();
}

// Load the raw RGB data for the given frame index into currentFrameRawData.
bool videoHandlerRGB::loadRawRGBData(int frameIndex)
{
  DEBUG_RGB("videoHandlerRGB::loadRawRGBData frame %d", frameIndex);

  if (currentFrameRawData_frameIndex == frameIndex && cacheValid)
  {
    DEBUG_RGB("videoHandlerRGB::loadRawRGBData frame %d already in the current buffer - Done",
              frameIndex);
    return true;
  }

  if (frameIndex == rawData_frameIndex)
  {
    // The raw data was loaded in the background. Now we just have to move it to the current
    // buffer. No actual loading is needed.
    requestDataMutex.lock();
    currentFrameRawData            = rawData;
    currentFrameRawData_frameIndex = frameIndex;
    requestDataMutex.unlock();
    return true;
  }

  DEBUG_RGB("videoHandlerRGB::loadRawRGBData %d", frameIndex);

  // The function loadFrameForCaching also uses the signalRequestRawData to request raw data.
  // However, only one thread can use this at a time.
  requestDataMutex.lock();
  emit signalRequestRawData(frameIndex, false);
  if (frameIndex == rawData_frameIndex)
  {
    currentFrameRawData            = rawData;
    currentFrameRawData_frameIndex = frameIndex;
  }
  requestDataMutex.unlock();

  DEBUG_RGB("videoHandlerRGB::loadRawRGBData %d %s",
            frameIndex,
            (frameIndex == rawData_frameIndex) ? "NewDataSet" : "Waiting...");
  return (currentFrameRawData_frameIndex == frameIndex);
}

// Convert the given raw RGB data in sourceBuffer (using srcPixelFormat) to image (RGB-888), using
// the buffer tmpRGBBuffer for intermediate RGB values.
void videoHandlerRGB::convertRGBToImage(const QByteArray &sourceBuffer, QImage &outputImage)
{
  DEBUG_RGB("videoHandlerRGB::convertRGBToImage");
  auto curFrameSize = QSize(frameSize.width, frameSize.height);

  // Create the output image in the right format.
  // Check if we have to premultiply alpha. The format of the raw buffer is: BGRA
  // (each 8 bit). Internally, this is how QImage allocates the number of bytes per line (with depth
  // = 32): const int bytes_per_line = ((width * depth + 31) >> 5) << 2; // bytes per scanline (must
  // be multiple of 4)
  bool premultiplyAlpha = false;
  if (is_Q_OS_WIN)
  {
    outputImage      = QImage(curFrameSize, QImage::Format_ARGB32_Premultiplied);
    premultiplyAlpha = true;
  }
  else if (is_Q_OS_MAC)
    outputImage = QImage(curFrameSize, QImage::Format_RGB32);
  else if (is_Q_OS_LINUX)
  {
    auto f = functionsGui::platformImageFormat();
    if (f == QImage::Format_ARGB32_Premultiplied)
    {
      outputImage      = QImage(curFrameSize, QImage::Format_ARGB32_Premultiplied);
      premultiplyAlpha = true;
    }
    if (f == QImage::Format_ARGB32)
      outputImage = QImage(curFrameSize, QImage::Format_ARGB32);
    else
      outputImage = QImage(curFrameSize, QImage::Format_RGB32);
  }

  // Check the image buffer size before we write to it
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
  assert(functions::clipToUnsigned(outputImage.byteCount()) >=
         frameSize.width * frameSize.height * 4);
#else
  assert(functions::clipToUnsigned(outputImage.sizeInBytes()) >=
         frameSize.width * frameSize.height * 4);
#endif

  this->convertSourceToRGBA32Bit(sourceBuffer, outputImage.bits(), premultiplyAlpha);

  if (is_Q_OS_LINUX)
  {
    // On linux, we may have to convert the image to the platform image format if it is not one of
    // the RGBA formats.
    auto f = functionsGui::platformImageFormat();
    if (f != QImage::Format_ARGB32_Premultiplied && f != QImage::Format_ARGB32 &&
        f != QImage::Format_RGB32)
      outputImage = outputImage.convertToFormat(f);
  }
}

void videoHandlerRGB::setSrcPixelFormat(const rgbPixelFormat &newFormat)
{
  this->rgbFormatMutex.lock();
  this->srcPixelFormat = newFormat;
  this->updateControlsForNewPixelFormat();
  this->rgbFormatMutex.unlock();
}

// Convert the data in "sourceBuffer" from the format "srcPixelFormat" to RGB 888. While doing so,
// apply the scaling factors, inversions and only convert the selected color components.
void videoHandlerRGB::convertSourceToRGBA32Bit(const QByteArray &sourceBuffer,
                                               unsigned char *   targetBuffer,
                                               bool              premultiplyAlpha)
{
  // Check if the source buffer is of the correct size
  Q_ASSERT_X(sourceBuffer.size() >= getBytesPerFrame(),
             Q_FUNC_INFO,
             "The source buffer does not hold enough data.");

  // Get the raw data pointer to the output array
  unsigned char *restrict dst = targetBuffer;

  // How many values do we have to skip in src to get to the next input value?
  // In case of 8 or less bits this is 1 byte per value, for 9 to 16 bits it is 2 bytes per value.
  int offsetToNextValue = srcPixelFormat.nrChannels();
  if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
    offsetToNextValue = 1;

  const auto bitDepth = srcPixelFormat.getBitsPerSample();
  const auto posR     = srcPixelFormat.getComponentPosition(Channel::Red);
  const auto posG     = srcPixelFormat.getComponentPosition(Channel::Green);
  const auto posB     = srcPixelFormat.getComponentPosition(Channel::Blue);
  const auto posA     = srcPixelFormat.getComponentPosition(Channel::Alpha);

  if (componentDisplayMode != ComponentShow::RGB && componentDisplayMode != ComponentShow::RGBA)
  {
    // Only convert one of the components to a gray-scale image.
    // Consider inversion and scale of that component

    auto       componentMap = std::map<ComponentShow, Channel>({{ComponentShow::R, Channel::Red},
                                                          {ComponentShow::G, Channel::Green},
                                                          {ComponentShow::B, Channel::Blue},
                                                          {ComponentShow::A, Channel::Alpha}});
    const auto displayComponentOffset =
        srcPixelFormat.getComponentPosition(componentMap[componentDisplayMode]);

    auto       displayIndexMap = std::map<ComponentShow, unsigned>({{ComponentShow::R, 0},
                                                              {ComponentShow::G, 1},
                                                              {ComponentShow::B, 2},
                                                              {ComponentShow::A, 3}});
    const auto displayIndex    = displayIndexMap[componentDisplayMode];
    const auto scale           = componentScale[displayIndex];
    const auto invert          = componentInvert[displayIndex];

    if (bitDepth > 8 && bitDepth <= 16)
    {
      // 9 to 16 bits per component. We assume two bytes per value.

      // The source values have to be shifted left by this many bits to get 8 bit output
      const auto rightShift = bitDepth - 8;

      // First get the pointer to the first value that we will need.
      unsigned short *src = (unsigned short *)sourceBuffer.data();
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
        src += displayComponentOffset * frameSize.width * frameSize.height;
      else
        src += displayComponentOffset;

      // Now we just have to iterate over all values and always skip "offsetToNextValue" values in
      // src and write 3 values in dst.
      for (unsigned i = 0; i < frameSize.width * frameSize.height; i++)
      {
        int val = (((int)src[0]) * scale) >> rightShift;
        val     = clip(val, 0, 255);
        if (invert)
          val = 255 - val;
        if (limitedRange)
          val = videoHandler::convScaleLimitedRange(val);
        dst[0] = val;
        dst[1] = val;
        dst[2] = val;
        dst[3] = 255;

        src += offsetToNextValue;
        dst += 4;
      }
    }
    else if (bitDepth == 8)
    {
      // First get the pointer to the first value that we will need.
      unsigned char *src = (unsigned char *)sourceBuffer.data();
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
        src += displayComponentOffset * frameSize.width * frameSize.height;
      else
        src += displayComponentOffset;

      // Now we just have to iterate over all values and always skip "offsetToNextValue" values in
      // src and write 3 values in dst.
      for (unsigned i = 0; i < frameSize.width * frameSize.height; i++)
      {
        int val = ((int)src[0]) * scale;
        val     = clip(val, 0, 255);
        if (invert)
          val = 255 - val;
        if (limitedRange)
          val = videoHandler::convScaleLimitedRange(val);
        dst[0] = val;
        dst[1] = val;
        dst[2] = val;
        dst[3] = 255;

        src += offsetToNextValue;
        dst += 4;
      }
    }
    else
      Q_ASSERT_X(
          false, Q_FUNC_INFO, "No RGB format with less than 8 or more than 16 bits supported yet.");
  }
  else if (componentDisplayMode == ComponentShow::RGB ||
           componentDisplayMode == ComponentShow::RGBA)
  {
    // Convert all components from the source RGB format to an RGB 888 array

    const auto renderAlpha =
        componentDisplayMode == ComponentShow::RGBA && srcPixelFormat.hasAlpha();
    if (bitDepth > 8 && bitDepth <= 16)
    {
      // 9 to 16 bits per component. We assume two bytes per value.

      // The source values have to be shifted left by this many bits to get 8 bit output
      const auto rightShift = bitDepth - 8;

      // First get the pointer to the first value of each channel.
      unsigned short *srcR, *srcG, *srcB, *srcA;
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      {
        srcR = (unsigned short *)sourceBuffer.data() + (posR * frameSize.width * frameSize.height);
        srcG = (unsigned short *)sourceBuffer.data() + (posG * frameSize.width * frameSize.height);
        srcB = (unsigned short *)sourceBuffer.data() + (posB * frameSize.width * frameSize.height);
        srcA = (unsigned short *)sourceBuffer.data() + (posA * frameSize.width * frameSize.height);
      }
      else
      {
        srcR = (unsigned short *)sourceBuffer.data() + posR;
        srcG = (unsigned short *)sourceBuffer.data() + posG;
        srcB = (unsigned short *)sourceBuffer.data() + posB;
        srcA = (unsigned short *)sourceBuffer.data() + posA;
      }

      // Now we just have to iterate over all values and always skip "offsetToNextValue" values in
      // the sources and write 3 values in dst.
      for (unsigned i = 0; i < frameSize.width * frameSize.height; i++)
      {
        int valR = (((int)srcR[0]) * componentScale[0]) >> rightShift;
        valR     = clip(valR, 0, 255);
        if (componentInvert[0])
          valR = 255 - valR;

        int valG = (((int)srcG[0]) * componentScale[1]) >> rightShift;
        valG     = clip(valG, 0, 255);
        if (componentInvert[1])
          valG = 255 - valG;

        int valB = (((int)srcB[0]) * componentScale[2]) >> rightShift;
        valB     = clip(valB, 0, 255);
        if (componentInvert[2])
          valB = 255 - valB;

        int valA = 255;
        if (renderAlpha)
        {
          valA = (((int)srcA[0]) * componentScale[3]) >> rightShift;
          valA = clip(valA, 0, 255);
          if (componentInvert[3])
            valA = 255 - valA;
        }

        if (limitedRange)
        {
          valR = videoHandler::convScaleLimitedRange(valR);
          valG = videoHandler::convScaleLimitedRange(valG);
          valB = videoHandler::convScaleLimitedRange(valB);
          // No limited range for alpha
        }

        if (renderAlpha && premultiplyAlpha)
        {
          valR = ((valR * 255) * valA) / (255 * 255);
          valG = ((valG * 255) * valA) / (255 * 255);
          valB = ((valB * 255) * valA) / (255 * 255);
        }

        srcR += offsetToNextValue;
        srcG += offsetToNextValue;
        srcB += offsetToNextValue;
        srcA += offsetToNextValue;

        dst[0] = valB;
        dst[1] = valG;
        dst[2] = valR;
        dst[3] = valA;
        dst += 4;
      }
    }
    else if (bitDepth == 8)
    {
      // First get the pointer to the first value of each channel.
      unsigned char *srcR, *srcG, *srcB, *srcA;
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      {
        srcR = (unsigned char *)sourceBuffer.data() + (posR * frameSize.width * frameSize.height);
        srcG = (unsigned char *)sourceBuffer.data() + (posG * frameSize.width * frameSize.height);
        srcB = (unsigned char *)sourceBuffer.data() + (posB * frameSize.width * frameSize.height);
        srcA = (unsigned char *)sourceBuffer.data() + (posA * frameSize.width * frameSize.height);
      }
      else
      {
        srcR = (unsigned char *)sourceBuffer.data() + posR;
        srcG = (unsigned char *)sourceBuffer.data() + posG;
        srcB = (unsigned char *)sourceBuffer.data() + posB;
        srcA = (unsigned char *)sourceBuffer.data() + posA;
      }

      // Now we just have to iterate over all values and always skip "offsetToNextValue" values in
      // the sources and write 3 values in dst.
      for (unsigned i = 0; i < frameSize.width * frameSize.height; i++)
      {
        int valR = ((int)srcR[0]) * componentScale[0];
        valR     = clip(valR, 0, 255);
        if (componentInvert[0])
          valR = 255 - valR;

        int valG = ((int)srcG[0]) * componentScale[1];
        valG     = clip(valG, 0, 255);
        if (componentInvert[1])
          valG = 255 - valG;

        int valB = ((int)srcB[0]) * componentScale[2];
        valB     = clip(valB, 0, 255);
        if (componentInvert[2])
          valB = 255 - valB;

        int valA = 255;
        if (renderAlpha)
        {
          valA = ((int)srcA[0]) * componentScale[3];
          valA = clip(valA, 0, 255);
          if (componentInvert[3])
            valA = 255 - valA;
        }

        if (limitedRange)
        {
          valR = videoHandler::convScaleLimitedRange(valR);
          valG = videoHandler::convScaleLimitedRange(valG);
          valB = videoHandler::convScaleLimitedRange(valB);
          // No limited range for alpha
        }

        if (renderAlpha && premultiplyAlpha)
        {
          valR = ((valR * 255) * valA) / (255 * 255);
          valG = ((valG * 255) * valA) / (255 * 255);
          valB = ((valB * 255) * valA) / (255 * 255);
        }

        srcR += offsetToNextValue;
        srcG += offsetToNextValue;
        srcB += offsetToNextValue;
        srcA += offsetToNextValue;

        dst[0] = valB;
        dst[1] = valG;
        dst[2] = valR;
        dst[3] = valA;
        dst += 4;
      }
    }
    else
      Q_ASSERT_X(
          false, Q_FUNC_INFO, "No RGB format with less than 8 or more than 16 bits supported yet.");
  }
  else
    Q_ASSERT_X(false, Q_FUNC_INFO, "Unsupported display mode.");
}

videoHandlerRGB::rgba_t videoHandlerRGB::getPixelValue(const QPoint &pixelPos) const
{
  const unsigned int offsetCoordinate = frameSize.width * pixelPos.y() + pixelPos.x();

  // How many values do we have to skip in src to get to the next input value?
  // In case of 8 or less bits this is 1 byte per value, for 9 to 16 bits it is 2 bytes per value.
  int offsetToNextValue = srcPixelFormat.nrChannels();
  if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
    offsetToNextValue = 1;

  rgba_t value{0, 0, 0, 0};

  const auto bitDept = srcPixelFormat.getBitsPerSample();
  const auto posR    = srcPixelFormat.getComponentPosition(Channel::Red);
  const auto posG    = srcPixelFormat.getComponentPosition(Channel::Green);
  const auto posB    = srcPixelFormat.getComponentPosition(Channel::Blue);
  const auto posA    = srcPixelFormat.getComponentPosition(Channel::Alpha);

  if (bitDept > 8 && bitDept <= 16)
  {
    // First get the pointer to the first value of each channel.
    unsigned short *srcR = nullptr, *srcG = nullptr, *srcB = nullptr, *srcA = nullptr;
    if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
    {
      srcR = (unsigned short *)currentFrameRawData.data() +
             (posR * frameSize.width * frameSize.height);
      srcG = (unsigned short *)currentFrameRawData.data() +
             (posG * frameSize.width * frameSize.height);
      srcB = (unsigned short *)currentFrameRawData.data() +
             (posB * frameSize.width * frameSize.height);
      if (srcPixelFormat.hasAlpha())
        srcA = (unsigned short *)currentFrameRawData.data() +
               (posA * frameSize.width * frameSize.height);
    }
    else
    {
      srcR = (unsigned short *)currentFrameRawData.data() + posR;
      srcG = (unsigned short *)currentFrameRawData.data() + posG;
      srcB = (unsigned short *)currentFrameRawData.data() + posB;
      if (srcPixelFormat.hasAlpha())
        srcA = (unsigned short *)currentFrameRawData.data() + posA;
    }

    value.R = (unsigned int)(*(srcR + offsetToNextValue * offsetCoordinate));
    value.G = (unsigned int)(*(srcG + offsetToNextValue * offsetCoordinate));
    value.B = (unsigned int)(*(srcB + offsetToNextValue * offsetCoordinate));
    if (srcPixelFormat.hasAlpha())
      value.A = (unsigned int)(*(srcA + offsetToNextValue * offsetCoordinate));
  }
  else if (bitDept == 8)
  {
    // First get the pointer to the first value of each channel.
    unsigned char *srcR = nullptr, *srcG = nullptr, *srcB = nullptr, *srcA = nullptr;
    if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
    {
      srcR =
          (unsigned char *)currentFrameRawData.data() + (posR * frameSize.width * frameSize.height);
      srcG =
          (unsigned char *)currentFrameRawData.data() + (posG * frameSize.width * frameSize.height);
      srcB =
          (unsigned char *)currentFrameRawData.data() + (posB * frameSize.width * frameSize.height);
      if (srcPixelFormat.hasAlpha())
        srcA = (unsigned char *)currentFrameRawData.data() +
               (posA * frameSize.width * frameSize.height);
    }
    else
    {
      srcR = (unsigned char *)currentFrameRawData.data() + posR;
      srcG = (unsigned char *)currentFrameRawData.data() + posG;
      srcB = (unsigned char *)currentFrameRawData.data() + posB;
      if (srcPixelFormat.hasAlpha())
        srcA = (unsigned char *)currentFrameRawData.data() + posA;
    }

    value.R = (unsigned int)(*(srcR + offsetToNextValue * offsetCoordinate));
    value.G = (unsigned int)(*(srcG + offsetToNextValue * offsetCoordinate));
    value.B = (unsigned int)(*(srcB + offsetToNextValue * offsetCoordinate));
    if (srcPixelFormat.hasAlpha())
      value.A = (unsigned int)(*(srcA + offsetToNextValue * offsetCoordinate));
  }
  else
    Q_ASSERT_X(
        false, Q_FUNC_INFO, "No RGB format with less than 8 or more than 16 bits supported yet.");

  return value;
}

void videoHandlerRGB::setFormatFromSizeAndName(const Size       size,
                                               int              bitDepth,
                                               DataLayout       dataLayout,
                                               int64_t          fileSize,
                                               const QFileInfo &fileInfo)
{
  // Get the file extension
  auto        ext       = fileInfo.suffix().toLower().toStdString();
  std::string subFormat = "rgb";
  bool        testAlpha = true;
  if (ext == "bgr" || ext == "gbr" || ext == "brg" || ext == "grb" || ext == "rbg")
  {
    subFormat = ext;
    testAlpha = false;
  }
  else
  {
    // Check if there is a format indicator in the file name
    auto f               = fileInfo.fileName().toLower().toStdString();
    auto rgbCombinations = std::vector<std::string>({"rgb", "rgb", "gbr", "grb", "brg", "bgr"});
    std::vector<std::string> rgbaCombinations;
    for (auto i : rgbCombinations)
    {
      rgbaCombinations.push_back("a" + i);
      rgbaCombinations.push_back(i + "a");
      rgbaCombinations.push_back(i);
    }
    for (auto i : rgbaCombinations)
    {
      if (f.find("_" + i) != std::string::npos || f.find(" " + i) != std::string::npos)
      {
        subFormat = i;
        testAlpha = false;
        break;
      }
    }
  }

  // If the bit depth could not be determined, check 8 and 10 bit
  std::vector<int> testBitDepths;
  if (bitDepth > 0)
    testBitDepths.push_back(bitDepth);
  else
    testBitDepths = {8, 10};

  for (auto bitDepth : testBitDepths)
  {
    // If testAlpha is set, we will test with and without alpha channel
    auto nrTests = testAlpha ? 2u : 1u;
    for (unsigned int i = 0; i < nrTests; i++)
    {
      // assume RGB if subFormat does not indicate anything else
      auto cFormat = rgbPixelFormat(i == 0 ? subFormat : subFormat + "a");
      cFormat.setBitsPerSample(bitDepth);
      cFormat.setDataLayout(dataLayout);

      // Check if the file size and the assumed format match
      auto bpf = cFormat.bytesPerFrame(size);
      if (bpf != 0 && (fileSize % bpf) == 0)
      {
        // Bits per frame and file size match
        this->setFrameSize(size);
        this->setSrcPixelFormat(cFormat);
        return;
      }
    }
  }

  // Still no match. Set RGB 8 bit planar no alpha channel.
  // This will probably be wrong but we are out of options
  this->setSrcPixelFormat(rgbPixelFormat(8, dataLayout, ChannelOrder::RGB));
}

void videoHandlerRGB::drawPixelValues(QPainter *    painter,
                                      const int     frameIdx,
                                      const QRect & videoRect,
                                      const double  zoomFactor,
                                      frameHandler *item2,
                                      const bool    markDifference,
                                      const int     frameIdxItem1)
{
  // First determine which pixels from this item are actually visible, because we only have to draw
  // the pixel values of the pixels that are actually visible
  auto viewport       = painter->viewport();
  auto worldTransform = painter->worldTransform();

  int xMin = (videoRect.width() / 2 - worldTransform.dx()) / zoomFactor;
  int yMin = (videoRect.height() / 2 - worldTransform.dy()) / zoomFactor;
  int xMax = (videoRect.width() / 2 - (worldTransform.dx() - viewport.width())) / zoomFactor;
  int yMax = (videoRect.height() / 2 - (worldTransform.dy() - viewport.height())) / zoomFactor;

  // Clip the min/max visible pixel values to the size of the item (no pixels outside of the
  // item have to be labeled)
  xMin = clip(xMin, 0, int(frameSize.width) - 1);
  yMin = clip(yMin, 0, int(frameSize.height) - 1);
  xMax = clip(xMax, 0, int(frameSize.width) - 1);
  yMax = clip(yMax, 0, int(frameSize.height) - 1);

  // Get the other RGB item (if any)
  auto rgbItem2 = dynamic_cast<videoHandlerRGB *>(item2);
  if (item2 != nullptr && rgbItem2 == nullptr)
  {
    // The second item is not a videoHandlerRGB item
    frameHandler::drawPixelValues(
        painter, frameIdx, videoRect, zoomFactor, item2, markDifference, frameIdxItem1);
    return;
  }

  // Check if the raw RGB values are up to date. If not, do not draw them. Do not trigger loading of
  // data here. The needsLoadingRawValues function will return that loading is needed. The caching
  // in the background should then trigger loading of them.
  if (currentFrameRawData_frameIndex != frameIdx)
    return;
  if (rgbItem2 && rgbItem2->currentFrameRawData_frameIndex != frameIdxItem1)
    return;

  // The center point of the pixel (0,0).
  auto centerPointZero = (QPoint(-(int(frameSize.width)), -(int(frameSize.height))) * zoomFactor +
                            QPoint(zoomFactor, zoomFactor)) /
                           2;
  // This QRect has the size of one pixel and is moved on top of each pixel to draw the text
  QRect pixelRect;
  pixelRect.setSize(QSize(zoomFactor, zoomFactor));
  const unsigned drawWhitLevel = 1 << (srcPixelFormat.getBitsPerSample() - 1);
  for (int x = xMin; x <= xMax; x++)
  {
    for (int y = yMin; y <= yMax; y++)
    {
      // Calculate the center point of the pixel. (Each pixel is of size (zoomFactor,zoomFactor))
      // and move the pixelRect to that point.
      QPoint pixCenter = centerPointZero + QPoint(x * zoomFactor, y * zoomFactor);
      pixelRect.moveCenter(pixCenter);

      // Get the text to show
      QString   valText;
      const int formatBase = settings.value("ShowPixelValuesHex").toBool() ? 16 : 10;
      if (rgbItem2 != nullptr)
      {
        rgba_t valueThis  = getPixelValue(QPoint(x, y));
        rgba_t valueOther = rgbItem2->getPixelValue(QPoint(x, y));

        const int     R       = int(valueThis.R) - int(valueOther.R);
        const int     G       = int(valueThis.G) - int(valueOther.G);
        const int     B       = int(valueThis.B) - int(valueOther.B);
        const int     A       = int(valueThis.A) - int(valueOther.A);
        const QString RString = ((R < 0) ? "-" : "") + QString::number(std::abs(R), formatBase);
        const QString GString = ((G < 0) ? "-" : "") + QString::number(std::abs(G), formatBase);
        const QString BString = ((B < 0) ? "-" : "") + QString::number(std::abs(B), formatBase);

        if (markDifference)
          painter->setPen((R == 0 && G == 0 && B == 0 && (!srcPixelFormat.hasAlpha() || A == 0))
                              ? Qt::white
                              : Qt::black);
        else
          painter->setPen((R < 0 && G < 0 && B < 0) ? Qt::white : Qt::black);

        if (srcPixelFormat.hasAlpha())
        {
          const QString AString = ((A < 0) ? "-" : "") + QString::number(std::abs(A), formatBase);
          valText = QString("R%1\nG%2\nB%3\nA%4").arg(RString, GString, BString, AString);
        }
        else
          valText = QString("R%1\nG%2\nB%3").arg(RString, GString, BString);
      }
      else
      {
        rgba_t value = getPixelValue(QPoint(x, y));
        if (srcPixelFormat.hasAlpha())
          valText = QString("R%1\nG%2\nB%3\nA%4")
                        .arg(value.R, 0, formatBase)
                        .arg(value.G, 0, formatBase)
                        .arg(value.B, 0, formatBase)
                        .arg(value.A, 0, formatBase);
        else
          valText = QString("R%1\nG%2\nB%3")
                        .arg(value.R, 0, formatBase)
                        .arg(value.G, 0, formatBase)
                        .arg(value.B, 0, formatBase);
        painter->setPen(
            (value.R < drawWhitLevel && value.G < drawWhitLevel && value.B < drawWhitLevel)
                ? Qt::white
                : Qt::black);
      }

      painter->drawText(pixelRect, Qt::AlignCenter, valText);
    }
  }
}

QImage videoHandlerRGB::calculateDifference(frameHandler *   item2,
                                            const int        frameIdxItem0,
                                            const int        frameIdxItem1,
                                            QList<InfoItem> &differenceInfoList,
                                            const int        amplificationFactor,
                                            const bool       markDifference)
{
  videoHandlerRGB *rgbItem2 = dynamic_cast<videoHandlerRGB *>(item2);
  if (rgbItem2 == nullptr)
    // The given item is not a RGB source. We cannot compare raw RGB values to non raw RGB values.
    // Call the base class comparison function to compare the items using the RGB 888 values.
    return videoHandler::calculateDifference(item2,
                                             frameIdxItem0,
                                             frameIdxItem1,
                                             differenceInfoList,
                                             amplificationFactor,
                                             markDifference);

  if (srcPixelFormat.getBitsPerSample() != rgbItem2->srcPixelFormat.getBitsPerSample())
    // The two items have different bit depths. Compare RGB 888 values instead.
    return videoHandler::calculateDifference(item2,
                                             frameIdxItem0,
                                             frameIdxItem1,
                                             differenceInfoList,
                                             amplificationFactor,
                                             markDifference);

  const int width  = std::min(frameSize.width, rgbItem2->frameSize.width);
  const int height = std::min(frameSize.height, rgbItem2->frameSize.height);

  // Load the right raw RGB data (if not already loaded).
  // This will just update the raw RGB data. No conversion to image (RGB) is performed. This is
  // either done on request if the frame is actually shown or has already been done by the caching
  // process.
  if (!loadRawRGBData(frameIdxItem0))
    return QImage(); // Loading failed
  if (!rgbItem2->loadRawRGBData(frameIdxItem1))
    return QImage(); // Loading failed

  // Also calculate the MSE while we're at it (R,G,B)
  int64_t mseAdd[3] = {0, 0, 0};

  // Create the output image in the right format
  // In both cases, we will set the alpha channel to 255. The format of the raw buffer is: BGRA
  // (each 8 bit).
  QImage outputImage;
  auto   qFrameSize = QSize(frameSize.width, frameSize.height);
  if (is_Q_OS_WIN)
    outputImage = QImage(qFrameSize, QImage::Format_ARGB32_Premultiplied);
  else if (is_Q_OS_MAC)
    outputImage = QImage(qFrameSize, QImage::Format_RGB32);
  else if (is_Q_OS_LINUX)
  {
    auto f = functionsGui::platformImageFormat();
    if (f == QImage::Format_ARGB32_Premultiplied)
      outputImage = QImage(qFrameSize, QImage::Format_ARGB32_Premultiplied);
    if (f == QImage::Format_ARGB32)
      outputImage = QImage(qFrameSize, QImage::Format_ARGB32);
    else
      outputImage = QImage(qFrameSize, QImage::Format_RGB32);
  }

  // We directly write the difference values into the QImage buffer in the right format (ABGR).
  unsigned char *restrict dst = outputImage.bits();

  const auto bitDepth = srcPixelFormat.getBitsPerSample();
  const auto posR     = srcPixelFormat.getComponentPosition(Channel::Red);
  const auto posG     = srcPixelFormat.getComponentPosition(Channel::Green);
  const auto posB     = srcPixelFormat.getComponentPosition(Channel::Blue);

  if (bitDepth >= 8 && bitDepth <= 16)
  {
    // How many values do we have to skip in src to get to the next input value?
    // In case of 8 or less bits this is 1 byte per value, for 9 to 16 bits it is 2 bytes per value.
    int offsetToNextValue = srcPixelFormat.nrChannels();
    if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      offsetToNextValue = 1;

    if (bitDepth > 8 && bitDepth <= 16)
    {
      // 9 to 16 bits per component. We assume two bytes per value.
      // First get the pointer to the first value of each channel. (this item)
      unsigned short *srcR0, *srcG0, *srcB0;
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      {
        srcR0 = (unsigned short *)currentFrameRawData.data() +
                (posR * frameSize.width * frameSize.height);
        srcG0 = (unsigned short *)currentFrameRawData.data() +
                (posG * frameSize.width * frameSize.height);
        srcB0 = (unsigned short *)currentFrameRawData.data() +
                (posB * frameSize.width * frameSize.height);
      }
      else
      {
        srcR0 = (unsigned short *)currentFrameRawData.data() + posR;
        srcG0 = (unsigned short *)currentFrameRawData.data() + posG;
        srcB0 = (unsigned short *)currentFrameRawData.data() + posB;
      }

      // Next get the pointer to the first value of each channel. (the other item)
      unsigned short *srcR1, *srcG1, *srcB1;
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      {
        srcR1 = (unsigned short *)rgbItem2->currentFrameRawData.data() +
                (posR * rgbItem2->frameSize.width * rgbItem2->frameSize.height);
        srcG1 = (unsigned short *)rgbItem2->currentFrameRawData.data() +
                (posG * rgbItem2->frameSize.width * rgbItem2->frameSize.height);
        srcB1 = (unsigned short *)rgbItem2->currentFrameRawData.data() +
                (posB * rgbItem2->frameSize.width * rgbItem2->frameSize.height);
      }
      else
      {
        srcR1 = (unsigned short *)rgbItem2->currentFrameRawData.data() + posR;
        srcG1 = (unsigned short *)rgbItem2->currentFrameRawData.data() + posG;
        srcB1 = (unsigned short *)rgbItem2->currentFrameRawData.data() + posB;
      }

      for (int y = 0; y < height; y++)
      {
        for (int x = 0; x < width; x++)
        {
          unsigned int offsetCoordinate = frameSize.width * y + x;

          unsigned int R0 = (unsigned int)(*(srcR0 + offsetToNextValue * offsetCoordinate));
          unsigned int G0 = (unsigned int)(*(srcG0 + offsetToNextValue * offsetCoordinate));
          unsigned int B0 = (unsigned int)(*(srcB0 + offsetToNextValue * offsetCoordinate));

          unsigned int R1 = (unsigned int)(*(srcR1 + offsetToNextValue * offsetCoordinate));
          unsigned int G1 = (unsigned int)(*(srcG1 + offsetToNextValue * offsetCoordinate));
          unsigned int B1 = (unsigned int)(*(srcB1 + offsetToNextValue * offsetCoordinate));

          int deltaR = R0 - R1;
          int deltaG = G0 - G1;
          int deltaB = B0 - B1;

          mseAdd[0] += deltaR * deltaR;
          mseAdd[1] += deltaG * deltaG;
          mseAdd[2] += deltaB * deltaB;

          if (markDifference)
          {
            // Just mark if there is a difference
            dst[0] = (deltaB == 0) ? 0 : 255;
            dst[1] = (deltaG == 0) ? 0 : 255;
            dst[2] = (deltaR == 0) ? 0 : 255;
          }
          else
          {
            // We want to see the difference
            dst[0] = clip(128 + deltaB * amplificationFactor, 0, 255);
            dst[1] = clip(128 + deltaG * amplificationFactor, 0, 255);
            dst[2] = clip(128 + deltaR * amplificationFactor, 0, 255);
          }
          dst[3] = 255;
          dst += 4;
        }
      }
    }
    else if (bitDepth == 8)
    {
      // First get the pointer to the first value of each channel. (this item)
      unsigned char *srcR0, *srcG0, *srcB0;
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      {
        srcR0 = (unsigned char *)currentFrameRawData.data() +
                (posR * frameSize.width * frameSize.height);
        srcG0 = (unsigned char *)currentFrameRawData.data() +
                (posG * frameSize.width * frameSize.height);
        srcB0 = (unsigned char *)currentFrameRawData.data() +
                (posB * frameSize.width * frameSize.height);
      }
      else
      {
        srcR0 = (unsigned char *)currentFrameRawData.data() + posR;
        srcG0 = (unsigned char *)currentFrameRawData.data() + posG;
        srcB0 = (unsigned char *)currentFrameRawData.data() + posB;
      }

      // First get the pointer to the first value of each channel. (other item)
      unsigned char *srcR1, *srcG1, *srcB1;
      if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      {
        srcR1 = (unsigned char *)rgbItem2->currentFrameRawData.data() +
                (posR * rgbItem2->frameSize.width * rgbItem2->frameSize.height);
        srcG1 = (unsigned char *)rgbItem2->currentFrameRawData.data() +
                (posG * rgbItem2->frameSize.width * rgbItem2->frameSize.height);
        srcB1 = (unsigned char *)rgbItem2->currentFrameRawData.data() +
                (posB * rgbItem2->frameSize.width * rgbItem2->frameSize.height);
      }
      else
      {
        srcR1 = (unsigned char *)rgbItem2->currentFrameRawData.data() + posR;
        srcG1 = (unsigned char *)rgbItem2->currentFrameRawData.data() + posG;
        srcB1 = (unsigned char *)rgbItem2->currentFrameRawData.data() + posB;
      }

      for (int y = 0; y < height; y++)
      {
        for (int x = 0; x < width; x++)
        {
          unsigned int offsetCoordinate = frameSize.width * y + x;

          unsigned int R0 = (unsigned int)(*(srcR0 + offsetToNextValue * offsetCoordinate));
          unsigned int G0 = (unsigned int)(*(srcG0 + offsetToNextValue * offsetCoordinate));
          unsigned int B0 = (unsigned int)(*(srcB0 + offsetToNextValue * offsetCoordinate));

          unsigned int R1 = (unsigned int)(*(srcR1 + offsetToNextValue * offsetCoordinate));
          unsigned int G1 = (unsigned int)(*(srcG1 + offsetToNextValue * offsetCoordinate));
          unsigned int B1 = (unsigned int)(*(srcB1 + offsetToNextValue * offsetCoordinate));

          int deltaR = R0 - R1;
          int deltaG = G0 - G1;
          int deltaB = B0 - B1;

          mseAdd[0] += deltaR * deltaR;
          mseAdd[1] += deltaG * deltaG;
          mseAdd[2] += deltaB * deltaB;

          if (markDifference)
          {
            // Just mark if there is a difference
            dst[0] = (deltaB == 0) ? 0 : 255;
            dst[1] = (deltaG == 0) ? 0 : 255;
            dst[2] = (deltaR == 0) ? 0 : 255;
          }
          else
          {
            // We want to see the difference
            dst[0] = clip(128 + deltaB * amplificationFactor, 0, 255);
            dst[1] = clip(128 + deltaG * amplificationFactor, 0, 255);
            dst[2] = clip(128 + deltaR * amplificationFactor, 0, 255);
          }
          dst[3] = 255;
          dst += 4;
        }
      }
    }
    else
      Q_ASSERT_X(
          false, Q_FUNC_INFO, "No RGB format with less than 8 or more than 16 bits supported yet.");
  }

  // Append the conversion information that will be returned
  differenceInfoList.append(InfoItem("Difference Type", QString("RGB %1bit").arg(bitDepth)));
  double mse[4];
  mse[0] = double(mseAdd[0]) / (width * height);
  mse[1] = double(mseAdd[1]) / (width * height);
  mse[2] = double(mseAdd[2]) / (width * height);
  mse[3] = mse[0] + mse[1] + mse[2];
  differenceInfoList.append(InfoItem("MSE R", QString("%1").arg(mse[0])));
  differenceInfoList.append(InfoItem("MSE G", QString("%1").arg(mse[1])));
  differenceInfoList.append(InfoItem("MSE B", QString("%1").arg(mse[2])));
  differenceInfoList.append(InfoItem("MSE All", QString("%1").arg(mse[3])));

  if (is_Q_OS_LINUX)
  {
    // On linux, we may have to convert the image to the platform image format if it is not one of
    // the RGBA formats.
    auto f = functionsGui::platformImageFormat();
    if (f != QImage::Format_ARGB32_Premultiplied && f != QImage::Format_ARGB32 &&
        f != QImage::Format_RGB32)
      return outputImage.convertToFormat(f);
  }
  return outputImage;
}

} // namespace video
