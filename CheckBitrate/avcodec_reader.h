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

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <deque>
#include <cassert>
#include <algorithm>

#pragma warning (push)
#pragma warning (disable: 4244)
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

#include "qsv_queue.h"

using std::vector;
using std::pair;
using std::deque;

static const uint32_t AVCODEC_READER_INPUT_BUF_SIZE = 16 * 1024 * 1024;
static const uint32_t AVQSV_FRAME_MAX_REORDER = 16;
static const int AVQSV_POC_INVALID = -1;

enum {
    AVQSV_AUDIO_NONE         = 0x00,
    AVQSV_AUDIO_MUX          = 0x01,
    AVQSV_AUDIO_COPY_TO_FILE = 0x02,
};

enum AVQSVPtsStatus : uint32_t {
    AVQSV_PTS_UNKNOWN           = 0x00,
    AVQSV_PTS_NORMAL            = 0x01,
    AVQSV_PTS_SOMETIMES_INVALID = 0x02, //時折、無効なptsを得る
    AVQSV_PTS_HALF_INVALID      = 0x04, //PAFFなため、半分のフレームのptsやdtsが無効
    AVQSV_PTS_ALL_INVALID       = 0x08, //すべてのフレームのptsやdtsが無効
    AVQSV_PTS_NONKEY_INVALID    = 0x10, //キーフレーム以外のフレームのptsやdtsが無効
    AVQSV_PTS_DUPLICATE         = 0x20, //重複するpts/dtsが存在する
    AVQSV_DTS_SOMETIMES_INVALID = 0x40, //時折、無効なdtsを得る
};

static AVQSVPtsStatus operator|(AVQSVPtsStatus a, AVQSVPtsStatus b) {
    return (AVQSVPtsStatus)((uint32_t)a | (uint32_t)b);
}

static AVQSVPtsStatus operator|=(AVQSVPtsStatus& a, AVQSVPtsStatus b) {
    a = a | b;
    return a;
}

static AVQSVPtsStatus operator&(AVQSVPtsStatus a, AVQSVPtsStatus b) {
    return (AVQSVPtsStatus)((uint32_t)a & (uint32_t)b);
}

static AVQSVPtsStatus operator&=(AVQSVPtsStatus& a, AVQSVPtsStatus b) {
    a = (AVQSVPtsStatus)((uint32_t)a & (uint32_t)b);
    return a;
}

enum AVQSVPicstruct : uint8_t {
    AVQSV_PICSTRUCT_UNKNOWN      = 0x00,
    AVQSV_PICSTRUCT_FRAME        = 0x01,                         //フレームとして符号化されている
    AVQSV_PICSTRUCT_FRAME_TFF    = 0x02 | AVQSV_PICSTRUCT_FRAME, //フレームとして符号化されているインタレ (TFF)
    AVQSV_PICSTRUCT_FRAME_BFF    = 0x04 | AVQSV_PICSTRUCT_FRAME, //フレームとして符号化されているインタレ (BFF)
    AVQSV_PICSTRUCT_FIELD        = 0x08,                         //フィールドとして符号化されている
    AVQSV_PICSTRUCT_FIELD_TOP    = AVQSV_PICSTRUCT_FIELD,        //フィールドとして符号化されている (Topフィールド)
    AVQSV_PICSTRUCT_FIELD_BOTTOM = 0x10 | AVQSV_PICSTRUCT_FIELD, //フィールドとして符号化されている (Bottomフィールド)
    AVQSV_PICSTRUCT_INTERLACED   = ((uint8_t)AVQSV_PICSTRUCT_FRAME_TFF | (uint8_t)AVQSV_PICSTRUCT_FRAME_BFF | (uint8_t)AVQSV_PICSTRUCT_FIELD_TOP | (uint8_t)AVQSV_PICSTRUCT_FIELD_BOTTOM) & ~AVQSV_PICSTRUCT_FRAME, //インタレ
};

static AVQSVPicstruct operator|(AVQSVPicstruct a, AVQSVPicstruct b) {
    return (AVQSVPicstruct)((uint8_t)a | (uint8_t)b);
}

static AVQSVPicstruct operator|=(AVQSVPicstruct& a, AVQSVPicstruct b) {
    a = a | b;
    return a;
}

static AVQSVPicstruct operator&(AVQSVPicstruct a, AVQSVPicstruct b) {
    return (AVQSVPicstruct)((uint8_t)a & (uint8_t)b);
}

static AVQSVPicstruct operator&=(AVQSVPicstruct& a, AVQSVPicstruct b) {
    a = (AVQSVPicstruct)((uint8_t)a & (uint8_t)b);
    return a;
}

//フレームの位置情報と長さを格納する
typedef struct FramePos {
    int64_t pts;  //pts
    int64_t dts;  //dts
    int duration;  //該当フレーム/フィールドの表示時間
    int duration2; //ペアフィールドの表示時間
    int poc; //出力時のフレーム番号
    uint8_t flags;    //flags (キーフレームならAV_PKT_FLAG_KEY)
    AVQSVPicstruct pic_struct; //AVQSV_PICSTRUCT_xxx
    uint8_t repeat_pict; //通常は1, RFFなら2+
    uint8_t pict_type; //I,P,Bフレーム
    uint32_t size; //フレームサイズ
} FramePos;

static FramePos framePos(int64_t pts, int64_t dts,
    int duration, int duration2 = 0,
    int poc = AVQSV_POC_INVALID,
    uint8_t flags = 0, AVQSVPicstruct pic_struct = AVQSV_PICSTRUCT_FRAME, uint8_t repeat_pict = 0, uint8_t pict_type = 0, uint32_t size = 0) {
    FramePos pos;
    pos.pts = pts;
    pos.dts = dts;
    pos.duration = duration;
    pos.duration2 = duration2;
    pos.poc = poc;
    pos.flags = flags;
    pos.pic_struct = pic_struct;
    pos.repeat_pict = repeat_pict;
    pos.pict_type = pict_type;
    pos.size = size;
    return pos;
}

class CompareFramePos {
public:
    uint32_t threshold;
    CompareFramePos() : threshold(0xFFFFFFFF) {
    }
    bool operator() (const FramePos& posA, const FramePos& posB) const {
        return ((uint32_t)std::abs(posA.pts - posB.pts) < threshold) ? posA.pts < posB.pts : posB.pts < posA.pts;
    }
};

class FramePosList {
public:
    FramePosList() :
        m_dFrameDuration(0.0),
        m_list(),
        m_nNextFixNumIndex(0),
        m_bInputFin(false),
        m_nDuration(0),
        m_nDurationNum(0),
        m_nStreamPtsStatus(AVQSV_PTS_UNKNOWN),
        m_nLastPoc(0),
        m_nFirstKeyframePts(AV_NOPTS_VALUE),
        m_nPAFFRewind(0),
        m_nPtsWrapArroundThreshold(0xFFFFFFFF) {
        m_list.init();
        static_assert(sizeof(m_list.get()[0]) == sizeof(m_list.get()->data), "FramePos must not have padding.");
    };
    virtual ~FramePosList() {
        clear();
    }
    //filenameに情報をcsv形式で出力する
    int printList(const TCHAR *filename) {
#if !defined(__GNUC__)
        const int nList = (int)m_list.size();
        if (nList == 0) {
            return 0;
        }
        if (filename == nullptr) {
            return 1;
        }
        FILE *fp = NULL;
        if (0 != _tfopen_s(&fp, filename, _T("wb"))) {
            return 1;
        }
        fprintf(fp, "pts,dts,duration,duration2,poc,flags,pic_struct,repeat_pict,pict_type\r\n");
        for (int i = 0; i < nList; i++) {
            fprintf(fp, "%I64d,%I64d,%d,%d,%d,%d,%d,%d,%d\r\n",
                m_list[i].data.pts, m_list[i].data.dts,
                m_list[i].data.duration, m_list[i].data.duration2,
                m_list[i].data.poc,
                (int)m_list[i].data.flags, (int)m_list[i].data.pic_struct, (int)m_list[i].data.repeat_pict, (int)m_list[i].data.pict_type);
        }
        fclose(fp);
#endif
        return 0;
    }
    //indexの位置への参照を返す
    // !! push側のスレッドからのみ有効 !!
    FramePos& list(uint32_t index) {
        return m_list[index].data;
    }
    //初期化
    void clear() {
        m_list.close();
        m_dFrameDuration = 0.0;
        m_nNextFixNumIndex = 0;
        m_bInputFin = false;
        m_nDuration = 0;
        m_nDurationNum = 0;
        m_nStreamPtsStatus = AVQSV_PTS_UNKNOWN;
        m_nLastPoc = 0;
        m_nFirstKeyframePts = AV_NOPTS_VALUE;
        m_nPAFFRewind = 0;
        m_nPtsWrapArroundThreshold = 0xFFFFFFFF;
        m_list.init();
    }
    //ここまで計算したdurationを返す
    int64_t duration() const {
        return m_nDuration;
    }
    //登録された(ptsの確定していないものを含む)フレーム数を返す
    int frameNum() const {
        return (int)m_list.size();
    }
    //ptsが確定したフレーム数を返す
    int fixedNum() const {
        return m_nNextFixNumIndex;
    }
    void clearPtsStatus() {
        if (m_nStreamPtsStatus & AVQSV_PTS_DUPLICATE) {
            const int nListSize = (int)m_list.size();
            for (int i = 0; i < nListSize; i++) {
                if (m_list[i].data.duration == 0
                    && m_list[i].data.pts != AV_NOPTS_VALUE
                    && m_list[i].data.dts != AV_NOPTS_VALUE
                    && m_list[i+1].data.pts - m_list[i].data.pts <= (std::min)(m_list[i+1].data.duration / 10, 1)
                    && m_list[i+1].data.dts - m_list[i].data.dts <= (std::min)(m_list[i+1].data.duration / 10, 1)) {
                    m_list[i].data.duration = m_list[i+1].data.duration;
                }
            }
        }
        m_nLastPoc = 0;
        m_nNextFixNumIndex = 0;
        m_nStreamPtsStatus = AVQSV_PTS_UNKNOWN;
        m_nPAFFRewind = 0;
        m_nPtsWrapArroundThreshold = 0xFFFFFFFF;
    }
    AVQSVPtsStatus getStreamPtsStatus() const {
        return m_nStreamPtsStatus;
    }
    //FramePosを追加し、内部状態を変更する
    void add(const FramePos& pos) {
        m_list.push(pos);
        const int nListSize = (int)m_list.size();
        //自分のフレームのインデックス
        const int nIndex = nListSize-1;
        //ptsの補正
        adjustFrameInfo(nIndex);
        //最初のキーフレームの位置を記憶しておく
        if (m_nFirstKeyframePts == AV_NOPTS_VALUE && (pos.flags & AV_PKT_FLAG_KEY) && nIndex == 0) {
            m_nFirstKeyframePts = m_list[nIndex].data.pts;
        }
        //m_nStreamPtsStatusがAVQSV_PTS_UNKNOWNの場合には、ソートなどは行わない
        if (m_bInputFin || (m_nStreamPtsStatus && nListSize - m_nNextFixNumIndex > (int)AVQSV_FRAME_MAX_REORDER)) {
            //ptsでソート
            sortPts(m_nNextFixNumIndex, nListSize - m_nNextFixNumIndex);
            setPocAndFix(nListSize);
        }
        calcDuration();
    };
    //pocの一致するフレームの情報のコピーを返す
    FramePos copy(int poc, uint32_t *lastIndex) {
        assert(lastIndex != nullptr);
        for (uint32_t index = *lastIndex + 1; ; index++) {
            FramePos pos;
            if (!m_list.copy(&pos, index)) {
                break;
            }
            if (pos.poc == poc) {
                *lastIndex = index;
                return pos;
            }
#if 0
            if (m_bInputFin && pos.poc == -1) {
                //もう読み込みは終了しているが、さらなるフレーム情報の要求が来ている
                //予想より出力が過剰になっているということで、tsなどで最初がopengopの場合に起こりうる
                //なにかおかしなことが起こっており、異常なのだが、最後の最後でエラーとしてしまうのもあほらしい
                //とりあえず、ptsを推定して返してしまう
                pos.poc = poc;
                FramePos pos_tmp ={ 0 };
                m_list.copy(&pos_tmp, index-1);
                int nLastPoc = pos_tmp.poc;
                int64_t nLastPts = pos_tmp.pts;
                m_list.copy(&pos_tmp, 0);
                int64_t pts0 = pos_tmp.pts;
                m_list.copy(&pos_tmp, 1);
                if (pos_tmp.poc == -1) {
                    m_list.copy(&pos_tmp, 2);
                }
                int64_t pts1 = pos_tmp.pts;
                int nFrameDuration = (int)(pts1 - pts0);
                pos.pts = nLastPts + (poc - nLastPoc) * nFrameDuration;
                return pos;
            }
#endif
        }
        //エラー
        FramePos pos ={ 0 };
        pos.poc = AVQSV_POC_INVALID;
        return pos;
    }
    //入力が終了した際に使用し、内部状態を変更する
    void fin(const FramePos& pos, int64_t total_duration) {
        m_bInputFin = true;
        if (m_nStreamPtsStatus == AVQSV_PTS_UNKNOWN) {
            checkPtsStatus();
        }
        const int nFrame = (int)m_list.size();
        sortPts(m_nNextFixNumIndex, nFrame - m_nNextFixNumIndex);
        m_nNextFixNumIndex += m_nPAFFRewind;
        for (int i = m_nNextFixNumIndex; i < nFrame; i++) {
            adjustDurationAfterSort(m_nNextFixNumIndex);
            setPoc(i);
        }
        m_nNextFixNumIndex = nFrame;
        add(pos);
        m_nNextFixNumIndex += m_nPAFFRewind;
        m_nPAFFRewind = 0;
        m_nDuration = total_duration;
        m_nDurationNum = m_nNextFixNumIndex;
    }
    //現在の情報から、ptsの状態を確認する
    //さらにptsの補正、ptsのソート、pocの確定を行う
    void checkPtsStatus(double durationHintifPtsAllInvalid = 0.0) {
        const int nInputPacketCount = (int)m_list.size();
        int nInputFrames = 0;
        int nInputFields = 0;
        int nInputKeys = 0;
        int nDuplicateFrameInfo = 0;
        int nInvalidPtsCount = 0;
        int nInvalidDtsCount = 0;
        int nInvalidPtsCountField = 0;
        int nInvalidPtsCountKeyFrame = 0;
        int nInvalidPtsCountNonKeyFrame = 0;
        int nInvalidDuration = 0;
        bool bFractionExists = std::abs(durationHintifPtsAllInvalid - (int)(durationHintifPtsAllInvalid + 0.5)) > 1e-6;
        vector<std::pair<int, int>> durationHistgram;
        for (int i = 0; i < nInputPacketCount; i++) {
            nInputFrames += (m_list[i].data.pic_struct & AVQSV_PICSTRUCT_FRAME) != 0;
            nInputFields += (m_list[i].data.pic_struct & AVQSV_PICSTRUCT_FIELD) != 0;
            nInputKeys   += (m_list[i].data.flags & AV_PKT_FLAG_KEY) != 0;
            nInvalidDuration += m_list[i].data.duration <= 0;
            if (m_list[i].data.pts == AV_NOPTS_VALUE) {
                nInvalidPtsCount++;
                nInvalidPtsCountField += (m_list[i].data.pic_struct & AVQSV_PICSTRUCT_FIELD) != 0;
                nInvalidPtsCountKeyFrame += (m_list[i].data.flags & AV_PKT_FLAG_KEY) != 0;
                nInvalidPtsCountNonKeyFrame += (m_list[i].data.flags & AV_PKT_FLAG_KEY) == 0;
            }
            if (m_list[i].data.dts == AV_NOPTS_VALUE) {
                nInvalidDtsCount++;
            }
            if (i > 0) {
                //VP8/VP9では重複するpts/dts/durationを持つフレームが存在することがあるが、これを無視する
                if (bFractionExists
                    && m_list[i].data.duration > 0
                    && m_list[i].data.pts != AV_NOPTS_VALUE
                    && m_list[i].data.dts != AV_NOPTS_VALUE
                    && m_list[i].data.pts - m_list[i-1].data.pts <= (std::min)(m_list[i].data.duration / 10, 1)
                    && m_list[i].data.dts - m_list[i-1].data.dts <= (std::min)(m_list[i].data.duration / 10, 1)
                    && m_list[i].data.duration == m_list[i-1].data.duration) {
                    nDuplicateFrameInfo++;
                }
            }
            int nDuration = m_list[i].data.duration;
            auto target = std::find_if(durationHistgram.begin(), durationHistgram.end(), [nDuration](const std::pair<int, int>& pair) { return pair.first == nDuration; });
            if (target != durationHistgram.end()) {
                target->second++;
            } else {
                durationHistgram.push_back(std::make_pair(nDuration, 1));
            }
        }
        //多い順にソートする
        std::sort(durationHistgram.begin(), durationHistgram.end(), [](const std::pair<int, int>& pairA, const std::pair<int, int>& pairB) { return pairA.second > pairB.second; });
        m_nStreamPtsStatus = AVQSV_PTS_UNKNOWN;
        if (nDuplicateFrameInfo > 0) {
            //VP8/VP9では重複するpts/dts/durationを持つフレームが存在することがあるが、これを無視する
            m_nStreamPtsStatus |= AVQSV_PTS_DUPLICATE;
        }
        if (nInvalidPtsCount == 0) {
            m_nStreamPtsStatus |= AVQSV_PTS_NORMAL;
        } else {
            m_dFrameDuration = durationHintifPtsAllInvalid;
            if (nInvalidPtsCount >= nInputPacketCount - 1) {
                if (m_list[0].data.duration || durationHintifPtsAllInvalid > 0.0) {
                    //durationが得られていれば、durationに基づいて、cfrでptsを発行する
                    //主にH.264/HEVCのESなど
                    m_nStreamPtsStatus |= AVQSV_PTS_ALL_INVALID;
                } else {
                    //durationがなければ、dtsを見てptsを発行する
                    //主にVC-1ストリームなど
                    m_nStreamPtsStatus |= AVQSV_PTS_SOMETIMES_INVALID;
                }
            } else if (nInputFields > 0 && nInvalidPtsCountField <= nInputFields / 2) {
                //主にH.264のPAFFストリームなど
                m_nStreamPtsStatus |= AVQSV_PTS_HALF_INVALID;
            } else if (nInvalidPtsCountKeyFrame == 0 && nInvalidPtsCountNonKeyFrame > (nInputPacketCount - nInputKeys) * 3 / 4) {
                m_nStreamPtsStatus |= AVQSV_PTS_NONKEY_INVALID;
                if (nInvalidPtsCount == nInvalidDtsCount) {
                    //ワンセグなど、ptsもdtsもキーフレーム以外は得られない場合
                    m_nStreamPtsStatus |= AVQSV_DTS_SOMETIMES_INVALID;
                }
                if (nInvalidDuration == 0) {
                    //ptsがだいぶいかれてるので、安定してdurationが得られていれば、durationベースで作っていったほうが早い
                    m_nStreamPtsStatus |= AVQSV_PTS_SOMETIMES_INVALID;
                }
            }
            if (!(m_nStreamPtsStatus & (AVQSV_PTS_ALL_INVALID | AVQSV_PTS_HALF_INVALID | AVQSV_PTS_NONKEY_INVALID | AVQSV_PTS_SOMETIMES_INVALID))
                && nInvalidPtsCount > nInputPacketCount / 16) {
                m_nStreamPtsStatus |= AVQSV_PTS_SOMETIMES_INVALID;
            }
        }
        if ((m_nStreamPtsStatus & AVQSV_PTS_ALL_INVALID)) {
            auto& mostPopularDuration = durationHistgram[durationHistgram.size() > 1 && durationHistgram[0].first == 0];
            if ((m_dFrameDuration > 0.0 && m_list[0].data.duration == 0) || mostPopularDuration.first == 0) {
                //主にH.264/HEVCのESなど向けの対策
                m_list[0].data.duration = (int)(m_dFrameDuration * ((m_list[0].data.pic_struct & AVQSV_PICSTRUCT_FIELD) ? 0.5 : 1.0) + 0.5);
            } else {
                //durationのヒストグラムを作成
                m_dFrameDuration = durationHistgram[durationHistgram.size() > 1 && durationHistgram[0].first == 0].first;
            }
        }
        for (int i = m_nNextFixNumIndex; i < nInputPacketCount; i++) {
            adjustFrameInfo(i);
        }
        sortPts(m_nNextFixNumIndex, nInputPacketCount - m_nNextFixNumIndex);
        setPocAndFix(nInputPacketCount);
        if (m_nNextFixNumIndex > 1) {
            int64_t pts0 = m_list[0].data.pts;
            int64_t pts1 = m_list[1 + (m_list[0].data.poc == -1)].data.pts;
            m_nPtsWrapArroundThreshold = (uint32_t)clamp((int64_t)(std::max)((uint32_t)(pts1 - pts0), (uint32_t)(m_dFrameDuration + 0.5)) * 360, 360, (int64_t)0xFFFFFFFF);
        }
    }
protected:
    //ptsでソート
    void sortPts(uint32_t index, uint32_t len) {
#if !defined(_MSC_VER) && __cplusplus <= 201103
        FramePos *pStart = (FramePos *)m_list.get(index);
        FramePos *pEnd = (FramePos *)m_list.get(index + len);
        std::sort(pStart, pEnd, CompareFramePos());
#else
        const auto nPtsWrapArroundThreshold = m_nPtsWrapArroundThreshold;
        std::sort(m_list.get(index), m_list.get(index + len), [nPtsWrapArroundThreshold](const auto& posA, const auto& posB) {
            return ((uint32_t)(std::abs(posA.data.pts - posB.data.pts)) < nPtsWrapArroundThreshold) ? posA.data.pts < posB.data.pts : posB.data.pts < posA.data.pts; });
#endif
    }
    //ptsの補正
    void adjustFrameInfo(uint32_t nIndex) {
        if (m_nStreamPtsStatus & AVQSV_PTS_SOMETIMES_INVALID) {
            if (m_nStreamPtsStatus & AVQSV_DTS_SOMETIMES_INVALID) {
                //ptsもdtsはあてにならないので、durationから再構築する (ワンセグなど)
                if (nIndex == 0) {
                    if (m_list[nIndex].data.pts == AV_NOPTS_VALUE) {
                        m_list[nIndex].data.pts = 0;
                    }
                } else if (m_list[nIndex].data.pts == AV_NOPTS_VALUE) {
                    m_list[nIndex].data.pts = m_list[nIndex-1].data.pts + m_list[nIndex-1].data.duration;
                }
            } else {
                //ptsはあてにならないので、dtsから再構築する (VC-1など)
                int64_t firstFramePtsDtsDiff = m_list[0].data.pts - m_list[0].data.dts;
                if (nIndex > 0 && m_list[nIndex].data.dts == AV_NOPTS_VALUE) {
                    m_list[nIndex].data.dts = m_list[nIndex-1].data.dts + m_list[0].data.duration;
                }
                m_list[nIndex].data.pts = m_list[nIndex].data.dts + firstFramePtsDtsDiff;
            }
        } else if (m_list[nIndex].data.pts == AV_NOPTS_VALUE) {
            if (nIndex == 0) {
                m_list[nIndex].data.pts = 0;
                m_list[nIndex].data.dts = 0;
            } else if (m_nStreamPtsStatus & (AVQSV_PTS_ALL_INVALID | AVQSV_PTS_NONKEY_INVALID)) {
                //AVPacketのもたらすptsが無効であれば、CFRを仮定して適当にptsとdurationを突っ込んでいく
                double frameDuration = m_dFrameDuration * ((m_list[0].data.pic_struct & AVQSV_PICSTRUCT_FIELD) ? 2.0 : 1.0);
                m_list[nIndex].data.pts = (int64_t)(nIndex * frameDuration * ((m_list[nIndex].data.pic_struct & AVQSV_PICSTRUCT_FIELD) ? 0.5 : 1.0) + 0.5);
                m_list[nIndex].data.dts = m_list[nIndex].data.pts;
            } else if (m_nStreamPtsStatus & AVQSV_PTS_NONKEY_INVALID) {
                //キーフレーム以外のptsとdtsが無効な場合は、適当に推定する
                double frameDuration = m_dFrameDuration * ((m_list[0].data.pic_struct & AVQSV_PICSTRUCT_FIELD) ? 2.0 : 1.0);
                m_list[nIndex].data.pts = m_list[nIndex-1].data.pts + (int)(frameDuration * ((m_list[nIndex].data.pic_struct & AVQSV_PICSTRUCT_FIELD) ? 0.5 : 1.0) + 0.5);
                m_list[nIndex].data.dts = m_list[nIndex-1].data.dts + (int)(frameDuration * ((m_list[nIndex].data.pic_struct & AVQSV_PICSTRUCT_FIELD) ? 0.5 : 1.0) + 0.5);
            } else if (m_nStreamPtsStatus & AVQSV_PTS_HALF_INVALID) {
                //ptsがないのは音声抽出で、正常に抽出されない問題が生じる
                //半分PTSがないPAFFのような動画については、前のフレームからの補完を行う
                if (m_list[nIndex].data.dts == AV_NOPTS_VALUE) {
                    m_list[nIndex].data.dts = m_list[nIndex-1].data.dts + m_list[nIndex-1].data.duration;
                }
                m_list[nIndex].data.pts = m_list[nIndex-1].data.pts + m_list[nIndex-1].data.duration;
            } else if (m_nStreamPtsStatus & AVQSV_PTS_NORMAL) {
                if (m_list[nIndex].data.pts == AV_NOPTS_VALUE) {
                    m_list[nIndex].data.pts = m_list[nIndex-1].data.pts + m_list[nIndex-1].data.duration;
                }
            }
        }
    }
    //ソートにより確定したptsに対して、pocを設定する
    void setPoc(int index) {
        if ((m_nStreamPtsStatus & AVQSV_PTS_DUPLICATE)
            && m_list[index].data.duration == 0
            && m_list[index+1].data.pts - m_list[index].data.pts <= (std::min)(m_list[index+1].data.duration / 10, 1)
            && m_list[index+1].data.dts - m_list[index].data.dts <= (std::min)(m_list[index+1].data.duration / 10, 1)) {
            //VP8/VP9では重複するpts/dts/durationを持つフレームが存在することがあるが、これを無視する
            m_list[index].data.poc = AVQSV_POC_INVALID;
        } else if (m_list[index].data.pic_struct & AVQSV_PICSTRUCT_FIELD) {
            if (index > 0 && (m_list[index-1].data.poc != AVQSV_POC_INVALID && (m_list[index-1].data.pic_struct & AVQSV_PICSTRUCT_FIELD))) {
                m_list[index].data.poc = AVQSV_POC_INVALID;
                m_list[index-1].data.duration2 = m_list[index].data.duration;
            } else {
                m_list[index].data.poc = m_nLastPoc++;
            }
        } else {
            m_list[index].data.poc = m_nLastPoc++;
        }
    }
    //ソート後にindexのdurationを再計算する
    //ソートはindex+1まで確定している必要がある
    //ソート後のこの段階では、AV_NOPTS_VALUEはないものとする
    void adjustDurationAfterSort(int index) {
        int diff = (int)(m_list[index+1].data.pts - m_list[index].data.pts);
        if ((m_nStreamPtsStatus & AVQSV_PTS_DUPLICATE)
            && diff <= 1
            && m_list[index].data.duration > 0
            && m_list[index].data.pts != AV_NOPTS_VALUE
            && m_list[index].data.dts != AV_NOPTS_VALUE
            && m_list[index+1].data.duration == m_list[index].data.duration
            && m_list[index+1].data.pts - m_list[index].data.pts <= (std::min)(m_list[index].data.duration / 10, 1)
            && m_list[index+1].data.dts - m_list[index].data.dts <= (std::min)(m_list[index].data.duration / 10, 1)) {
            //VP8/VP9では重複するpts/dts/durationを持つフレームが存在することがあるが、これを無視する
            m_list[index].data.duration = 0;
        } else if (diff > 0) {
            m_list[index].data.duration = diff;
        }
    }
    //進捗表示用のdurationの計算を行う
    //これは16フレームに1回行う
    void calcDuration() {
        int nNonDurationCalculatedFrames = m_nNextFixNumIndex - m_nDurationNum;
        if (nNonDurationCalculatedFrames >= 16) {
            const auto *pos_fixed = m_list.get(m_nDurationNum);
            int64_t duration = pos_fixed[nNonDurationCalculatedFrames-1].data.pts - pos_fixed[0].data.pts;
            if (duration < 0 || duration > m_nPtsWrapArroundThreshold) {
                duration = 0;
                for (int i = 1; i < nNonDurationCalculatedFrames; i++) {
                    int64_t diff = (std::max<int64_t>)(0, pos_fixed[i].data.pts - pos_fixed[i-1].data.pts);
                    int64_t last_frame_dur = (std::max<int64_t>)(0, pos_fixed[i-1].data.duration);
                    duration += (diff > m_nPtsWrapArroundThreshold) ? last_frame_dur : diff;
                }
            }
            m_nDuration += duration;
            m_nDurationNum += nNonDurationCalculatedFrames;
        }
    }
    //pocを確定させる
    void setPocAndFix(int nSortedSize) {
        //ソートによりptsが確定している範囲
        //本来はnSortedSize - (int)AVQSV_FRAME_MAX_REORDERでよいが、durationを確定させるためにはさらにもう一枚必要になる
        int nSortFixedSize = nSortedSize - (int)AVQSV_FRAME_MAX_REORDER - 1;
        m_nNextFixNumIndex += m_nPAFFRewind;
        for (; m_nNextFixNumIndex < nSortFixedSize; m_nNextFixNumIndex++) {
            if (m_list[m_nNextFixNumIndex].data.pts < m_nFirstKeyframePts //ソートの先頭のptsが塚下キーフレームの先頭のptsよりも小さいことがある(opengop)
                && m_nNextFixNumIndex <= 16) { //wrap arroundの場合は除く
                                               //これはフレームリストから取り除く
                m_list.pop();
                m_nNextFixNumIndex--;
                nSortFixedSize--;
            } else {
                adjustDurationAfterSort(m_nNextFixNumIndex);
                //ソートにより確定したptsに対して、pocとdurationを設定する
                setPoc(m_nNextFixNumIndex);
            }
        }
        m_nPAFFRewind = 0;
        //もし、現在のインデックスがフィールドデータの片割れなら、次のフィールドがくるまでdurationは確定しない
        //setPocでduration2が埋まるのを待つ必要がある
        if (m_nNextFixNumIndex > 0
            && (m_list[m_nNextFixNumIndex-1].data.pic_struct & AVQSV_PICSTRUCT_FIELD)
            && m_list[m_nNextFixNumIndex-1].data.poc != AVQSV_POC_INVALID) {
            m_nNextFixNumIndex--;
            m_nPAFFRewind = 1;
        }
    }
protected:
    double m_dFrameDuration; //CFRを仮定する際のフレーム長 (AVQSV_PTS_ALL_INVALID, AVQSV_PTS_NONKEY_INVALID, AVQSV_PTS_NONKEY_INVALID時有効)
    CQueueSPSP<FramePos, 1> m_list; //内部データサイズとFramePosのデータサイズを一致させるため、alignを1に設定
    int m_nNextFixNumIndex; //次にptsを確定させるフレームのインデックス
    bool m_bInputFin; //入力が終了したことを示すフラグ
    int64_t m_nDuration; //m_nDurationNumのフレーム数分のdurationの総和
    int m_nDurationNum; //durationを計算したフレーム数
    AVQSVPtsStatus m_nStreamPtsStatus; //入力から提供されるptsの状態 (AVQSV_PTS_xxx)
    uint32_t m_nLastPoc; //ptsが確定したフレームのうち、直近のpoc
    int64_t m_nFirstKeyframePts; //最初のキーフレームのpts
    int m_nPAFFRewind; //PAFFのdurationを確定させるため、戻した枚数
    uint32_t m_nPtsWrapArroundThreshold; //wrap arroundを判定する閾値
};
