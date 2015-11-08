/*
 * Copyright (C) 2015  Andrew Gunnerson <andrewgunnerson@gmail.com>
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

#pragma once

#include "bootimage/fileformat.h"

namespace mbp
{

class SonyElfFormat : public BootImageFormat
{
public:
    SonyElfFormat(BootImageIntermediate *i10e);
    virtual ~SonyElfFormat();

    static uint64_t typeSupportMask();

    static bool isValid(const unsigned char *data, std::size_t size);

    virtual bool loadImage(const unsigned char *data, std::size_t size) override;

    virtual bool createImage(BinData *dataOut) override;
};

}
