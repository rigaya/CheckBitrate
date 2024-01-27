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

#ifndef __CHECK_BITRATE_VERSION_H__
#define __CHECK_BITRATE_VERSION_H__

#define VER_FILEVERSION             0,0,5,0
#define VER_STR_FILEVERSION          "0.05"
#define VER_STR_FILEVERSION_TCHAR _T("0.05")

#ifdef DEBUG
#define VER_DEBUG   VS_FF_DEBUG
#define VER_PRIVATE VS_FF_PRIVATEBUILD
#else
#define VER_DEBUG   0
#define VER_PRIVATE 0
#endif

#ifdef _M_IX86
#define CHECK_BITRATE_FILENAME "CheckBitrate (x86)"
#else
#define CHECK_BITRATE_FILENAME "CheckBitrate (x64)"
#endif

#define VER_STR_COMMENTS         "CheckBitrate"
#define VER_STR_COMPANYNAME      ""
#define VER_STR_FILEDESCRIPTION  CHECK_BITRATE_FILENAME
#define VER_STR_INTERNALNAME     CHECK_BITRATE_FILENAME
#define VER_STR_ORIGINALFILENAME "CheckBitrate.exe"
#define VER_STR_LEGALCOPYRIGHT   "CheckBitrate by rigaya"
#define VER_STR_PRODUCTNAME      CHECK_BITRATE_FILENAME
#define VER_PRODUCTVERSION       VER_FILEVERSION
#define VER_STR_PRODUCTVERSION   VER_STR_FILEVERSION

#endif //__CHECK_BITRATE_VERSION_H__
