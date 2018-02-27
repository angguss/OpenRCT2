#pragma region Copyright (c) 2014-2017 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#ifdef __vita__

#include "platform.h"
#include "../config/Config.h"
#include "../localisation/Language.h"
#include "../util/Util.h"
#include <wchar.h>
#include <SDL.h>

#ifndef NO_TTF
bool platform_get_font_path(TTFFontDescriptor *font, utf8 *buffer, size_t size)
{
    STUB();
    return false;
}
#endif

uint16 platform_get_locale_language() {
    return LANGUAGE_ENGLISH_UK;
}

uint8 platform_get_locale_currency() {
    return platform_get_currency_value(NULL);
}

uint8 platform_get_locale_measurement_format() {
    return MEASUREMENT_FORMAT_METRIC;
}

bool platform_get_steam_path(utf8 * outPath, size_t outSize)
{
    return false;
}

#endif
