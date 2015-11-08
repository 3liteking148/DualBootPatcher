/*
 * Copyright (C) 2014-2015  Andrew Gunnerson <andrewgunnerson@gmail.com>
 *
 * This file is part of MultiBootPatcher
 *
 * MultiBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bootimage.h"

#include <algorithm>

#include <cstring>

#include "libmbpio/file.h"

#include "bootimage/androidformat.h"
#include "bootimage/bumpformat.h"
#include "bootimage/lokiformat.h"
#include "bootimage/mtkformat.h"
#include "bootimage/sonyelfformat.h"

#include "private/fileutils.h"
#include "private/logging.h"


namespace mbp
{

const char *BootImage::AndroidBootMagic = BOOT_MAGIC;
const uint32_t BootImage::AndroidBootMagicSize = BOOT_MAGIC_SIZE;
const uint32_t BootImage::AndroidBootNameSize = BOOT_NAME_SIZE;
const uint32_t BootImage::AndroidBootArgsSize = BOOT_ARGS_SIZE;

// Universal defaults
const char *BootImage::DefaultCmdline = "";

// Android-based boot image defaults
const char *BootImage::AndroidDefaultBoard = "";
const uint32_t BootImage::AndroidDefaultPageSize = 2048u;
const uint32_t BootImage::AndroidDefaultBase = 0x10000000u;
const uint32_t BootImage::AndroidDefaultKernelOffset = 0x00008000u;
const uint32_t BootImage::AndroidDefaultRamdiskOffset = 0x01000000u;
const uint32_t BootImage::AndroidDefaultSecondOffset = 0x00f00000u;
const uint32_t BootImage::AndroidDefaultTagsOffset = 0x00000100u;

// Sony ELF boot image defaults
const uint32_t BootImage::SonyElfDefaultKernelAddress = 0u;
const uint32_t BootImage::SonyElfDefaultRamdiskAddress = 0u;
const uint32_t BootImage::SonyElfDefaultIplAddress = 0u;
const uint32_t BootImage::SonyElfDefaultRpmAddress = 0u;
const uint32_t BootImage::SonyElfDefaultAppsblAddress = 0u;
const uint32_t BootImage::SonyElfDefaultEntrypointAddress = 0u;


/*! \cond INTERNAL */
class BootImage::Impl
{
public:
    BootImageIntermediate i10e;
    BootImage::Type type = Type::Android;
    BootImage::Type sourceType;

    ErrorCode error;
};
/*! \endcond */


/*!
 * \class BootImage
 * \brief Handles the creation and manipulation of Android boot images
 *
 * BootImage provides a complete implementation of the following formats:
 *
 * | Format           | Extract | Create |
 * |------------------|---------|--------|
 * | Android          | Yes     | Yes    |
 * | Loki (old-style) | Yes     | No     | (Will be created as new-style)
 * | Loki (new-style) | Yes     | Yes    |
 * | Bump             | Yes     | Yes    |
 * | Mtk              | Yes     | Yes    |
 * | Sony             | Yes     | Yes    |
 *
 * The following parameters in the Android header can be changed:
 *
 * - Board name (truncated if length > 16)
 * - Kernel cmdline (truncated if length > 512)
 * - Page size
 * - Kernel address [1]
 * - Ramdisk address [1]
 * - Second bootloader address [1]
 * - Kernel tags address [1]
 * - Kernel size [2]
 * - Ramdisk size [2]
 * - Second bootloader size [2]
 * - Device tree size [2]
 * - SHA1 identifier [3]
 *
 * [1] - Can be set using a base and an offset
 *
 * ]2] - Cannot be manually changed. This is automatically updated when the
 *       corresponding image is set
 *
 * [3] - This is automatically computed when the images within the boot image
 *       are changed
 *
 *
 * If the boot image is patched with loki, the following parameters may be used:
 *
 * - Original kernel size
 * - Original ramdisk size
 * - Ramdisk address
 *
 * However, because some of these parameters were set to zero in early versions
 * of loki, they are sometimes ignored and BootImage will search the file for
 * the location of the kernel image and ramdisk image.
 */

BootImage::BootImage() : m_impl(new Impl())
{
}

BootImage::~BootImage()
{
}

/*!
 * \brief Get error information
 *
 * \note The returned ErrorCode contains valid information only if an
 *       operation has failed.
 *
 * \return ErrorCode containing information about the error
 */
ErrorCode BootImage::error() const
{
    return m_impl->error;
}

bool BootImage::isValid(const unsigned char *data, std::size_t size)
{
    return LokiFormat::isValid(data, size)
            || BumpFormat::isValid(data, size)
            || MtkFormat::isValid(data, size)
            || AndroidFormat::isValid(data, size)
            || SonyElfFormat::isValid(data, size);
}

bool BootImage::load(const unsigned char *data, std::size_t size)
{
    bool ret = false;

    if (LokiFormat::isValid(data, size)) {
        LOGD("Boot image is a loki'd Android boot image");
        m_impl->sourceType = Type::Loki;
        // We can't repatch with Loki until we have access to the aboot
        // partition
        m_impl->type = Type::Android;
        ret = LokiFormat(&m_impl->i10e).loadImage(data, size);
    } else if (BumpFormat::isValid(data, size)) {
        LOGD("Boot image is a bump'd Android boot image");
        m_impl->sourceType = Type::Bump;
        m_impl->type = Type::Bump;
        ret = BumpFormat(&m_impl->i10e).loadImage(data, size);
    } else if (MtkFormat::isValid(data, size)) {
        LOGD("Boot image is an mtk boot image");
        m_impl->sourceType = Type::Mtk;
        m_impl->type = Type::Mtk;
        ret = MtkFormat(&m_impl->i10e).loadImage(data, size);
    } else if (AndroidFormat::isValid(data, size)) {
        LOGD("Boot image is a plain boot image");
        m_impl->sourceType = Type::Android;
        m_impl->type = Type::Android;
        ret = AndroidFormat(&m_impl->i10e).loadImage(data, size);
    } else if (SonyElfFormat::isValid(data, size)) {
        LOGD("Boot image is a Sony ELF32 boot image");
        m_impl->sourceType = Type::SonyElf;
        m_impl->type = Type::SonyElf;
        ret = SonyElfFormat(&m_impl->i10e).loadImage(data, size);
    } else {
        LOGD("Unknown boot image type");
    }

    if (!ret) {
        m_impl->error = ErrorCode::BootImageParseError;
        return false;
    }

    return true;
}

/*!
 * \brief Load a boot image from binary data
 *
 * This function loads a boot image from a vector containing the binary data.
 * The boot image headers and other images (eg. kernel and ramdisk) will be
 * copied and stored.
 *
 * \warning If the boot image cannot be loaded, do not use the same BootImage
 *          object to load another boot image as it may contain partially
 *          loaded data.
 *
 * \return Whether the boot image was successfully read and parsed.
 */
bool BootImage::load(const BinData &data)
{
    return load(data.data(), data.size());
}

/*!
 * \brief Load a boot image file
 *
 * This function reads a boot image file and then calls
 * BootImage::load(const std::vector<unsigned char> &)
 *
 * \warning If the boot image cannot be loaded, do not use the same BootImage
 *          object to load another boot image as it may contain partially
 *          loaded data.
 *
 * \sa BootImage::load(const std::vector<unsigned char> &)
 *
 * \return Whether the boot image was successfully read and parsed.
 */
bool BootImage::loadFile(const std::string &filename)
{
    std::vector<unsigned char> data;
    auto ret = FileUtils::readToMemory(filename, &data);
    if (ret != ErrorCode::NoError) {
        m_impl->error = ret;
        return false;
    }

    BinData bd;
    bd.setData(data.data(), data.size(), false);
    return load(bd);
}

/*!
 * \brief Constructs the boot image binary data
 *
 * This function builds the bootable boot image binary data that the BootImage
 * represents. This is equivalent to AOSP's \a mkbootimg tool.
 *
 * \return Boot image binary data
 */
bool BootImage::create(BinData *data) const
{
    bool ret = false;

    switch (m_impl->type) {
    case Type::Android:
        LOGD("Creating Android boot image");
        ret = AndroidFormat(&m_impl->i10e).createImage(data);
        break;
    case Type::Bump:
        LOGD("Creating bump'd Android boot image");
        ret = BumpFormat(&m_impl->i10e).createImage(data);
        break;
    case Type::Loki:
        LOGD("Creating loki'd Android boot image");
        ret = LokiFormat(&m_impl->i10e).createImage(data);
        break;
    case Type::Mtk:
        LOGD("Creating mtk Android boot image");
        ret = MtkFormat(&m_impl->i10e).createImage(data);
        break;
    case Type::SonyElf:
        LOGD("Creating Sony ELF32 boot image");
        ret = SonyElfFormat(&m_impl->i10e).createImage(data);
        break;
    default:
        LOGE("Unknown boot image type");
        break;
    }

    return ret;
}

/*!
 * \brief Constructs boot image and writes it to a file
 *
 * This is a convenience function that calls BootImage::create() and writes the
 * data to the specified file.
 *
 * \return Whether the file was successfully written
 *
 * \sa BootImage::create()
 */
bool BootImage::createFile(const std::string &path)
{
    io::File file;
    if (!file.open(path, io::File::OpenWrite)) {
        FLOGE("%s: Failed to open for writing: %s",
              path.c_str(), file.errorString().c_str());

        m_impl->error = ErrorCode::FileOpenError;
        return false;
    }

    BinData data;
    if (!create(&data)) {
        return false;
    }

    uint64_t bytesWritten;
    if (!file.write(data.data(), data.size(), &bytesWritten)) {
        FLOGE("%s: Failed to write file: %s",
              path.c_str(), file.errorString().c_str());

        m_impl->error = ErrorCode::FileWriteError;
        return false;
    }

    return true;
}

/*!
 * \brief Get type of boot image
 *
 * This is set to the type of the source boot image if it has not been changed
 * by calling setFormat().
 *
 * \note The return value is undefined before load() or loadFile() has been
 *       called (and returned true).
 *
 * \return Boot image format
 */
BootImage::Type BootImage::wasType() const
{
    return m_impl->sourceType;
}

BootImage::Type BootImage::targetType() const
{
    return m_impl->type;
}

void BootImage::setTargetType(BootImage::Type type)
{
    m_impl->type = type;
}

uint64_t BootImage::typeSupportMask(BootImage::Type type)
{
    switch (type) {
    case Type::Android:
        return AndroidFormat::typeSupportMask();
    case Type::Bump:
        return BumpFormat::typeSupportMask();
    case Type::Loki:
        return LokiFormat::typeSupportMask();
    case Type::Mtk:
        return MtkFormat::typeSupportMask();
    case Type::SonyElf:
        return SonyElfFormat::typeSupportMask();
    default:
        return 0;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Board name
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Board name field in the boot image header
 *
 * \return Board name
 */
const std::string & BootImage::boardName() const
{
    return m_impl->i10e.boardName;
}

/*!
 * \brief Set the board name field in the boot image header
 *
 * \param name Board name
 */
void BootImage::setBoardName(std::string name)
{
    m_impl->i10e.boardName = std::move(name);
}

const char * BootImage::boardNameC() const
{
    return m_impl->i10e.boardName.c_str();
}

void BootImage::setBoardNameC(const char *name)
{
    m_impl->i10e.boardName = name;
}

////////////////////////////////////////////////////////////////////////////////
// Kernel cmdline
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Kernel cmdline in the boot image header
 *
 * \return Kernel cmdline
 */
const std::string & BootImage::kernelCmdline() const
{
    return m_impl->i10e.cmdline;
}

/*!
 * \brief Set the kernel cmdline in the boot image header
 *
 * \param cmdline Kernel cmdline
 */
void BootImage::setKernelCmdline(std::string cmdline)
{
    m_impl->i10e.cmdline = std::move(cmdline);
}

const char * BootImage::kernelCmdlineC() const
{
    return m_impl->i10e.cmdline.c_str();
}

void BootImage::setKernelCmdlineC(const char *cmdline)
{
    m_impl->i10e.cmdline = cmdline;
}

////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Page size field in the boot image header
 *
 * \return Page size
 */
uint32_t BootImage::pageSize() const
{
    return m_impl->i10e.pageSize;
}

/*!
 * \brief Set the page size field in the boot image header
 *
 * \note The page size should be one of if 2048, 4096, 8192, 16384, 32768,
 *       65536, or 131072
 *
 * \param size Page size
 */
void BootImage::setPageSize(uint32_t size)
{
    m_impl->i10e.pageSize = size;
}

/*!
 * \brief Kernel address field in the boot image header
 *
 * \return Kernel address
 */
uint32_t BootImage::kernelAddress() const
{
    return m_impl->i10e.kernelAddr;
}

/*!
 * \brief Set the kernel address field in the boot image header
 *
 * \param address Kernel address
 */
void BootImage::setKernelAddress(uint32_t address)
{
    m_impl->i10e.kernelAddr = address;
}

/*!
 * \brief Ramdisk address field in the boot image header
 *
 * \return Ramdisk address
 */
uint32_t BootImage::ramdiskAddress() const
{
    return m_impl->i10e.ramdiskAddr;
}

/*!
 * \brief Set the ramdisk address field in the boot image header
 *
 * \param address Ramdisk address
 */
void BootImage::setRamdiskAddress(uint32_t address)
{
    m_impl->i10e.ramdiskAddr = address;
}

/*!
 * \brief Second bootloader address field in the boot image header
 *
 * \return Second bootloader address
 */
uint32_t BootImage::secondBootloaderAddress() const
{
    return m_impl->i10e.secondAddr;
}

/*!
 * \brief Set the second bootloader address field in the boot image header
 *
 * \param address Second bootloader address
 */
void BootImage::setSecondBootloaderAddress(uint32_t address)
{
    m_impl->i10e.secondAddr = address;
}

/*!
 * \brief Kernel tags address field in the boot image header
 *
 * \return Kernel tags address
 */
uint32_t BootImage::kernelTagsAddress() const
{
    return m_impl->i10e.tagsAddr;
}

/*!
 * \brief Set the kernel tags address field in the boot image header
 *
 * \param address Kernel tags address
 */
void BootImage::setKernelTagsAddress(uint32_t address)
{
    m_impl->i10e.tagsAddr = address;
}

uint32_t BootImage::iplAddress() const
{
    return m_impl->i10e.iplAddr;
}

void BootImage::setIplAddress(uint32_t address)
{
    m_impl->i10e.iplAddr = address;
}

uint32_t BootImage::rpmAddress() const
{
    return m_impl->i10e.rpmAddr;
}

void BootImage::setRpmAddress(uint32_t address)
{
    m_impl->i10e.rpmAddr = address;
}

uint32_t BootImage::appsblAddress() const
{
    return m_impl->i10e.appsblAddr;
}

void BootImage::setAppsblAddress(uint32_t address)
{
    m_impl->i10e.appsblAddr = address;
}

uint32_t BootImage::entrypointAddress() const
{
    return m_impl->i10e.hdrEntrypoint;
}

void BootImage::setEntrypointAddress(uint32_t address)
{
    m_impl->i10e.hdrEntrypoint = address;
}

////////////////////////////////////////////////////////////////////////////////
// Kernel image
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Kernel image
 *
 * \return Vector containing the kernel image binary data
 */
const BinData & BootImage::kernelImage() const
{
    return m_impl->i10e.kernelImage;
}

/*!
 * \brief Set the kernel image
 *
 * This will automatically update the kernel size in the boot image header and
 * recalculate the SHA1 hash.
 */
void BootImage::setKernelImage(BinData data)
{
    m_impl->i10e.hdrKernelSize = data.size();
    m_impl->i10e.kernelImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Ramdisk image
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Ramdisk image
 *
 * \return Vector containing the ramdisk image binary data
 */
const BinData & BootImage::ramdiskImage() const
{
    return m_impl->i10e.ramdiskImage;
}

/*!
 * \brief Set the ramdisk image
 *
 * This will automatically update the ramdisk size in the boot image header and
 * recalculate the SHA1 hash.
 */
void BootImage::setRamdiskImage(BinData data)
{
    m_impl->i10e.hdrRamdiskSize = data.size();
    m_impl->i10e.ramdiskImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Second bootloader image
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Second bootloader image
 *
 * \return Vector containing the second bootloader image binary data
 */
const BinData & BootImage::secondBootloaderImage() const
{
    return m_impl->i10e.secondImage;
}

/*!
 * \brief Set the second bootloader image
 *
 * This will automatically update the second bootloader size in the boot image
 * header and recalculate the SHA1 hash.
 */
void BootImage::setSecondBootloaderImage(BinData data)
{
    m_impl->i10e.hdrSecondSize = data.size();
    m_impl->i10e.secondImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Device tree image
////////////////////////////////////////////////////////////////////////////////

/*!
 * \brief Device tree image
 *
 * \return Vector containing the device tree image binary data
 */
const BinData & BootImage::deviceTreeImage() const
{
    return m_impl->i10e.dtImage;
}

/*!
 * \brief Set the device tree image
 *
 * This will automatically update the device tree size in the boot image
 * header and recalculate the SHA1 hash.
 */
void BootImage::setDeviceTreeImage(BinData data)
{
    m_impl->i10e.hdrDtSize = data.size();
    m_impl->i10e.dtImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Aboot image
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::abootImage() const
{
    return m_impl->i10e.abootImage;
}

void BootImage::setAbootImage(BinData data)
{
    m_impl->i10e.abootImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Kernel MTK header
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::kernelMtkHeader() const
{
    return m_impl->i10e.mtkKernelHdr;
}

void BootImage::setKernelMtkHeader(BinData data)
{
    m_impl->i10e.mtkKernelHdr = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Ramdisk MTK header
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::ramdiskMtkHeader() const
{
    return m_impl->i10e.mtkRamdiskHdr;
}

void BootImage::setRamdiskMtkHeader(BinData data)
{
    m_impl->i10e.mtkRamdiskHdr = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Sony ipl image
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::iplImage() const
{
    return m_impl->i10e.iplImage;
}

void BootImage::setIplImage(BinData data)
{
    m_impl->i10e.iplImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Sony rpm image
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::rpmImage() const
{
    return m_impl->i10e.rpmImage;
}

void BootImage::setRpmImage(BinData data)
{
    m_impl->i10e.rpmImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Sony appsbl image
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::appsblImage() const
{
    return m_impl->i10e.appsblImage;
}

void BootImage::setAppsblImage(BinData data)
{
    m_impl->i10e.appsblImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Sony SIN! image
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::sinImage() const
{
    return m_impl->i10e.sonySinImage;
}

void BootImage::setSinImage(BinData data)
{
    m_impl->i10e.sonySinImage = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////
// Sony SIN! header
////////////////////////////////////////////////////////////////////////////////

const BinData & BootImage::sinHeader() const
{
    return m_impl->i10e.sonySinHdr;
}

void BootImage::setSinHeader(BinData data)
{
    m_impl->i10e.sonySinHdr = std::move(data);
}

////////////////////////////////////////////////////////////////////////////////

bool BootImage::operator==(const BootImage &other) const
{
    // Check that the images, addresses, and metadata are equal. This doesn't
    // care if eg. one boot image is loki'd and the other is not as long as the
    // contents are the same.
    return
            // Images
            m_impl->i10e.kernelImage == other.m_impl->i10e.kernelImage
            && m_impl->i10e.ramdiskImage == other.m_impl->i10e.ramdiskImage
            && m_impl->i10e.secondImage == other.m_impl->i10e.secondImage
            && m_impl->i10e.dtImage == other.m_impl->i10e.dtImage
            && m_impl->i10e.abootImage == other.m_impl->i10e.abootImage
            // MTK headers
            && m_impl->i10e.mtkKernelHdr == other.m_impl->i10e.mtkKernelHdr
            && m_impl->i10e.mtkRamdiskHdr == other.m_impl->i10e.mtkRamdiskHdr
            // Sony images
            && m_impl->i10e.iplImage == other.m_impl->i10e.iplImage
            && m_impl->i10e.rpmImage == other.m_impl->i10e.rpmImage
            && m_impl->i10e.appsblImage == other.m_impl->i10e.appsblImage
            && m_impl->i10e.sonySinImage == other.m_impl->i10e.sonySinImage
            && m_impl->i10e.sonySinHdr == other.m_impl->i10e.sonySinHdr
            // Header's integral values
            && m_impl->i10e.hdrKernelSize == other.m_impl->i10e.hdrKernelSize
            && m_impl->i10e.kernelAddr == other.m_impl->i10e.kernelAddr
            && m_impl->i10e.hdrRamdiskSize == other.m_impl->i10e.hdrRamdiskSize
            && m_impl->i10e.ramdiskAddr == other.m_impl->i10e.ramdiskAddr
            && m_impl->i10e.hdrSecondSize == other.m_impl->i10e.hdrSecondSize
            && m_impl->i10e.secondAddr == other.m_impl->i10e.secondAddr
            && m_impl->i10e.tagsAddr == other.m_impl->i10e.tagsAddr
            && m_impl->i10e.pageSize == other.m_impl->i10e.pageSize
            && m_impl->i10e.hdrDtSize == other.m_impl->i10e.hdrDtSize
            //&& m_impl->i10e.hdrUnused == other.m_impl->i10e.hdrUnused
            // ID
            && m_impl->i10e.hdrId[0] == other.m_impl->i10e.hdrId[0]
            && m_impl->i10e.hdrId[1] == other.m_impl->i10e.hdrId[1]
            && m_impl->i10e.hdrId[2] == other.m_impl->i10e.hdrId[2]
            && m_impl->i10e.hdrId[3] == other.m_impl->i10e.hdrId[3]
            && m_impl->i10e.hdrId[4] == other.m_impl->i10e.hdrId[4]
            && m_impl->i10e.hdrId[5] == other.m_impl->i10e.hdrId[5]
            && m_impl->i10e.hdrId[6] == other.m_impl->i10e.hdrId[6]
            && m_impl->i10e.hdrId[7] == other.m_impl->i10e.hdrId[7]
            // Header's string values
            && boardName() == other.boardName()
            && kernelCmdline() == other.kernelCmdline();
}

bool BootImage::operator!=(const BootImage &other) const
{
    return !(*this == other);
}

}
