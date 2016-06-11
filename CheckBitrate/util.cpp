// -----------------------------------------------------------------------------------------
// CheckBitrate by rigaya
// -----------------------------------------------------------------------------------------
// The MIT License
//
// Copyright (c) 2016 rigaya
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// --------------------------------------------------------------------------------------------

#include <cstdio>
#include <vector>
#include <numeric>
#include <memory>
#include <sstream>
#include <algorithm>
#include <type_traits>
#include "util.h"

#pragma warning (push)
#pragma warning (disable: 4100)
#if defined(_WIN32) || defined(_WIN64)
unsigned int wstring_to_string(const wchar_t *wstr, std::string& str, uint32_t codepage) {
    if (wstr == nullptr) {
        str = "";
        return 0;
    }
    uint32_t flags = (codepage == CP_UTF8) ? 0 : WC_NO_BEST_FIT_CHARS;
    int multibyte_length = WideCharToMultiByte(codepage, flags, wstr, -1, nullptr, 0, nullptr, nullptr);
    str.resize(multibyte_length, 0);
    if (0 == WideCharToMultiByte(codepage, flags, wstr, -1, &str[0], multibyte_length, nullptr, nullptr)) {
        str.clear();
        return 0;
    }
    return multibyte_length;
}
#else
unsigned int wstring_to_string(const wchar_t *wstr, std::string& str, uint32_t codepage) {
    if (wstr == nullptr) {
        str = "";
        return 0;
    }
    auto ic = iconv_open("UTF-8", "wchar_t"); //to, from
    auto input_len = wcslen(wstr) * 2;
    auto output_len = input_len * 6;
    std::vector<char> buf(output_len, 0);
    char *outbuf = buf.data();
    iconv(ic, (char **)&wstr, &input_len, &outbuf, &output_len);
    iconv_close(ic);
    str = buf.data();
    return output_len;
}
#endif //#if defined(_WIN32) || defined(_WIN64)

std::string wstring_to_string(const wchar_t *wstr, uint32_t codepage) {
    if (wstr == nullptr) {
        return "";
    }
    std::string str;
    wstring_to_string(wstr, str, codepage);
    return str;
}

std::string wstring_to_string(const std::wstring& wstr, uint32_t codepage) {
    std::string str;
    wstring_to_string(wstr.c_str(), str, codepage);
    return str;
}

unsigned int tchar_to_string(const TCHAR *tstr, std::string& str, uint32_t codepage) {
#if UNICODE
    return wstring_to_string(tstr, str, codepage);
#else
    str = (tstr) ? std::string(tstr) : "";
    return (unsigned int)str.length();
#endif
}

std::string tchar_to_string(const TCHAR *tstr, uint32_t codepage) {
    if (tstr == nullptr) {
        return "";
    }
    std::string str;
    tchar_to_string(tstr, str, codepage);
    return str;
}

std::wstring tchar_to_wstring(const tstring& tstr, uint32_t codepage) {
#if UNICODE
    return std::wstring(tstr);
#else
    return char_to_wstring(tstr, codepage);
#endif
}

std::wstring tchar_to_wstring(const TCHAR *tstr, uint32_t codepage) {
    if (tstr == nullptr) {
        return L"";
    }
    return tchar_to_wstring(tstring(tstr), codepage);
}

std::string tchar_to_string(const tstring& tstr, uint32_t codepage) {
    std::string str;
    tchar_to_string(tstr.c_str(), str, codepage);
    return str;
}

unsigned int wstring_to_tstring(const WCHAR *wstr, tstring& tstr, uint32_t codepage) {
    if (wstr == nullptr) {
        tstr = _T("");
        return 0;
    }
#if UNICODE
    tstr = std::wstring(wstr);
#else
    return wstring_to_string(wstr, tstr, codepage);
#endif
    return (unsigned int)tstr.length();
}

tstring wstring_to_tstring(const WCHAR *wstr, uint32_t codepage) {
    if (wstr == nullptr) {
        return _T("");
    }
    tstring tstr;
    wstring_to_tstring(wstr, tstr, codepage);
    return tstr;
}

tstring wstring_to_tstring(const std::wstring& wstr, uint32_t codepage) {
    tstring tstr;
    wstring_to_tstring(wstr.c_str(), tstr, codepage);
    return tstr;
}

#if defined(_WIN32) || defined(_WIN64)
unsigned int char_to_wstring(std::wstring& wstr, const char *str, uint32_t codepage) {
    if (str == nullptr) {
        wstr = L"";
        return 0;
    }
    int widechar_length = MultiByteToWideChar(codepage, 0, str, -1, nullptr, 0);
    wstr.resize(widechar_length, 0);
    if (0 == MultiByteToWideChar(codepage, 0, str, -1, &wstr[0], (int)wstr.size())) {
        wstr.clear();
        return 0;
    }
    return widechar_length;
}
#else
unsigned int char_to_wstring(std::wstring& wstr, const char *str, uint32_t codepage) {
    if (str == nullptr) {
        wstr = L"";
        return 0;
    }
    auto ic = iconv_open("wchar_t", "UTF-8"); //to, from
    if ((int64_t)ic == -1) {
        fprintf(stderr, "iconv_error\n");
    }
    auto input_len = strlen(str);
    std::vector<char> buf(input_len + 1);
    strcpy(buf.data(), str);
    auto output_len = (input_len + 1) * 8;
    std::vector<char> bufout(output_len, 0);
    char *inbuf = buf.data();
    char *outbuf = bufout.data();
    iconv(ic, &inbuf, &input_len, &outbuf, &output_len);
    iconv_close(ic);
    wstr = std::wstring((WCHAR *)bufout.data());
    return wstr.length();
}
#endif //#if defined(_WIN32) || defined(_WIN64)
std::wstring char_to_wstring(const char *str, uint32_t codepage) {
    if (str == nullptr) {
        return L"";
    }
    std::wstring wstr;
    char_to_wstring(wstr, str, codepage);
    return wstr;
}
std::wstring char_to_wstring(const std::string& str, uint32_t codepage) {
    std::wstring wstr;
    char_to_wstring(wstr, str.c_str(), codepage);
    return wstr;
}

unsigned int char_to_tstring(tstring& tstr, const char *str, uint32_t codepage) {
#if UNICODE
    return char_to_wstring(tstr, str, codepage);
#else
    tstr = std::string(str);
    return (unsigned int)tstr.length();
#endif
}

tstring char_to_tstring(const char *str, uint32_t codepage) {
    if (str == nullptr) {
        return _T("");
    }
    tstring tstr;
    char_to_tstring(tstr, str, codepage);
    return tstr;
}
tstring char_to_tstring(const std::string& str, uint32_t codepage) {
    tstring tstr;
    char_to_tstring(tstr, str.c_str(), codepage);
    return tstr;
}
std::string strsprintf(const char* format, ...) {
    if (format == nullptr) {
        return "";
    }
    va_list args;
    va_start(args, format);
    const size_t len = _vscprintf(format, args) + 1;

    std::vector<char> buffer(len, 0);
    vsprintf_s(buffer.data(), len, format, args);
    va_end(args);
    std::string retStr = std::string(buffer.data());
    return retStr;
}
#if defined(_WIN32) || defined(_WIN64)
std::wstring strsprintf(const WCHAR* format, ...) {
    if (format == nullptr) {
        return L"";
    }
    va_list args;
    va_start(args, format);
    const size_t len = _vscwprintf(format, args) + 1;

    std::vector<WCHAR> buffer(len, 0);
    vswprintf_s(buffer.data(), len, format, args);
    va_end(args);
    std::wstring retStr = std::wstring(buffer.data());
    return retStr;
}
#endif //#if defined(_WIN32) || defined(_WIN64)

std::string str_replace(std::string str, const std::string& from, const std::string& to) {
    std::string::size_type pos = 0;
    while (pos = str.find(from, pos), pos != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return std::move(str);
}

#if defined(_WIN32) || defined(_WIN64)
std::wstring str_replace(std::wstring str, const std::wstring& from, const std::wstring& to) {
    std::wstring::size_type pos = 0;
    while (pos = str.find(from, pos), pos != std::wstring::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return std::move(str);
}
#endif //#if defined(_WIN32) || defined(_WIN64)

#pragma warning (pop)

#if defined(_WIN32) || defined(_WIN64)
std::vector<std::wstring> split(const std::wstring &str, const std::wstring &delim, bool bTrim) {
    std::vector<std::wstring> res;
    size_t current = 0, found, delimlen = delim.size();
    while (std::wstring::npos != (found = str.find(delim, current))) {
        auto segment = std::wstring(str, current, found - current);
        if (bTrim) {
            segment = trim(segment);
        }
        if (!bTrim || segment.length()) {
            res.push_back(segment);
        }
        current = found + delimlen;
    }
    auto segment = std::wstring(str, current, str.size() - current);
    if (bTrim) {
        segment = trim(segment);
    }
    if (!bTrim || segment.length()) {
        res.push_back(std::wstring(segment.c_str()));
    }
    return res;
}
#endif //#if defined(_WIN32) || defined(_WIN64)

std::vector<std::string> split(const std::string &str, const std::string &delim, bool bTrim) {
    std::vector<std::string> res;
    size_t current = 0, found, delimlen = delim.size();
    while (std::string::npos != (found = str.find(delim, current))) {
        auto segment = std::string(str, current, found - current);
        if (bTrim) {
            segment = trim(segment);
        }
        if (!bTrim || segment.length()) {
            res.push_back(segment);
        }
        current = found + delimlen;
    }
    auto segment = std::string(str, current, str.size() - current);
    if (bTrim) {
        segment = trim(segment);
    }
    if (!bTrim || segment.length()) {
        res.push_back(std::string(segment.c_str()));
    }
    return res;
}

std::string lstrip(const std::string& string, const char* trim) {
    auto result = string;
    auto left = string.find_first_not_of(trim);
    if (left != std::string::npos) {
        result = string.substr(left, 0);
    }
    return result;
}

std::string rstrip(const std::string& string, const char* trim) {
    auto result = string;
    auto right = string.find_last_not_of(trim);
    if (right != std::string::npos) {
        result = string.substr(0, right);
    }
    return result;
}

std::string trim(const std::string& string, const char* trim) {
    auto result = string;
    auto left = string.find_first_not_of(trim);
    if (left != std::string::npos) {
        auto right = string.find_last_not_of(trim);
        result = string.substr(left, right - left + 1);
    }
    return result;
}

std::wstring lstrip(const std::wstring& string, const WCHAR* trim) {
    auto result = string;
    auto left = string.find_first_not_of(trim);
    if (left != std::string::npos) {
        result = string.substr(left, 0);
    }
    return result;
}

std::wstring rstrip(const std::wstring& string, const WCHAR* trim) {
    auto result = string;
    auto right = string.find_last_not_of(trim);
    if (right != std::string::npos) {
        result = string.substr(0, right);
    }
    return result;
}

std::wstring trim(const std::wstring& string, const WCHAR* trim) {
    auto result = string;
    auto left = string.find_first_not_of(trim);
    if (left != std::string::npos) {
        auto right = string.find_last_not_of(trim);
        result = string.substr(left, right - left + 1);
    }
    return result;
}

bool qsv_get_filesize(const char *filepath, uint64_t *filesize) {
#if defined(_WIN32) || defined(_WIN64)
    WIN32_FILE_ATTRIBUTE_DATA fd = { 0 };
    bool ret = (GetFileAttributesExA(filepath, GetFileExInfoStandard, &fd)) ? true : false;
    *filesize = (ret) ? (((UINT64)fd.nFileSizeHigh) << 32) + (UINT64)fd.nFileSizeLow : NULL;
    return ret;
#else //#if defined(_WIN32) || defined(_WIN64)
    struct stat stat;
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL || fstat(fileno(fp), &stat)) {
        *filesize = 0;
        return 1;
    }
    if (fp) {
        fclose(fp);
    }
    *filesize = stat.st_size;
    return 0;
#endif //#if defined(_WIN32) || defined(_WIN64)
}

#if defined(_WIN32) || defined(_WIN64)
bool qsv_get_filesize(const WCHAR *filepath, UINT64 *filesize) {
    WIN32_FILE_ATTRIBUTE_DATA fd = { 0 };
    bool ret = (GetFileAttributesExW(filepath, GetFileExInfoStandard, &fd)) ? true : false;
    *filesize = (ret) ? (((UINT64)fd.nFileSizeHigh) << 32) + (UINT64)fd.nFileSizeLow : NULL;
    return ret;
}
#endif //#if defined(_WIN32) || defined(_WIN64)