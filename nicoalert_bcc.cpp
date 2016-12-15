//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_bcc.h"
#include "nicoalert_cmm.h"
#include "nicoalert_snd.h"

// アラート通知データキュー
static deque_ts<c_alertinfo> alinfo;
// アラート通知イベント
static HANDLE hBCSCEvent = NULL;
// アラート通知済み放送番号キュー
static deque < pair<time_t, tstring> > bcc_queue;

// スレッド終了通知
static HANDLE hBCSCExitEvent = NULL;
static volatile bool isBCSCExit = false;

// 各種コールバック
static void(*callback_alinfo_ntf)(unsigned int, BOOL) = NULL;
static BOOL(*callback_msgpopup)(const tstring &msg, const tstring &link) = NULL;
static void(*callback_msginfo)(const TCHAR *) = NULL;

static class nicoalert_snd nasnd;

#ifdef _DEBUG
static volatile int DEBUG_bcc_check_strcnt = 0;
static volatile int DEBUG_bcc_check_endcnt = 0;
static volatile int DEBUG_bcc_check_cnkcnt = 0;
static volatile int DEBUG_bcc_check_errcnt = 0;
#endif

// アラート通知受付(callback)
void alertinfo_ind(const c_alertinfo *ai){
    alinfo.lock();
    alinfo.push_back(*ai);
    SetEvent(hBCSCEvent);
    alinfo.unlock();
}

// サウンドテスト受付
bool bcc_soundtest(const TCHAR *fn, int vol){
    nasnd.setVol(vol);
    return nasnd.play(fn);
}
void bcc_soundvol(int vol){
    nasnd.setVolPlaying(vol);
}

#if 0
// 放送開始数計測
static deque_ts<unsigned int> start_count;
const unsigned int start_count_period       =  15;  // 計測単位
const unsigned int start_count_period_num   = 120;  // 計測単位数(15s*120 = 30min.)
static bool bcc_countup(tstring &lvid){
    static unsigned int start_count_tick;               // 最終計測単位時刻

    start_count.lock();
    if(start_count_tick != (now/start_count_period)){
        if(start_count.size() > start_count_period_num){
            start_count.pop_front();
        }
        start_count.push_back(0);
        start_count_tick = (now/start_count_period);
    }
    (*(start_count.end()-1))++;

    if(0){
        tstring tmp;
        TCHAR buf[256];
        FOREACH(it, start_count){
            _stprintf_s(buf, _T("%u "), *it);
            tmp += buf;
        }
        _dbg(_T("%s\n"), tmp.c_str());
    }
    start_count.unlock();

    return true;
}

bool bcc_startstat(unsigned int *count, unsigned int *period){
    unsigned int sum = 0, num = 0;
    start_count.lock();
    num = start_count.size()-1;
    for(unsigned int i = 0; i < num; i++){ sum += start_count[i]; }

    if(1){
        tstring tmp;
        TCHAR buf[256];
        FOREACH(it, start_count){
            _stprintf_s(buf, _T("%u "), *it);
            tmp += buf;
        }
        _dbg(_T("%s\n"), tmp.c_str());
    }
    start_count.unlock();

    *count = sum;
    *period = start_count_period * num;
    return true;
}
#endif

// アラート通知処理
static void bcc_check(int infotype, tstring &lvid, unsigned int idx[3]){

    time_t now = time(NULL);

    // 通知済み放送番号のデータを削除(BCC_QUEUE_TIME sec保持)
    while(bcc_queue.size() && bcc_queue.front().first + BCC_QUEUE_TIME < now){
        bcc_queue.pop_front();
    }
    // すでに通知済みの放送番号の場合通知しない
    FOREACH(it, bcc_queue){
        if(it->second == lvid) return;
    }
    bcc_queue.push_back(make_pair(now, lvid));

    c_regdata mrd, *trd = NULL;
    regdata_info.lock();

    // 通知方式を決定(最大3つの一致キーから該当する通知方式をor加算)
    unsigned int notify = 0;
    for(int i = 0; i < 3; i++){
        if(idx[i] == 0) continue;
        ITER(regdata_info) it = regdata_info.find(idx[i]);
        if(it == regdata_info.end()) continue;

        c_regdata *rd = &(it->second);
        if(rd->idx != idx[i]) continue;

        rd->last_lv = lvid;
        rd->last_start = now;
        if(rd->notify & NOTIFY_ENABLE){
            notify |= rd->notify;
        }
        // リストビュー更新通知
        if(callback_alinfo_ntf){
            callback_alinfo_ntf(idx[i], TRUE);
        }

        if(!trd) trd = rd;
    }
    if(trd) mrd = *trd;

    regdata_info.unlock();

    // dbidxが引けなかった場合(ガード)
    if(mrd.key.size() == 0) return;

    // ログウィンドウに通知(要regdata_info解除)
    if(callback_msginfo){
        tstring msg;
        msg = mrd.key + _T(" : ") + lvid + _T(" が放送開始しました。");
        callback_msginfo(msg.c_str());
    }

    // バルーン通知ビット
    if((notify & NOTIFY_BALLOON)){
        tstring msg, msg2;
        switch(infotype){
        case INFOTYPE_OFFICIAL:
            msg += _T("[公式生放送]");
            break;
        case INFOTYPE_CHANNEL:
            msg += _T("[チャンネル生放送]");
            break;
        case INFOTYPE_USER:
            msg += _T("[ユーザー生放送]");
            break;
        }
        msg2 = msg + _T(" ") + mrd.key_name + _T(" が放送開始しました。");
        //msg += _T("\n");
        msg = msg + _T(" (") + lvid + _T(")\n");
        msg = msg + mrd.key_name + _T("(") + mrd.key + _T(")") + _T(" が放送開始しました。\n");

        if(infotype != INFOTYPE_OFFICIAL){
            // 公式生放送は取得エラーになるため取得しない
            c_streaminfo si;
            if(nicoalert_getstreaminfo(lvid, si) == CMM_SUCCESS){
                msg = msg + _T("【") + si.title + _T("】\n");
                msg = msg + si.desc + _T("\n");
                msg2 = msg2 + si.title + _T(" / ") + si.desc;
            }
        } else {
            // lv指定じゃない公式生放送は放送ページからタイトルを取得
            if(mrd.key.substr(0, LVID_PREFIX_LEN) != _T(LVID_PREFIX)){
                int failcnt = 0;
                tstring keyname;
                while(1){
                    if(nicoalert_getkeyname(lvid, keyname) == CMM_SUCCESS) break;
                    keyname = _T("放送タイトル取得に失敗しました。");
                    //callback_msginfo(keyname.c_str());

                    failcnt++;
                    if(failcnt >= 5) break;
                    Sleep(1000*failcnt);
                }
                msg = msg + _T("【") + keyname + _T("】\n");
                msg2 = msg2 + keyname;
            }
        }

        if(callback_msginfo){
            callback_msginfo(msg2.c_str());
        }

        // 棒読みちゃん
        if(ReadOptionInt(OPTION_BALLOON_BOUYOMI, DEF_OPTION_BALLOON_BOUYOMI)){
            _dbg(_T("bouyomi send start\n"));
            nicoalert_bouyomi(msg2);
            _dbg(_T("bouyomi send end\n"));
        }

        // タスクトレイバルーン表示実行
        if(callback_msgpopup){
            trim_trailws(msg);
            callback_msgpopup(msg, lvid);
        }
    }

    // サウンド通知ビット
    if(notify & NOTIFY_SOUND){
        tstring &p = ReadOptionString(OPTION_SOUND_PATH, DEF_OPTION_SOUND_PATH);
        if(p == _T("")){
            // サウンド未定義の場合、システム音声
            PlaySound(_T("SystemAsterisk"), NULL, SND_ALIAS | SND_ASYNC | SND_NOWAIT);
        } else {
            nasnd.setVol(ReadOptionInt(OPTION_SOUND_VOL, DEF_OPTION_SOUND_VOL));
            nasnd.play(p.c_str());
        }
    }

    // ブラウザ通知ビット
    if(notify & NOTIFY_BROWSER){
        // 該当する放送ページを開く
        if(!exec_browser_by_key(lvid)){
            if(callback_msginfo){
                callback_msginfo(_T("ブラウザ起動に失敗しました。"));
            }
        }
    }

    // 外部アプリ通知ビット
    if(notify & NOTIFY_EXTAPP){
        int ret;
        tstring &p = ReadOptionString(OPTION_EXTAPP_PATH, _T(""));
        tstring opt;
        if(p == _T("")){
            callback_msginfo(_T("外部アプリが指定されていません。"));
        } else {
            splitpath_opt(p, opt);
            opt += _T(" ");
            opt += lvid;

            ret = (int)ShellExecute(NULL, _T("open"), p.c_str(), opt.c_str(), NULL, SW_SHOWNORMAL);
            if(ret < 32){
                if(callback_msginfo){
                    callback_msginfo(_T("外部アプリ起動に失敗しました。"));
                }
            }
        }
    }
}


// アラート通知データ処理スレッド
static void bcc_check_thread(void *arg){
    while(!isBCSCExit){
        WaitForSingleObject(hBCSCEvent, INFINITE);

        _DEBUGDO(DEBUG_bcc_check_strcnt++);
        while(!isBCSCExit){

            // アラート通知データキューからデキュー
            c_alertinfo ai;
            alinfo.lock();
            if(alinfo.size()){
                ai = alinfo.front();
                alinfo.pop_front();
            } else {
                alinfo.unlock();
                break;
            }
            alinfo.unlock();

            // keepaliveは無視
            if(ai.infotype == INFOTYPE_NODATA) continue;

            unsigned int idx[3];
            idx[0] = idx[1] = idx[2] = 0;

            regdata_hash.lock();

            // アラート通知種別ごとに登録キーをチェック
            //  公式生:       放送番号、ユーザID = user/0
            //  チャンネル生: 放送番号、チャンネル番号、ユーザID
            //  ユーザー生:   放送番号、コミュニティ番号、ユーザID
            ITER(regdata_hash) it;
            it = regdata_hash.find(ai.lvid);
            if(it != regdata_hash.end()) idx[0] = it->second;
            it = regdata_hash.find(ai.usrid);
            if(it != regdata_hash.end()) idx[1] = it->second;

            switch(ai.infotype){
            case INFOTYPE_OFFICIAL:
                break;
            case INFOTYPE_CHANNEL:
                it = regdata_hash.find(ai.chid);
                if(it != regdata_hash.end()) idx[2] = it->second;
                break;
            case INFOTYPE_USER:
                it = regdata_hash.find(ai.coid);
                if(it != regdata_hash.end()) idx[2] = it->second;
                break;
            }

            regdata_hash.unlock();

            // 登録キーにヒットした場合、通知処理実施
            if(idx[0] != 0 || idx[1] != 0 || idx[2] != 0){
                bcc_check(ai.infotype, ai.lvid, idx);
            }
        }
        _DEBUGDO(DEBUG_bcc_check_endcnt++);
    }

    bcc_queue.clear();
    // スレッド終了通知
    SetEvent(hBCSCExitEvent);
}

// bccスレッド起動
bool bcc_start(
    void(*cb_alinfo_ntf)(unsigned int, BOOL),
    BOOL(*cb_msgpopup)(const tstring &msg, const tstring &link),
    void(*cb_msginfo)(const TCHAR *) )
{

    callback_alinfo_ntf = cb_alinfo_ntf;
    callback_msgpopup = cb_msgpopup;
    callback_msginfo = cb_msginfo;

    hBCSCEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hBCSCExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    isBCSCExit = false;

    if(-1 == _beginthread(bcc_check_thread, 0, NULL)){
        return false;
    }
    return true;
}

// bccスレッド停止
bool bcc_exit(void){
    int waitcount = PROCESS_EXIT_WAITCOUNT;
    isBCSCExit = true;
    DWORD ret;

    callback_alinfo_ntf = NULL;
    callback_msgpopup = NULL;
    callback_msginfo = NULL;

    while(waitcount){
        SetEvent(hBCSCEvent);
        doevent();
        ret = WaitForSingleObject(hBCSCExitEvent, 500);
        if(ret == WAIT_OBJECT_0) break;
        waitcount--;
    }

    nasnd.close();

    alinfo.lock();
    alinfo.clear();
    alinfo.unlock();

    return true;
}

bool bcc_check(void){
#ifdef _DEBUG
    if(DEBUG_bcc_check_strcnt != DEBUG_bcc_check_endcnt){
        if(DEBUG_bcc_check_strcnt == DEBUG_bcc_check_cnkcnt){
            DEBUG_bcc_check_errcnt++;
        }
        DEBUG_bcc_check_cnkcnt = DEBUG_bcc_check_strcnt;
    } else {
        DEBUG_bcc_check_errcnt = 0;
    }
    if(DEBUG_bcc_check_errcnt >= 3){
        return false;
    }
#endif
    return true;
}