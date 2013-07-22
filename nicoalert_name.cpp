//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_name.h"
#include "nicoalert_cmm.h"

// キー名称取得イベント
static HANDLE hConEvent = NULL;

// スレッド終了通知
static HANDLE hConExitEvent = NULL;
static volatile bool isConExit = false;

// 各種コールバック
static void(*callback_alinfo_ntf)(unsigned int, BOOL) = NULL;
static void(*callback_msginfo)(const TCHAR *) = NULL;

// キー名称取得要求インデックスキュー
static deque_ts<unsigned int> acq_dbidx;
// 取得タイミング制御用キュー
static deque<time_t> acq_check;


static void keyname_acq_nummsg(unsigned int num, bool flag){
    if(!flag){
        if(num >= 100 && (num % 100) != 0) return;
        if(num < 100 && (num % 10) != 0) return;
    }
    if(callback_msginfo){
        if(num == 0){
            if(!flag){
                callback_msginfo(_T("コミュ/チャンネル/放送名 取得完了"));
            }
        } else {
            TCHAR msg[128];
            _stprintf_s(msg, _T("コミュ/チャンネル/放送名取得中: 残%d件"), num);
            callback_msginfo(msg);
        }
    }
}

// キー名称取得スレッド
static void keyname_acq_thread(void *arg){
    while(!isConExit){
        WaitForSingleObject(hConEvent, INFINITE);

        while(!isConExit){
            if(ReadOptionInt(OPTION_AUTO_KEYNAME_ACQ, DEF_OPTION_AUTO_KEYNAME_ACQ) == 0) break;

            // キー名称取得要求インデックスキューからデキュー
            unsigned int dbidx;
            acq_dbidx.lock();
            _dbg(_T("acq_dbidx.size() = %d\n"), acq_dbidx.size());
            if(acq_dbidx.size()){
                dbidx = acq_dbidx.front();
                acq_dbidx.pop_front();
            } else {
                acq_dbidx.unlock();
                break;
            }
            acq_dbidx.unlock();

            // 登録アイテムDBを検索し、
            // 自動取得済みフラグが立っている場合は何もしない
            tstring key = _T("");
            tstring keyname;
            regdata_info.lock();
            ITER(regdata_info) it = regdata_info.find(dbidx);
            if(it != regdata_info.end()){
                if(!(it->second.notify & NOTIFY_AUTOCOACQ)){
                    key = it->second.key;
                }
            }
            regdata_info.unlock();
            if(key == _T("")){
                keyname_acq_nummsg(acq_dbidx.size(), false);
                continue;
            }

            // キー名称取得要求
            if(nicoalert_getkeyname(key, keyname) != CMM_SUCCESS){
                keyname = _T("<取得失敗>");
            }

            // 取得したキー名称が長すぎる場合 規定長で切断し ...を付ける
            if(keyname.size() > REGDATA_KEYNAME_MAXLEN){
                keyname.replace(REGDATA_KEYNAME_MAXLEN-3, keyname.size(), _T("..."));
            }

            // 登録アイテムDBに保存(取得済みフラグON)
            regdata_info.lock();
            it = regdata_info.find(dbidx);
            if(it != regdata_info.end()){
                if(it->second.key_name == _T("")){
                    it->second.key_name = keyname;
                }
                it->second.notify |= NOTIFY_AUTOCOACQ;
            }
            regdata_info.unlock();

            _dbg(_T("key:%s : %s\n"), key.c_str(), keyname.c_str());

            // メインウィンドウのリストビュー更新
            if(callback_alinfo_ntf){
                callback_alinfo_ntf(dbidx, FALSE);
            }
            keyname_acq_nummsg(acq_dbidx.size(), false);

            // 自動取得間隔の制御
            //  取得数が ACQ_CHECK_COUNT に達するまでは最低wait(1秒)
            //  それ以上の場合、ACQ_CHECK_TIME_SL 秒間隔に制限
            //  ACQ_CHECK_TIME 秒以上経過すると取得数カウントキューから抜ける
            while(!isConExit){
                if(acq_check.size() >= ACQ_CHECK_COUNT){
                    if(acq_check.front() + ACQ_CHECK_TIME < time(NULL)){
                        acq_check.pop_front();
                    }
                    if(acq_check.size()){
                        if(acq_check.back() + ACQ_CHECK_TIME_SL < time(NULL)){
                            break;
                        }
                    }
                } else {
                    break;
                }
                Sleep(500);
            }
            acq_check.push_back(time(NULL));
            Sleep(1000);

        }
    }

    acq_check.clear();
    // スレッド終了通知
    SetEvent(hConExitEvent);
}

// 自動取得処理の開始
void con_start(void){
    if(ReadOptionInt(OPTION_AUTO_KEYNAME_ACQ, DEF_OPTION_AUTO_KEYNAME_ACQ)){
        keyname_acq_nummsg(acq_dbidx.size(), true);
        SetEvent(hConEvent);
    }
}

// 自動取得処理キューに追加(IN: dbidx)
void con_add(unsigned int mindbidx){
    acq_dbidx.lock();
    regdata_info.lock();
    FOREACH(it, regdata_info){
        if( it->second.idx >= mindbidx &&
            !(it->second.notify & NOTIFY_AUTOCOACQ) )
        {
            acq_dbidx.push_back(it->second.idx);
        }
    }
    regdata_info.unlock();
    acq_dbidx.unlock();
    con_start();
}

// 自動取得処理スレッド起動
bool con_start( void(*cb_alinfo_ntf)(unsigned int, BOOL), void(*cb_msginfo)(const TCHAR *) ){
    callback_alinfo_ntf = cb_alinfo_ntf;
    callback_msginfo = cb_msginfo;

    hConEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hConExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    isConExit = false;

    if(-1 == _beginthread(keyname_acq_thread, 0, NULL)){
        return false;
    }
    con_add(0);

    return true;
}

// 自動取得処理スレッド終了
bool con_exit(void){
    int waitcount = PROCESS_EXIT_WAITCOUNT;
    isConExit = true;
    DWORD ret;

    callback_alinfo_ntf = NULL;
    callback_msginfo = NULL;

    while(waitcount){
        SetEvent(hConEvent);
        doevent();
        ret = WaitForSingleObject(hConExitEvent, 500);
        if(ret == WAIT_OBJECT_0) break;
        waitcount--;
    }

    acq_dbidx.lock();
    acq_dbidx.clear();
    acq_dbidx.unlock();

    return true;
}
