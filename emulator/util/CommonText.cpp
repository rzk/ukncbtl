/* This file is part of UKNCBTL, distributed under the GNU LGPL v3 or later. */
#include "stdafx.h"
#include "UkncCharset.h"

// Print octal 16-bit value to buffer
// buffer size at least 7 characters
void PrintOctalValue(TCHAR* buffer, WORD value)
{
    for (int p = 0; p < 6; p++)
    {
        int digit = value & 7;
        buffer[5 - p] = _T('0') + (TCHAR)digit;
        value = (value >> 3);
    }
    buffer[6] = 0;
}
// Print hex 16-bit value to buffer
// buffer size at least 5 characters
void PrintHexValue(TCHAR* buffer, WORD value)
{
    for (int p = 0; p < 4; p++)
    {
        int digit = value & 15;
        buffer[3 - p] = (digit < 10) ? _T('0') + (TCHAR)digit : _T('a') + (TCHAR)(digit - 10);
        value = (value >> 4);
    }
    buffer[4] = 0;
}
// Print binary 16-bit value to buffer
// buffer size at least 17 characters
void PrintBinaryValue(TCHAR* buffer, WORD value)
{
    for (int b = 0; b < 16; b++)
    {
        int bit = (value >> b) & 1;
        buffer[15 - b] = bit ? _T('1') : _T('0');
    }
    buffer[16] = 0;
}

#ifndef _UNICODE
// UKNC KOI8-R (Russian) to Windows-1251 conversion table
const TCHAR KOI8R_CODES[] =
{
    _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'),
    _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T(' '), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'),
    _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'),
    _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'), _T('·'),
    _T('ю'), _T('а'), _T('б'), _T('ц'), _T('д'), _T('е'), _T('ф'), _T('г'), _T('х'), _T('и'), _T('й'), _T('к'), _T('л'), _T('м'), _T('н'), _T('о'),
    _T('п'), _T('я'), _T('р'), _T('с'), _T('т'), _T('у'), _T('ж'), _T('в'), _T('ь'), _T('ы'), _T('з'), _T('ш'), _T('э'), _T('щ'), _T('ч'), _T('ъ'),
    _T('Ю'), _T('А'), _T('Б'), _T('Ц'), _T('Д'), _T('Е'), _T('Ф'), _T('Г'), _T('Х'), _T('И'), _T('Й'), _T('К'), _T('Л'), _T('М'), _T('Н'), _T('О'),
    _T('П'), _T('Я'), _T('Р'), _T('С'), _T('Т'), _T('У'), _T('Ж'), _T('В'), _T('Ь'), _T('Ы'), _T('З'), _T('Ш'), _T('Э'), _T('Щ'), _T('Ч'), _T('Ъ'),
};
#endif
// Translate one KOI8-R character to Unicode character
TCHAR Translate_KOI8R(uint8_t ch)
{
#ifdef _UNICODE
    return static_cast<TCHAR>(uknc_charset::ToUnicode(ch));
#else
    if (ch < 128) return (TCHAR) ch;
    return KOI8R_CODES[ch - 128];
#endif
}

// Returns a pointer into the input; only backslash is a separator.
LPCTSTR GetFileNameFromFilePath(LPCTSTR lpfilepath)
{
    LPCTSTR lpfilename = _tcsrchr(lpfilepath, _T('\\'));
    if (lpfilename == nullptr)
        return lpfilepath;
    else
        return lpfilename + 1;
}
