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
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <string>
#include "CheckBitrateVersion.h"
#include "rgy_util.h"
#include "rgy_filesystem.h"
#pragma warning (push)
#pragma warning (disable: 4244)
#pragma warning (disable: 4819)
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#pragma comment (lib, "avcodec.lib")
#pragma comment (lib, "avformat.lib")
#pragma comment (lib, "avutil.lib")
#pragma warning (pop)

#ifdef UNICODE
#define to_tstring to_wstring
#else
#define to_tstring to_string
#endif

static const TCHAR *AVCODEC_DLL_NAME[] = {
    _T("avcodec-60.dll"), _T("avformat-60.dll"), _T("avutil-58.dll")
};

template<typename T>
struct RGYAVDeleter {
    RGYAVDeleter() : deleter(nullptr) {};
    RGYAVDeleter(std::function<void(T**)> deleter) : deleter(deleter) {};
    void operator()(T *p) { deleter(&p); }
    std::function<void(T**)> deleter;
};

struct FrameData {
    int64_t pts;
    int64_t dts;
    int size;
    uint32_t flags;

    FrameData() : pts(0), dts(0), size(0), flags(0) {};
    FrameData(int64_t pts_, int64_t dts_, int size_, uint32_t flags_) : pts(pts_), dts(dts_), size(size_), flags(flags_) {};
};

struct StreamHandler {
    int streamId;
    AVRational streamTimebase;
    std::vector<FrameData> frameDataList;

    StreamHandler(int stream_id, AVRational stream_timebase) : streamId(stream_id), streamTimebase(stream_timebase), frameDataList() {};
};

std::vector<int> getStreamIndex(AVFormatContext *pFormatCtx, AVMediaType type, const std::vector<int> *pVidStreamIndex = nullptr) {
    std::vector<int> streams;
    const int n_streams = pFormatCtx->nb_streams;
    for (int i = 0; i < n_streams; i++) {
        if (pFormatCtx->streams[i]->codecpar->codec_type == type) {
            streams.push_back(i);
        }
    }
    if (type == AVMEDIA_TYPE_VIDEO) {
        std::sort(streams.begin(), streams.end(), [pFormatCtx = pFormatCtx](int streamIdA, int streamIdB) {
            auto pStreamA = pFormatCtx->streams[streamIdA];
            auto pStreamB = pFormatCtx->streams[streamIdB];
            if (pStreamA->codecpar == nullptr) {
                return false;
            }
            if (pStreamB->codecpar == nullptr) {
                return true;
            }
            const int resA = pStreamA->codecpar->width * pStreamA->codecpar->height;
            const int resB = pStreamB->codecpar->width * pStreamB->codecpar->height;
            return (resA > resB);
        });
    } else if (pVidStreamIndex && pVidStreamIndex->size()) {
        auto mostNearestVidStreamId =[pFormatCtx = pFormatCtx, pVidStreamIndex](int streamId) {
            auto ret = std::make_pair(0, UINT32_MAX);
            for (uint32_t i = 0; i < pVidStreamIndex->size(); i++) {
                uint32_t diff = (uint32_t)(streamId - pFormatCtx->streams[(*pVidStreamIndex)[i]]->id);
                if (diff < ret.second) {
                    ret.second = diff;
                    ret.first = i;
                }
            }
            return ret;
        };
        std::sort(streams.begin(), streams.end(), [pFormatCtx = pFormatCtx, pVidStreamIndex, mostNearestVidStreamId](int streamIdA, int streamIdB) {
            if (pFormatCtx->streams[streamIdA]->codecpar == nullptr) {
                return false;
            }
            if (pFormatCtx->streams[streamIdB]->codecpar == nullptr) {
                return true;
            }
            auto pStreamIdA = pFormatCtx->streams[streamIdA]->id;
            auto pStreamIdB = pFormatCtx->streams[streamIdB]->id;
            auto nearestVidA = mostNearestVidStreamId(pStreamIdA);
            auto nearestVidB = mostNearestVidStreamId(pStreamIdB);
            if (nearestVidA.first == nearestVidB.first) {
                return nearestVidA.second < nearestVidB.second;
            }
            return nearestVidA.first < nearestVidB.first;
        });
    }
    return streams;
}

int selectStream(AVFormatContext *pFormatCtx, std::vector<int>& videoStreams, int nVideoTrack, int nStreamId) {
    int nIndex = videoStreams[0];
    if (nVideoTrack) {
        if (videoStreams.size() < (uint32_t)std::abs(nVideoTrack)) {
            _ftprintf(stderr, _T("track %d was selected for video, but input only contains %d video tracks.\n"), nVideoTrack, (int)videoStreams.size());
            return 1;
        } else if (nVideoTrack < 0) {
            //逆順に並べ替え
            std::reverse(videoStreams.begin(), videoStreams.end());
        }
        nIndex = videoStreams[std::abs(nVideoTrack)-1];
    } else if (nStreamId) {
        auto streamIndexFound = std::find_if(videoStreams.begin(), videoStreams.end(), [pFormatCtx = pFormatCtx, nSearchId = nStreamId](int nStreamIndex) {
            return (pFormatCtx->streams[nStreamIndex]->id == nSearchId);
        });
        if (streamIndexFound == videoStreams.end()) {
            _ftprintf(stderr, _T("stream id %d (0x%x) not found in video tracks.\n"), nStreamId, nStreamId);
            return 1;
        }
        nIndex = *streamIndexFound;
    }
    return nIndex;
}

int check(AVFormatContext *pFormatCtx, std::vector<std::unique_ptr<StreamHandler>>& streamHandlers, const uint64_t filesize) {
    std::unique_ptr<AVPacket, RGYAVDeleter<AVPacket>> pkt(av_packet_alloc(), RGYAVDeleter<AVPacket>(av_packet_free));
    auto tmupdate = std::chrono::system_clock::now();
    double lastprogress = 0.0;
    int vidpkts = 0;
    while (av_read_frame(pFormatCtx, pkt.get()) >= 0) {
        if (pkt->flags & AV_PKT_FLAG_CORRUPT) {
            av_packet_unref(pkt.get());
            continue;
        }
        const auto codecpar = pFormatCtx->streams[pkt->stream_index]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vidpkts++;
            if ((vidpkts % 1000) == 0) {
                auto tmnow = std::chrono::system_clock::now();
                if (tmnow - tmupdate > std::chrono::milliseconds(500)) {
                    double progress = pkt->pos * 100.0 / (double)filesize;
                    if (progress > lastprogress) {
                        tmupdate = tmnow;
                        _ftprintf(stderr, _T("reading input file %.2f%%  \r"), pkt->pos * 100.0 / (double)filesize);
                        lastprogress = progress;
                    }
                }
            }
            streamHandlers[pkt->stream_index]->frameDataList.emplace_back(FrameData(pkt->pts, pkt->dts, pkt->size, pkt->flags));
        }
        av_packet_unref(pkt.get());
    }
    return 0;
}

static inline double ts2sec(int64_t ts, AVRational timebase) {
    return ts * av_q2d(timebase);
}

static int64_t get_dts(const FrameData& frame) {
    return frame.dts != AV_NOPTS_VALUE ? frame.dts : frame.pts;
}

// 基本的にdtsベースで処理する
static int writeBitrate(const tstring& filename, StreamHandler *streamHandler, const double interval, const AVRational avgFrameRate) {
    // 有効なtimestampを探す
    int64_t firstTimestampIdx = -1;
    auto& frames = streamHandler->frameDataList;
    for (int64_t i = 0; i < (int64_t)frames.size(); i++) {
        if (get_dts(frames[i]) != AV_NOPTS_VALUE) {
            firstTimestampIdx = i;
            break;
        }
    }
    if (firstTimestampIdx >= 0) { // 有効なtimestampがある場合
        // PCR Wrapを考慮 (AV_NOPTS_VALUEでない値を対象にする)
        // 単調増加に補正する
        const int64_t PCR_WRAP_CHECK_VAL = (1LL << 32) - 1;
        const int64_t PCR_WRAP_VAL = (1LL << 33);
        int64_t ptsOffset = 0;
        int64_t prevts = get_dts(frames[firstTimestampIdx]);
        for (int64_t i = firstTimestampIdx; i < (int64_t)frames.size(); i++) {
            auto timestamp = get_dts(frames[i]);
            if (timestamp != AV_NOPTS_VALUE) {
                if (timestamp + ptsOffset < prevts) {
                    if ((prevts - (timestamp + ptsOffset)) >= PCR_WRAP_CHECK_VAL) {
                        ptsOffset += PCR_WRAP_VAL;
                    } else if (frames[i].flags & AV_PKT_FLAG_CORRUPT) {
                        timestamp = AV_NOPTS_VALUE;
                    }
                }
                frames[i].dts = prevts = ((timestamp == AV_NOPTS_VALUE) ? AV_NOPTS_VALUE : timestamp + ptsOffset);
            }
        }
        // 途中にAV_NOPTS_VALUEがある場合も多い
        // その場合は、前後のtimestampから大雑把に線形補間する
        prevts = -1;
        int64_t prevtsidx = firstTimestampIdx;
        for (int64_t i = firstTimestampIdx; i < (int64_t)frames.size(); i++) {
            auto timestamp = get_dts(frames[i]);
            if (timestamp != AV_NOPTS_VALUE) {
                // 途中までのフレームについてはtimestampを大雑把に線形補間する
                for (int64_t j = prevtsidx + 1; j < i; j++) {
                    frames[j].dts = prevts + av_rescale(timestamp - prevts, j - prevtsidx, i - prevtsidx);
                }
                frames[i].dts = timestamp;
                prevts = timestamp;
                prevtsidx = i;
            }
        }
        // その後の区間にAV_NOPTS_VALUEがあれば、最後の30フレームのtimestampを使って線形外挿する
        const int64_t iterp_interval = std::min<int64_t>(30, prevtsidx);
        const int64_t ts_iterp_interval = frames[prevtsidx - iterp_interval].dts;
        for (int64_t i = prevtsidx+1; i < (int64_t)frames.size(); i++) {
            frames[i].dts = prevts + av_rescale(prevts - ts_iterp_interval, i - prevtsidx, iterp_interval);
        }
        //for (int64_t i = 0; i < (int64_t)frames.size(); i++) {
        //    fprintf(stderr, "%12lld, %d\n", frames[i].dts, frames[i].size);
        //}
    } else {
        // avgFrameRate を仮定して、timestampを計算する
        for (size_t i = 0; i < frames.size(); i++) {
            frames[i].dts = (int64_t)av_rescale_q(i, streamHandler->streamTimebase, avgFrameRate);
        }
        firstTimestampIdx = 0;
    }
    const auto firstts = frames[firstTimestampIdx].dts;

    // 出力
    double tick = 0.0;
    uint64_t sizetick = 0;
    uint64_t sizesum = 0;
    FILE *fp = NULL;
    if (_tfopen_s(&fp, filename.c_str(), _T("w"))) {
        _ftprintf(stderr, _T("failed to open output file \"%s\"\n"), filename.c_str());
        return 1;
    }
    _ftprintf(fp, _T(",kbps,kbps(avg)\n"));
    double framesec = 0.0;
    for (int64_t i = firstTimestampIdx; i < (int64_t)frames.size(); i++) {
        const auto& frame = frames[i];
        const auto timestamp = frames[i].dts;
        framesec = ts2sec(timestamp - firstts, streamHandler->streamTimebase);
        if (tick + interval < framesec) {
            double time = framesec - tick;
            double kbps = sizetick * 8 / time * 0.001;
            double avgkbps = sizesum * 8 / framesec * 0.001;
            _ftprintf(fp, _T("%10.3f,%.2f,%.2f\n"), tick, kbps, avgkbps);
            tick = framesec;
            sizetick = 0;
        }
        sizetick += frame.size;
        sizesum += frame.size;
    }
    double time = framesec - tick;
    double kbps = sizetick * 8 / time * 0.001;
    double avgkbps = sizesum * 8 / framesec * 0.001;
    _ftprintf(fp, _T("%10.3f,%.2f,%.2f\n"), tick, kbps, avgkbps);
    return 0;
}

int run(const tstring& filename, double interval = 0.0) {
    av_log_set_level(AV_LOG_ERROR);

    //UTF-8に変換
    std::string filename_char;
    if (0 == tchar_to_string(filename.c_str(), filename_char, CP_UTF8)) {
        _ftprintf(stderr, _T("failed to convert filename to utf-8 characters.\n"));
        return 1;
    }

    auto pFormatCtx = avformat_alloc_context();

    //ts向けの設定
    AVDictionary *pFormatOption = nullptr;
    //av_dict_set(&pFormatOption, "scan_all_pmts", "1", 0);

    //ファイルのオープン
    if (avformat_open_input(&pFormatCtx, filename_char.c_str(), nullptr, nullptr)) {
        _ftprintf(stderr, _T("error opening file: \"%s\"\n"), char_to_tstring(filename_char, CP_UTF8).c_str());
        return 1;
    }

    if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
        _ftprintf(stderr, _T("error finding stream information.\n"));
        return 1; // Couldn't find stream information
    }
    av_dump_format(pFormatCtx, 0, filename_char.c_str(), 0);

    auto videoStreams = getStreamIndex(pFormatCtx, AVMEDIA_TYPE_VIDEO);
    if (videoStreams.size() == 0) {
        _ftprintf(stderr, _T("no video stream found.\n"));
        return 1; // Couldn't find stream information
    }
    //auto nVideoIndex = selectStream(pFormatCtx, videoStreams, nVideoTrack, nStreamId);

    std::vector<std::unique_ptr<StreamHandler>> streamHandlers(pFormatCtx->nb_streams);
    for (auto index : videoStreams) {
        streamHandlers[index] = std::make_unique<StreamHandler>(index, pFormatCtx->streams[index]->time_base);
    }

    uint64_t filesize = 0;
    rgy_get_filesize(filename.c_str(), &filesize);

    check(pFormatCtx, streamHandlers, filesize);

    double duration_sec = ts2sec(pFormatCtx->duration, av_make_q(1, AV_TIME_BASE));
    if (interval <= 0.0) {
        interval = clamp(duration_sec / 100, 0.5, 4.0);
    }
    _ftprintf(stderr, _T("analyzing video bitrate (interval: %.2f sec)...\n"), interval);


    for (auto& st : streamHandlers) {
        if (!st) continue;
        _ftprintf(stderr, _T("output bitrate of video track #%d...\n"), st->streamId + 1);
        writeBitrate(filename + _T(".track") + std::to_tstring(st->streamId + 1) + _T(".bitrate.csv"), st.get(), interval, pFormatCtx->streams[st->streamId]->avg_frame_rate);
    }

    if (pFormatOption) {
        av_dict_free(&pFormatOption);
    }
    if (pFormatCtx) {
        avformat_close_input(&pFormatCtx);
        avformat_free_context(pFormatCtx);
    }
    return 0;
}

//必要なavcodecのdllがそろっているかを確認

//avcodecのdllが存在しない場合のエラーメッセージ
tstring error_mes_avcodec_dll_not_found() {
    tstring mes;
    mes += _T("avcodec: failed to load dlls.\n");
    mes += _T("please make sure ");
    for (int i = 0; i < _countof(AVCODEC_DLL_NAME); i++) {
        if (i) mes += _T(", ");
        if (i % 3 == 2) {
            mes += _T("\n");
        }
        mes += _T("\"") + tstring(AVCODEC_DLL_NAME[i]) + _T("\"");
    }
    mes += _T("\nis installed in your system.\n");
    return mes;
}

bool check_avcodec_dll() {
#if defined(_WIN32) || defined(_WIN64)
    // static変数として、一度存在を確認したら再度チェックはしないように
    static bool check = false;
    if (check) return check;
    std::vector<HMODULE> hDllList;
    check = true;
    for (int i = 0; i < _countof(AVCODEC_DLL_NAME); i++) {
        HMODULE hDll = NULL;
        if (NULL == (hDll = LoadLibrary(AVCODEC_DLL_NAME[i]))) {
            check = false;
            break;
        }
        hDllList.push_back(hDll);
    }
    for (auto hDll : hDllList) {
        FreeLibrary(hDll);
    }
    return check;
#else
    return true;
#endif //#if defined(_WIN32) || defined(_WIN64)
}

tstring getAVVersions() {
    if (!check_avcodec_dll()) {
        return error_mes_avcodec_dll_not_found();
    }
    auto ver2str = [](uint32_t ver) {
        return strsprintf("%3d.%3d.%4d", (ver >> 16) & 0xff, (ver >> 8) & 0xff, ver & 0xff);
        };
    std::string mes;
    mes = std::string("ffmpeg     version: ") + std::string(av_version_info()) + "\n";
    mes += std::string("avutil     version: ") + ver2str(avutil_version()) + "\n";
    mes += std::string("avcodec    version: ") + ver2str(avcodec_version()) + "\n";
    mes += std::string("avformat   version: ") + ver2str(avformat_version()) + "\n";
    return char_to_tstring(mes);
}

void option_error(const TCHAR *option, const TCHAR *argvalue) {
    if (argvalue == nullptr) {
        _ftprintf(stderr, _T("--%s requires value.\n"), option);
    } else {
        _ftprintf(stderr, _T("invalid value for --%s: \"%s\".\n"), option, argvalue);
    }
}

void print_help() {
    tstring str = tstring(_T("CheckBitrate ")) + VER_STR_FILEVERSION_TCHAR + _T(" by rigaya\n");
    str += _T("Usage: <exe> [options] <target filepath1> [<target filepath2>] ...\n");
    str += _T("\n");
    str += _T("Options:\n");
    str += _T("-i,--interval <float>   bitrate calc interval in seconds.\n");
    _ftprintf(stdout, _T("%s"), str.c_str());
}

int _tmain(int argc, TCHAR **argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }
    vector<tstring> filelist;
    double interval = 0.0;
    for (int i = 1; i < argc; i++) {
        const TCHAR *option_name = nullptr;
        if (argv[i][0] == _T('-')) {
            switch (argv[i][1]) {
            case 'i':
                option_name = _T("interval");
                break;
            case '-':
                option_name = &argv[i][2];
                break;
            default:
                break;
            }
        }
        if (option_name) {
            if (0 == _tcscmp(option_name, _T("interval"))) {
                if (i + 1 >= argc) {
                    option_error(option_name, nullptr);
                    break;
                }
                i++;
                if (1 != _stscanf_s(argv[i], _T("%lf"), &interval)) {
                    option_error(option_name, argv[i]);
                    break;
                }
            } else if (0 == _tcscmp(option_name, _T("help"))) {
                print_help();
                return 0;
            }
        } else {
            filelist.push_back(argv[i]);
        }
    }
    if (!check_avcodec_dll()) {
        _ftprintf(stdout, _T("%s"), error_mes_avcodec_dll_not_found().c_str());
        return 1;
    }
    for (auto filename : filelist) {
        run(filename, interval);
    }
    return 0;
}
