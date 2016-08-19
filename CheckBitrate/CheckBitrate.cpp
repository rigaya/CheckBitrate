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
#include <algorithm>
#include <memory>
#include <map>
#include <chrono>
#include <functional>
#include <string>
#include <tchar.h>
#include "CheckBitrateVersion.h"
#include "util.h" 
#include "avcodec_reader.h"

#ifdef UNICODE
#define to_tstring to_wstring
#else
#define to_tstring to_string
#endif

using std::map;

struct StreamHandler {
    AVCodecContext *pCodecCtx;

    bool bUseHEVCmp42AnnexB;
    vector<uint8_t> hevcMp42AnnexbBuffer;
    AVBitStreamFilterContext *pH264Bsfc;
    uint8_t *pExtradata;
    int nExtradataSize;
    AVCodecParserContext *pParserCtx;

    FramePosList framePosList;
};

static inline bool av_isvalid_q(AVRational q) {
    return q.den * q.num != 0;
}

vector<int> getStreamIndex(AVFormatContext *pFormatCtx, AVMediaType type, const vector<int> *pVidStreamIndex = nullptr) {
    vector<int> streams;
    const int n_streams = pFormatCtx->nb_streams;
    for (int i = 0; i < n_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == type) {
            streams.push_back(i);
        }
    }
    if (type == AVMEDIA_TYPE_VIDEO) {
        std::sort(streams.begin(), streams.end(), [pFormatCtx = pFormatCtx](int streamIdA, int streamIdB) {
            auto pStreamA = pFormatCtx->streams[streamIdA];
            auto pStreamB = pFormatCtx->streams[streamIdB];
            if (pStreamA->codec == nullptr) {
                return false;
            }
            if (pStreamB->codec == nullptr) {
                return true;
            }
            const int resA = pStreamA->codec->width * pStreamA->codec->height;
            const int resB = pStreamB->codec->width * pStreamB->codec->height;
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
            if (pFormatCtx->streams[streamIdA]->codec == nullptr) {
                return false;
            }
            if (pFormatCtx->streams[streamIdB]->codec == nullptr) {
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
    return std::move(streams);
}

int selectStream(AVFormatContext *pFormatCtx, vector<int>& videoStreams, int nVideoTrack, int nStreamId) {
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

void hevcMp42Annexb(AVPacket *pkt, StreamHandler& streamHandler) {
    static const uint8_t SC[] = { 0, 0, 0, 1 };
    const uint8_t *ptr, *ptr_fin;
    vector<uint8_t>& hevcMp42AnnexbBuffer = streamHandler.hevcMp42AnnexbBuffer;
    if (pkt == NULL) {
        hevcMp42AnnexbBuffer.reserve(streamHandler.nExtradataSize + 128);
        ptr = streamHandler.pExtradata;
        ptr_fin = ptr + streamHandler.nExtradataSize;
        ptr += 0x16;
    } else {
        hevcMp42AnnexbBuffer.reserve(pkt->size + 128);
        ptr = pkt->data;
        ptr_fin = ptr + pkt->size;
    }
    const int numOfArrays = *ptr;
    ptr += !!numOfArrays;

    while (ptr + 6 < ptr_fin) {
        ptr += !!numOfArrays;
        const int count = readUB16(ptr); ptr += 2;
        int units = (numOfArrays) ? count : 1;
        for (int i = (std::max)(1, units); i; i--) {
            uint32_t size = readUB16(ptr); ptr += 2;
            uint32_t uppper = count << 16;
            size += (numOfArrays) ? 0 : uppper;
            hevcMp42AnnexbBuffer.insert(hevcMp42AnnexbBuffer.end(), SC, SC+4);
            hevcMp42AnnexbBuffer.insert(hevcMp42AnnexbBuffer.end(), ptr, ptr+size); ptr += size;
        }
    }
    if (pkt) {
        if (pkt->buf->size < (int)hevcMp42AnnexbBuffer.size()) {
            av_grow_packet(pkt, (int)hevcMp42AnnexbBuffer.size());
        }
        memcpy(pkt->data, hevcMp42AnnexbBuffer.data(), hevcMp42AnnexbBuffer.size());
        pkt->size = (int)hevcMp42AnnexbBuffer.size();
    } else {
        if (streamHandler.pExtradata) {
            av_free(streamHandler.pExtradata);
        }
        streamHandler.pExtradata = (uint8_t *)av_malloc(hevcMp42AnnexbBuffer.size());
        streamHandler.nExtradataSize = (int)hevcMp42AnnexbBuffer.size();
        memcpy(streamHandler.pExtradata, hevcMp42AnnexbBuffer.data(), hevcMp42AnnexbBuffer.size());
    }
    hevcMp42AnnexbBuffer.clear();
}


int GetHeader(StreamHandler& streamHandler) {
    if (streamHandler.pExtradata == nullptr) {
        streamHandler.nExtradataSize = streamHandler.pCodecCtx->extradata_size;
        //ここでav_mallocを使用しないと正常に動作しない
        streamHandler.pExtradata = (uint8_t *)av_malloc(streamHandler.pCodecCtx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        //ヘッダのデータをコピーしておく
        memcpy(streamHandler.pExtradata, streamHandler.pCodecCtx->extradata, streamHandler.nExtradataSize);
        memset(streamHandler.pExtradata + streamHandler.nExtradataSize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

        if (streamHandler.bUseHEVCmp42AnnexB) {
            hevcMp42Annexb(nullptr, streamHandler);
        } else if (streamHandler.pH264Bsfc && streamHandler.pExtradata[0] == 1) {
            uint8_t *dummy = nullptr;
            int dummy_size = 0;
            std::swap(streamHandler.pExtradata,     streamHandler.pCodecCtx->extradata);
            std::swap(streamHandler.nExtradataSize, streamHandler.pCodecCtx->extradata_size);
            av_bitstream_filter_filter(streamHandler.pH264Bsfc, streamHandler.pCodecCtx, nullptr, &dummy, &dummy_size, nullptr, 0, 0);
            std::swap(streamHandler.pExtradata,     streamHandler.pCodecCtx->extradata);
            std::swap(streamHandler.nExtradataSize, streamHandler.pCodecCtx->extradata_size);
            av_bitstream_filter_close(streamHandler.pH264Bsfc);
            if (NULL == (streamHandler.pH264Bsfc = av_bitstream_filter_init("h264_mp4toannexb"))) {
                _ftprintf(stderr, _T("failed to init h264_mp4toannexb.\n"));
                return 1;
            }
        }
    }
    return 0;
}

int check(AVFormatContext *pFormatCtx, map<int, StreamHandler>& streamHandlers, uint64_t filesize) {
    AVPacket pkt;
    av_init_packet(&pkt);
    auto tmupdate = std::chrono::system_clock::now();
    double lastprogress = 0.0;
    while (av_read_frame(pFormatCtx, &pkt) >= 0) {
        auto tmnow = std::chrono::system_clock::now();
        if (tmnow - tmupdate > std::chrono::milliseconds(500)) {
            double progress = pkt.pos * 100.0 / (double)filesize;
            if (progress > lastprogress) {
                tmupdate = tmnow;
                _ftprintf(stderr, _T("reading input file %.2f%%  \r"), pkt.pos * 100.0 / (double)filesize);
                lastprogress = progress;
            }
        }
        auto pCodecCtx = pFormatCtx->streams[pkt.stream_index]->codec;
        if (pCodecCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
            auto pH264Bsfc = streamHandlers[pkt.stream_index].pH264Bsfc;
            if (pH264Bsfc) {
                uint8_t *data = nullptr;
                int dataSize = 0;
                std::swap(streamHandlers[pkt.stream_index].pExtradata, pCodecCtx->extradata);
                std::swap(streamHandlers[pkt.stream_index].nExtradataSize, pCodecCtx->extradata_size);
                av_bitstream_filter_filter(pH264Bsfc, pCodecCtx, nullptr,
                    &data, &dataSize, pkt.data, pkt.size, 0);
                std::swap(streamHandlers[pkt.stream_index].pExtradata, pCodecCtx->extradata);
                std::swap(streamHandlers[pkt.stream_index].nExtradataSize, pCodecCtx->extradata_size);
                AVPacket pktProp;
                av_init_packet(&pktProp);
                av_packet_copy_props(&pktProp, &pkt);
                av_packet_unref(&pkt); //メモリ解放を忘れない
                av_packet_copy_props(&pkt, &pktProp);
                av_packet_from_data(&pkt, data, dataSize);
            }
            if (streamHandlers[pkt.stream_index].bUseHEVCmp42AnnexB) {
                hevcMp42Annexb(&pkt, streamHandlers[pkt.stream_index]);
            }
            FramePos pos = { 0 };
            pos.pts = pkt.pts;
            pos.dts = pkt.dts;
            pos.duration = (int)pkt.duration;
            pos.duration2 = 0;
            pos.poc = AVQSV_POC_INVALID;
            pos.flags = (uint8_t)pkt.flags;
            pos.size = pkt.size;
            auto pParserCtx = streamHandlers[pkt.stream_index].pParserCtx;
            if (pParserCtx) {
                auto bUseHEVCmp42AnnexB = streamHandlers[pkt.stream_index].bUseHEVCmp42AnnexB;
                if (pH264Bsfc || bUseHEVCmp42AnnexB) {
                    std::swap(streamHandlers[pkt.stream_index].pExtradata, pCodecCtx->extradata);
                    std::swap(streamHandlers[pkt.stream_index].nExtradataSize, pCodecCtx->extradata_size);
                }
                uint8_t *dummy = nullptr;
                int dummy_size = 0;
                av_parser_parse2(pParserCtx, pCodecCtx, &dummy, &dummy_size, pkt.data, pkt.size, pkt.pts, pkt.dts, pkt.pos);
                if (pH264Bsfc || bUseHEVCmp42AnnexB) {
                    std::swap(streamHandlers[pkt.stream_index].pExtradata, pCodecCtx->extradata);
                    std::swap(streamHandlers[pkt.stream_index].nExtradataSize, pCodecCtx->extradata_size);
                }
                pos.pict_type = (uint8_t)std::max(pParserCtx->pict_type, 0);
                switch (pParserCtx->picture_structure) {
                    //フィールドとして符号化されている
                case AV_PICTURE_STRUCTURE_TOP_FIELD:    pos.pic_struct = AVQSV_PICSTRUCT_FIELD_TOP; break;
                case AV_PICTURE_STRUCTURE_BOTTOM_FIELD: pos.pic_struct = AVQSV_PICSTRUCT_FIELD_BOTTOM; break;
                    //フレームとして符号化されている
                default:
                    switch (pParserCtx->field_order) {
                    case AV_FIELD_TT:
                    case AV_FIELD_TB: pos.pic_struct = AVQSV_PICSTRUCT_FRAME_TFF; break;
                    case AV_FIELD_BT:
                    case AV_FIELD_BB: pos.pic_struct = AVQSV_PICSTRUCT_FRAME_BFF; break;
                    default:          pos.pic_struct = AVQSV_PICSTRUCT_FRAME;     break;
                    }
                }
                pos.repeat_pict = (uint8_t)pParserCtx->repeat_pict;
            }
            streamHandlers[pkt.stream_index].framePosList.add(pos);
        }
        av_packet_unref(&pkt);
    }
    return 0;
}

static inline double ts2sec(int64_t ts, AVRational timebase) {
    return ts * av_q2d(timebase);
}

static int writeBitrate(const tstring& filename, StreamHandler& streamHandler, double interval) {
    double tick = 0.0;
    uint64_t sizetick = 0;
    uint64_t sizesum = 0;
    FILE *fp = NULL;
    if (_tfopen_s(&fp, filename.c_str(), _T("w"))) {
        _ftprintf(stderr, _T("failed to open output file \"%s\"\n"), filename.c_str());
        return 1;
    }
    _ftprintf(fp, _T(",kbps,kbps(avg)\n"));
    int64_t firstpts = AV_NOPTS_VALUE;
    double framesec = 0.0;
    uint32_t index = (uint32_t)-1;
    for (int poc = 0; ; poc++) {
        auto framepos = streamHandler.framePosList.copy(poc, &index);
        if (framepos.poc == AVQSV_POC_INVALID && framepos.pts == 0) {
            break;
        }
        if (framepos.pts != AV_NOPTS_VALUE && framepos.poc != AVQSV_POC_INVALID) {
            if (firstpts == AV_NOPTS_VALUE) {
                firstpts = framepos.pts;
            }
            framesec = ts2sec(framepos.pts - firstpts, streamHandler.pCodecCtx->pkt_timebase);
            if (tick + interval < framesec) {
                double time = framesec - tick;
                double kbps = sizetick * 8 / time * 0.001;
                double avgkbps = sizesum * 8 / framesec * 0.001;
                _ftprintf(fp, _T("%10.3f,%.2f,%.2f\n"), tick, kbps, avgkbps);
                tick = framesec;
                sizetick = 0;
            }
        }
        sizetick += framepos.size;
        sizesum += framepos.size;
    }
    double time = framesec - tick;
    double kbps = sizetick * 8 / time * 0.001;
    double avgkbps = sizesum * 8 / framesec * 0.001;
    _ftprintf(fp, _T("%10.3f,%.2f,%.2f\n"), tick, kbps, avgkbps);
    return 0;
}

int run(const tstring& filename, double interval = 0.0, int nVideoTrack = 0, int nStreamId = 0) {
    av_register_all();
    avcodec_register_all();
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

    map<int, StreamHandler> streamHandlers;
    for (auto index : videoStreams) {
        StreamHandler stream;
        streamHandlers[index].pCodecCtx = pFormatCtx->streams[index]->codec;
        streamHandlers[index].pH264Bsfc = nullptr;
        streamHandlers[index].pParserCtx = nullptr;
    }

    //必要ならbitstream filterを初期化
    for (auto ist = streamHandlers.begin(); ist != streamHandlers.end(); ist++) {
        auto pCodecCtx = pFormatCtx->streams[ist->first]->codec;
        if (pCodecCtx->extradata && pCodecCtx->extradata[0] == 1) {
            if (pCodecCtx->codec_id == AV_CODEC_ID_H264) {
                if (NULL == (ist->second.pH264Bsfc = av_bitstream_filter_init("h264_mp4toannexb"))) {
                    _ftprintf(stderr, _T("failed to init h264_mp4toannexb.\n"));
                    return 1;
                }
            } else if (pCodecCtx->codec_id == AV_CODEC_ID_HEVC) {
                ist->second.bUseHEVCmp42AnnexB = true;
            }
        }
        ist->second.pParserCtx = av_parser_init(pCodecCtx->codec_id);
        ist->second.pParserCtx->flags |= PARSER_FLAG_COMPLETE_FRAMES;

        GetHeader(ist->second);
    }

    uint64_t filesize = 0;
    qsv_get_filesize(filename.c_str(), &filesize);

    check(pFormatCtx, streamHandlers, filesize);

    double duration_sec = ts2sec(pFormatCtx->duration, av_make_q(1, AV_TIME_BASE));
    if (interval <= 0.0) {
        interval = clamp(duration_sec / 100, 0.5, 4.0);
    }
    _ftprintf(stderr, _T("analyzing video bitrate (interval: %.2f sec)...\n"), interval);


    for (auto ist = streamHandlers.begin(); ist != streamHandlers.end(); ist++) {
        _ftprintf(stderr, _T("output bitrate of video track #%d...\n"), ist->first + 1);
        double dEstFrameDurationByFpsDecoder = 0.0;
        AVRational fpsDecoder = ist->second.pCodecCtx->framerate;
        if (av_isvalid_q(fpsDecoder) && av_isvalid_q(ist->second.pCodecCtx->pkt_timebase)) {
            dEstFrameDurationByFpsDecoder = av_q2d(av_inv_q(fpsDecoder)) * av_q2d(av_inv_q(ist->second.pCodecCtx->pkt_timebase));
        }
        ist->second.framePosList.checkPtsStatus(dEstFrameDurationByFpsDecoder);

        //動画の終端を表す最後のptsを挿入する
        int64_t videoFinPts = 0;
        const int nFrameNum = ist->second.framePosList.frameNum();
        if (ist->second.framePosList.getStreamPtsStatus() & AVQSV_PTS_ALL_INVALID) {
            videoFinPts = nFrameNum * ist->second.framePosList.list(0).duration;
        } else if (nFrameNum) {
            const FramePos *lastFrame = &ist->second.framePosList.list(nFrameNum - 1);
            videoFinPts = lastFrame->pts + lastFrame->duration;
        }
        //最後のフレーム情報をセットし、m_Demux.framesの内部状態を終了状態に移行する
        ist->second.framePosList.fin(framePos(videoFinPts, videoFinPts, 0), pFormatCtx->duration);


        //tstring outfile = filename + _T(".track") + std::to_tstring(ist->first + 1) + _T(".framepos.csv");
        //ist->second.framePosList.printList(outfile.c_str());

        writeBitrate(filename + _T(".track") + std::to_tstring(ist->first + 1) + _T(".bitrate.csv"), ist->second, interval);
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
    _ftprintf(stdout, str.c_str());
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
    for (auto filename : filelist) {
        run(filename, interval);
    }
    return 0;
}
