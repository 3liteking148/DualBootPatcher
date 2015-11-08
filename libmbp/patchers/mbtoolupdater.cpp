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

#include "patchers/mbtoolupdater.h"

#include <cassert>

#include "bootimage.h"
#include "cpiofile.h"
#include "patcherconfig.h"
#include "ramdiskpatchers/core.h"

#include "private/stringutils.h"


namespace mbp
{

/*! \cond INTERNAL */
class MbtoolUpdater::Impl
{
public:
    Impl(MbtoolUpdater *parent) : m_parent(parent) {}

    PatcherConfig *pc;
    const FileInfo *info;

    ErrorCode error;

    bool patchImage();
    void patchInitRc(CpioFile *cpio);

private:
    MbtoolUpdater *m_parent;
};
/*! \endcond */


const std::string MbtoolUpdater::Id("MbtoolUpdater");


MbtoolUpdater::MbtoolUpdater(PatcherConfig * const pc)
    : m_impl(new Impl(this))
{
    m_impl->pc = pc;
}

MbtoolUpdater::~MbtoolUpdater()
{
}

ErrorCode MbtoolUpdater::error() const
{
    return m_impl->error;
}

std::string MbtoolUpdater::id() const
{
    return Id;
}

void MbtoolUpdater::setFileInfo(const FileInfo * const info)
{
    m_impl->info = info;
}

std::string MbtoolUpdater::newFilePath()
{
    assert(m_impl->info != nullptr);

    // Insert "_patched" before ".img"/".lok"
    std::string path(m_impl->info->filename());
    path.insert(path.size() - 4, "_patched");

    return path;
}

void MbtoolUpdater::cancelPatching()
{
    // Ignore. This runs fast enough that canceling is not needed
}

bool MbtoolUpdater::patchFile(ProgressUpdatedCallback progressCb,
                              FilesUpdatedCallback filesCb,
                              DetailsUpdatedCallback detailsCb,
                              void *userData)
{
    (void) progressCb;
    (void) filesCb;
    (void) detailsCb;
    (void) userData;

    assert(m_impl->info != nullptr);

    bool isImg = StringUtils::iends_with(m_impl->info->filename(), ".img");
    bool isLok = StringUtils::iends_with(m_impl->info->filename(), ".lok");

    if (!isImg && !isLok) {
        m_impl->error = ErrorCode::OnlyBootImageSupported;
        return false;
    }

    return m_impl->patchImage();
}

bool MbtoolUpdater::Impl::patchImage()
{
    BootImage bi;
    if (!bi.loadFile(info->filename())) {
        error = bi.error();
        return false;
    }

    CpioFile mainCpio;
    CpioFile cpioInCpio;
    CpioFile *target;

    // Load the ramdisk cpio
    const BinData &bd = bi.ramdiskImage();
    if (!mainCpio.load(bd.data(), bd.size())) {
        error = mainCpio.error();
        return false;
    }

    const unsigned char *data;
    std::size_t size;
    if (mainCpio.contentsC("sbin/ramdisk.cpio", &data, &size)) {
        // Mess with the Android cpio archive for ramdisks on Sony devices with
        // combined boot/recovery partitions
        if (!cpioInCpio.load(data, size)) {
            error = cpioInCpio.error();
            return false;
        }

        target = &cpioInCpio;
    } else {
        target = &mainCpio;
    }

    // Make sure init.rc has the mbtooldaemon service
    patchInitRc(target);

    if (target == &cpioInCpio) {
        // Store new internal cpio archive
        std::vector<unsigned char> newContents;
        if (!cpioInCpio.createData(&newContents)) {
            error = cpioInCpio.error();
            return false;
        }
        mainCpio.setContents("sbin/ramdisk.cpio", std::move(newContents));
    }

    std::vector<unsigned char> newRamdisk;
    if (!mainCpio.createData(&newRamdisk)) {
        error = mainCpio.error();
        return false;
    }
    BinData newBd;
    newBd.setDataCopy(newRamdisk.data(), newRamdisk.size());
    bi.setRamdiskImage(std::move(newBd));

    if (!bi.createFile(m_parent->newFilePath())) {
        error = bi.error();
        return false;
    }

    return true;
}

static bool allWhiteSpace(const char *str)
{
    for (; *str && isspace(*str); ++str);
    return *str == '\0';
}

void MbtoolUpdater::Impl::patchInitRc(CpioFile *cpio)
{
    std::vector<unsigned char> contents;
    cpio->contents("init.rc", &contents);

    std::vector<std::string> lines = StringUtils::splitData(contents, '\n');

    bool insideService = false;

    // Remove old mbtooldaemon service definition
    for (auto it = lines.begin(); it != lines.end();) {
        if (StringUtils::starts_with(*it, "service")) {
            insideService = it->find("mbtooldaemon") != std::string::npos;
        } else if (insideService && allWhiteSpace(it->c_str())) {
            insideService = false;
        }

        if (insideService) {
            it = lines.erase(it);
        } else {
            ++it;
        }
    }

    contents = StringUtils::joinData(lines, '\n');
    cpio->setContents("init.rc", std::move(contents));

    CoreRP crp(pc, info, cpio);
    crp.patchRamdisk();
}

}
