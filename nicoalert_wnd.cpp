//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_wnd.h"
#include "nicoalert_db.h"
#include "nicoalert_cmm.h"
#include "nicoalert_bcc.h"
#include "nicoalert_name.h"
#include "nicoalert_ole.h"
#include "nicoalert_label.h"

static HWND hDlg;
static HWND hWndListView;
static HACCEL hAccel = NULL;
static HHOOK HHook;
static FARPROC Org_WndProc_Listview;
static deque_ts<time_t> RedrawTimer;

static class nicoalert_db nadb;
static void msginfo_ind(const TCHAR *msg);

// リストビュー描画ロック
static inline void LV_Lock(HWND hWndListView){
    SendMessage(hWndListView, WM_SETREDRAW, (WPARAM)FALSE, NULL);
    LockWindowUpdate(hWndListView);
}
// リストビュー描画ロック解除
static inline void LV_UnLock(HWND hWndListView){
    LockWindowUpdate(NULL);
    SendMessage(hWndListView, WM_SETREDRAW, (WPARAM)TRUE, NULL);
}

// リストビュー用カラムデータ
static const struct COLUMN{
    int width;
    int format;
    TCHAR *str;
} header[] = {
    {30,  LVCFMT_CENTER|LVCFMT_IMAGE,   _T("4")},
    {200, LVCFMT_LEFT,                  _T("コミュ/チャンネル/放送主/番組名")},
    {40,  LVCFMT_LEFT,                  _T("番号")},
    {100, LVCFMT_LEFT,                  _T("最近の放送開始")},
    {30,  LVCFMT_CENTER|LVCFMT_IMAGE,   _T("0")},
    {30,  LVCFMT_CENTER|LVCFMT_IMAGE,   _T("1")},
    {30,  LVCFMT_CENTER|LVCFMT_IMAGE,   _T("2")},
    {30,  LVCFMT_CENTER|LVCFMT_IMAGE,   _T("3")},
    {100, LVCFMT_LEFT,                  _T("メモ")},
    {100, LVCFMT_LEFT,                  _T("ラベル")},
};

// リストビューにカラム追加
static void InsertColumn(HWND hWndListView){
    int i;
    LV_COLUMN lvcol;
    HIMAGELIST himhd;

    himhd = ImageList_Create(ICO_SW, ICO_SH, ILC_COLOR32 | ILC_MASK, 4, 16);
    ImageList_AddIcon(himhd, LoadIconRsc(IDI_BALLOON));
    ImageList_AddIcon(himhd, LoadIconRsc(IDI_BROWSER));
    ImageList_AddIcon(himhd, LoadIconRsc(IDI_SOUND));
    ImageList_AddIcon(himhd, LoadIconRsc(IDI_EXTAPP));
    ImageList_AddIcon(himhd, LoadIconRsc(IDI_NOTIFY));
    Header_SetImageList(ListView_GetHeader(hWndListView), himhd);

    int mask = LVCF_FMT | LVCF_WIDTH | LVCF_SUBITEM;
    for(i = 0; i < (sizeof(header)/sizeof(COLUMN)); i++){
        lvcol.mask = mask;
        lvcol.iSubItem = i;
        lvcol.cx = header[i].width;
        lvcol.fmt = header[i].format;
        if(header[i].format & LVCFMT_IMAGE){
            lvcol.mask |= LVCF_IMAGE;
            lvcol.iImage = _tstoi(header[i].str);
        } else {
            lvcol.mask |= LVCF_TEXT;
            lvcol.pszText = header[i].str;
        }
        ListView_InsertColumn(hWndListView, i, &lvcol);
    }

    ListView_SetExtendedListViewStyle(hWndListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER | LVS_EX_TRACKSELECT);
}

#define LISTVIEW_STRINGBUFFER 256 // 文字列生成用内部バッファ

// サブアイテムのデータ更新
static void UpdateListView(HWND hWndListView, unsigned int lvidx, c_regdata *rd){
    LV_ITEM item;
    TCHAR szStringBuffer[LISTVIEW_STRINGBUFFER];

    item.iItem = lvidx;
    item.mask = LVIF_TEXT;
    item.pszText = szStringBuffer;
    item.iSubItem = 1;

    // key_name
    _stprintf_s(szStringBuffer, _T("%s"), rd->key_name.c_str());
    ListView_SetItem(hWndListView, &item); item.iSubItem++;

    // key
    _stprintf_s(szStringBuffer, _T("%s"), rd->key.c_str());
    ListView_SetItem(hWndListView, &item); item.iSubItem++;

    // last_start
    TCHAR timebuf[24];
    if(rd->last_start){
        timefmt(timebuf, ESIZEOF(timebuf), rd->last_start);
        _stprintf_s(szStringBuffer, _T("%s"), timebuf);
    } else {
        _stprintf_s(szStringBuffer, _T("%s"), _T("なし"));
    }
    ListView_SetItem(hWndListView, &item); item.iSubItem++;

    // notify *4
    szStringBuffer[0] = '\0';
    ListView_SetItem(hWndListView, &item); item.iSubItem++;
    ListView_SetItem(hWndListView, &item); item.iSubItem++;
    ListView_SetItem(hWndListView, &item); item.iSubItem++;
    ListView_SetItem(hWndListView, &item); item.iSubItem++;

    // memo
    _stprintf_s(szStringBuffer, _T("%s"), rd->memo.c_str());
    ListView_SetItem(hWndListView, &item); item.iSubItem++;

    // label
    item.pszText = LPSTR_TEXTCALLBACK;
    ListView_SetItem(hWndListView, &item); item.iSubItem++;
}

// リストビューの全更新
static void UpdateListViewAll(HWND hWndListView){

    LV_ITEM item;

    //int idx = 0;

    //SCROLLINFO si;
    //memset(&si, 0, sizeof(SCROLLINFO));
    //si.cbSize = sizeof(SCROLLINFO);
    //si.fMask = SIF_POS;
    //GetScrollInfo(hWndListView, SB_VERT, &si);

    //DWORD dwStyle = GetWindowLong(hWndListView, GWL_STYLE);
    //SetWindowLong(hWndListView, GWL_STYLE, dwStyle | LVS_NOSCROLL);

    LV_Lock(hWndListView);
    ListView_DeleteAllItems(hWndListView);

    unsigned int num = 0;

    regdata_info.lock();
    FOREACH(it, regdata_info){
        c_regdata *rd = &(it->second);

        item.iItem = num;
        item.pszText = NULL;
        item.iSubItem = 0;
        item.lParam = rd->idx;

        item.mask = LVIF_PARAM;
        ListView_InsertItem(hWndListView, &item); item.iSubItem++;
        ListView_SetCheckState(hWndListView, num, (rd->notify & NOTIFY_ENABLE)?TRUE:FALSE);

        UpdateListView(hWndListView, num, rd);
        num++;
    }
    regdata_info.unlock();

    // カラム幅をオプションから復旧
    int itemcount = Header_GetItemCount(ListView_GetHeader(hWndListView));
    for(int i = 0; i < itemcount; i++){
        if(!(header[i].format & LVCFMT_IMAGE)){
            TCHAR szHeaderWidthKey[64];
            _stprintf_s(szHeaderWidthKey, OPTION_HEADER_WIDTH_FORMAT, i);
            int width = ReadOptionInt(szHeaderWidthKey, -1);
            if(width < 0) width = LVSCW_AUTOSIZE_USEHEADER;
            ListView_SetColumnWidth(hWndListView, i, width);
        }
    }

    LV_UnLock(hWndListView);
    ListView_Scroll(hWndListView, 0, 0);
}

// リストビューの新規アイテム追加(IN: 追加する最低dbidx)
static void InsertListViewAdd(HWND hWndListView, unsigned int mindbidx){
    LV_ITEM item;

    unsigned int num = ListView_GetItemCount(hWndListView);

    LV_Lock(hWndListView);
    regdata_info.lock();

    FOREACH(it, regdata_info){
        c_regdata *rd = &(it->second);
        if(rd->idx < mindbidx) continue;

        item.iItem = num;
        item.pszText = NULL;
        item.iSubItem = 0;
        item.lParam = rd->idx;

        item.mask = LVIF_PARAM;
        ListView_InsertItem(hWndListView, &item); item.iSubItem++;
        ListView_SetCheckState(hWndListView, num, (rd->notify & NOTIFY_ENABLE)?TRUE:FALSE);

        UpdateListView(hWndListView, num, rd);
        num++;
    }

    regdata_info.unlock();
    LV_UnLock(hWndListView);
}


// リストビューアイテム削除
static void DeleteListView(HWND hWndListView, unsigned int lvidx){
    ListView_DeleteItem(hWndListView, lvidx);
}

// データベースファイルのパス生成
static const TCHAR *GetDBFilePath(){
    static TCHAR szDBFile[_MAX_PATH + 1];
	if(szDBFile[0] == 0){
        DWORD ret;
        int err;
		TCHAR szFileDrv [_MAX_DRIVE + 1];
		TCHAR szFileDir [_MAX_DIR + 1];
		TCHAR szFileName[_MAX_FNAME + 1];
        TCHAR szFileExt [_MAX_EXT + 1];
		ret = GetModuleFileName(NULL, szDBFile, ESIZEOF(szDBFile));
        if(ret == 0) return NULL;
		err = _tsplitpath_s(szDBFile, szFileDrv, szFileDir, szFileName, szFileExt);
        if(err != 0) return NULL;
        err = _tmakepath_s(szDBFile, szFileDrv, szFileDir, szFileName, _T(".db"));
        if(err != 0) return NULL;
	}
	return szDBFile;
}

// データベース読み出し(設定情報/登録情報)
static bool LoadDatabase(){
    if(!nadb.open(GetDBFilePath())){
        return false;
    }
    if(!nadb.loadsetting(setting_info)){
        return false;
    }

    // スキーマバージョンチェック
    int dbver = ReadOptionInt(OPTION_DATABASE_VERSION, OPTION_DATABASE_VERSION_DEF);

    // 登録情報テーブルが存在しない場合、最新版とする。
    if(!nadb.istableexistregdata()){
        dbver = OPTION_DATABASE_VERSION_NEW;
        SaveOptionInt(OPTION_DATABASE_VERSION, dbver);
    }
    if(dbver != OPTION_DATABASE_VERSION_NEW){
        if(MessageBox(hDlg, _T("古いデータベースを検出したため、自動的にバージョンアップします。nicoalert.dbファイルのバックアップを取っておくことを推奨します。\nOKボタンを押すと継続します。"), PROGRAM_NAME, MB_OKCANCEL | MB_ICONASTERISK) != IDOK){
            return false;
        }

        // バージョンアップトランザクション開始
        if(!nadb.tr_begin()) return false;

        bool success = false;
        do {
            // 旧バージョンDBスキーマをバージョンアップ
            if(!nadb.verupregdata(dbver, OPTION_DATABASE_VERSION_NEW)){
                // 変更失敗
                break;
            }
            // バージョンアップ成功したら即座にバージョン値を書き込み
            SaveOptionInt(OPTION_DATABASE_VERSION, OPTION_DATABASE_VERSION_NEW);
            if(!nadb.savesetting(setting_info)){
                // バージョン値書き込み失敗
                break;
            }
            success = true;
        } while(0);
        if(!success){
            nadb.tr_rollback();
            return false;
        }
        nadb.tr_commit();
        nadb.cleanup();
        // バージョンアップトランザクション終了
    }

    if(!nadb.loadregdata(regdata_info)){
        return false;
    }

    // キー番号からdbidxへのハッシュ生成
    regdata_info.lock(); regdata_hash.lock();
    FOREACH(it, regdata_info){
        regdata_hash[it->second.key] = it->first;
    }
    regdata_hash.unlock(); regdata_info.unlock();

    return true;
}

// データベースクローズ
static bool CloseDataBase(){
    setting_info.clear();
    regdata_info.clear();
    regdata_hash.clear();
    nadb.cleanup();
    return nadb.close();
}

// 再描画タイマ設定(放送開始30分以内のカラーリング消去)
static void SetRedrawTimer(HWND hWnd){
    time_t now = time(NULL);
    RedrawTimer.lock();
    while(!RedrawTimer.empty() && RedrawTimer.front() <= now){
        RedrawTimer.pop_front();
    }
    _dbg(_T("SetRedrawTimer : timer count = %u\n"), RedrawTimer.size());
    if(!RedrawTimer.empty()){
        time_t t = RedrawTimer.front() - now + 1;
        SetTimer(hWnd, TID_REDRAW, (UINT)(t * 1000), NULL);
        _dbg(_T("SetRedrawTimer : settimer %u\n"), t * 1000);
    } else {
        KillTimer(hWnd, TID_REDRAW);
        _dbg(_T("SetRedrawTimer : killtimer\n"));
    }

    RedrawTimer.unlock();
}

static void InitRedrawTimer(HWND hWnd){
    time_t now = time(NULL);
    regdata_info.lock();
    RedrawTimer.lock();
    FOREACH(it, regdata_info){
        time_t t = it->second.last_start + BROADCAST_PERIOD;
        if(t > now) RedrawTimer.push_back(t);
    }
    sort(RedrawTimer.begin(), RedrawTimer.end());
    _dbg(_T("InitRedrawTimer : timer count = %u\n"), RedrawTimer.size());
    RedrawTimer.unlock();
    regdata_info.unlock();

    SetRedrawTimer(hWnd);
}

static void AddRedrawTimer(HWND hWnd, unsigned int dbidx){
    time_t now = time(NULL);
    regdata_info.lock();
    do {
        if(dbidx <= 0) break;

        ITER(regdata_info) it = regdata_info.find(dbidx);
        if(it == regdata_info.end()) break;
        c_regdata &rd = it->second;
        if(rd.idx != dbidx) break;

        RedrawTimer.lock();
        time_t t = rd.last_start + BROADCAST_PERIOD;
        if(t > now) RedrawTimer.push_back(t);
        _dbg(_T("AddRedrawTimer : timer count = %u\n"), RedrawTimer.size());
        RedrawTimer.unlock();

    } while(0);
    regdata_info.unlock();

    SetRedrawTimer(hWnd);
}

// ツールチップウィンドウ初期化
HWND ToolTipInit(HWND hDlgWnd, HINSTANCE hInstance, const ToolTipInfo *tti, int num){
    // ツールチップウィンドウ生成
    HWND hToolTip = CreateWindowEx(
        0, TOOLTIPS_CLASS, NULL, 0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hDlgWnd, NULL, hInstance, NULL);
    if(!hToolTip) return NULL;

    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(TOOLINFO);
    ti.hwnd = hDlgWnd;
    ti.hinst = hInstance; 
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;

    // ツールチップデータ登録
    for(int i = 0; i < num; i++){
        HWND hWnd = GetDlgItem(hDlgWnd, tti[i].uDlgItem);
        ti.uId = (UINT_PTR)hWnd;
        ti.lpszText = tti[i].msg;
        SendMessage(hToolTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
    }

    // 折り返し用
    SendMessage(hToolTip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)1024); 

    return hToolTip;
}


// キー番号比較用プレフィックスリスト
const TCHAR *checkstr[] = {
    _T(USERID_PREFIX), _T(COID_PREFIX), _T(CHID_PREFIX), _T(LVID_PREFIX)
};

// キー番号の比較
//   基本的にアルファベット順、
//   プレフィックス部分が同一の場合、後続の数値順にソート
static int key_compare(const tstring &p1, const tstring &p2){
    const TCHAR *cp1 = p1.c_str();
    const TCHAR *cp2 = p2.c_str();
    while(1){
        if(!_istdigit(*cp1) || !_istdigit(*cp2)){
            if(*cp1 != *cp2) return (*cp1 - *cp2);
        } else {
            // 両方数字
            return (_tstoi(cp1) - _tstoi(cp2));
        }
        cp1++; cp2++;
    }
}

// std:set用のソートクラス
class CKeySort : public std::binary_function< tstring, tstring, bool > {
    public:
    bool operator() (const tstring &p1, const tstring &p2) const {
        return (key_compare(p1, p2) < 0);
    }
};

bool getidnum(const TCHAR *p, unsigned long &idnum){
    if(!_istdigit(p[0])) return false;

    errno = 0;
    unsigned long num = _tcstoul(p, NULL, 10);
    if(idnum == ULONG_MAX && errno == ERANGE) return false;
    idnum = num;
    return true;
}

// 登録情報の分析 抽出できた個数を返す
static unsigned int RegistrationInfoAnalyse(vector<TCHAR> &inout, BOOL anmode, HWND hWndMsg){
    unsigned int len = inout.size();
    TCHAR *buf = &inout[0];

    // 登録情報の一時保存データ
    set<tstring, CKeySort> reginfo;

    for(unsigned int i = 0; i < len; i++){
        if(!_istalnum(buf[i]) && buf[i] != '/') buf[i] = _T(' ');
    }

    // 入力文字列からトークン分解して登録可能なキーを抽出 → reginfoに一時保存
    TCHAR *p, *context;
    p = _tcstok_s(buf, _T(" "), &context);
    if(p == NULL) return 0;
    do {
        bool ismatch = false;
        for(unsigned int i = 0; i < ESIZEOF(checkstr) && !ismatch; i++){
            unsigned int prefixlen = _tcslen(checkstr[i]);
            if(_tcslen(p) <= prefixlen) continue;
            TCHAR tbuf[64], *q = p;
            while((q = _tcsstr(q, checkstr[i])) != NULL){
                unsigned long idnum = 0;
                if(getidnum(q + prefixlen, idnum)){
                    _stprintf_s(tbuf, _T("%s%lu"), checkstr[i], idnum);
                    reginfo.insert(tbuf);
                    ismatch = true;
                }
                q++;
            }
        }
        if(!ismatch && anmode){
            unsigned int len = _tcslen(p);
            unsigned int i;
            for(i = 0; i < len; i++){
                if(!_istdigit(p[i])) break;
            }
            if(i == len){
                TCHAR tbuf[64];
                unsigned long idnum = 0;
                if(getidnum(p, idnum)){
                    _stprintf_s(tbuf, _T("%s%lu"), _T(USERID_PREFIX), idnum);
                    reginfo.insert(tbuf);
                }
            }
        }
        if(hWndMsg && reginfo.size() % 1024 == 0){
            TCHAR tbuf[64];
            _stprintf_s(tbuf, _T("解析中: %u件"), reginfo.size());
            SetWindowText(hWndMsg, tbuf);
        }

    } while((p = _tcstok_s(NULL, _T(" "), &context)) != NULL);

    if(hWndMsg){
        TCHAR tbuf[64];
        _stprintf_s(tbuf, _T("解析中: %u件"), reginfo.size());
        SetWindowText(hWndMsg, tbuf);
    }

    int regnum = reginfo.size();

    // reginfo からバッファへ改行区切りで書き戻し
    tstring outbuf;
    int outlen = 0;
    buf[0] = '\0';

    FOREACH(it, reginfo){
        outbuf = *it;
        outbuf += _T("\r\n");
        unsigned int bufsize = outbuf.size()+1;

        if(outlen + bufsize > inout.size()){
            inout.resize(outlen + bufsize);
        }
        memcpy(&inout[outlen], outbuf.c_str(), bufsize*sizeof(TCHAR));
        outlen += (bufsize-1);
    }

    return regnum;
}

// データの登録 入力はRegistrationInfoAnalyse()実行直後のデータである必要がある
// 登録した最低dbidxを返す。データなしor失敗時は0
static unsigned int Registration(TCHAR *buf){
    TCHAR *p, *context;
    bool success = true;
    unsigned int cnt_sucess, cnt_fail, cnt_dup;
    cnt_sucess = cnt_fail = cnt_dup = 0;
    unsigned int mindbidx = 0;

    // トランザクション開始
    if(!nadb.tr_begin()){
        MessageBox(hDlg, _T("データベースの制御に失敗しました。"), PROGRAM_NAME, MB_OK | MB_ICONERROR);
        return 0;
    }
    regdata_info.lock(); regdata_hash.lock();

    // トークン分解してDBへ登録、登録成功数/失敗数/重複数をカウント
    p = _tcstok_s(buf, _T(" \r\n"), &context);
    do {
        if(!p) break;
        for(unsigned int i = 0; i < ESIZEOF(checkstr); i++){
            unsigned int prefixlen = _tcslen(checkstr[i]);
            if(_tcslen(p) <= prefixlen) continue;
            TCHAR tbuf[64];
            if(_tcsncmp(p, checkstr[i], prefixlen) != 0) continue;

            unsigned long idnum = 0;
            if(getidnum(p + prefixlen, idnum)){
                c_regdata rd;

                _stprintf_s(tbuf, _T("%s%lu"), checkstr[i], idnum);
                rd.key = tbuf;
                if(nadb.newregdata(rd)){
                    // idxとkey以外 無効フィールド
                    rd.key_name.clear();
                    rd.last_lv.clear();
                    rd.last_start = 0;
                    rd.notify = ReadOptionInt(OPTION_DEFAULT_NOTIFY, DEF_OPTION_DEFAULT_NOTIFY) | NOTIFY_ENABLE;
                    rd.label = 0;
                    rd.memo.clear();

                    if(!nadb.updateregdata(rd)){
                        cnt_fail++;
                        break;
                    }
                    if(mindbidx == 0) mindbidx = rd.idx;

                    regdata_info[rd.idx] = rd;
                    regdata_hash[rd.key] = rd.idx;

                    _dbg(_T("add: idx = %u, key = %s\n"), rd.idx, rd.key.c_str());
                    cnt_sucess++;
                } else {
                    cnt_dup++;
                }
                break;
            }
        }
    } while((p = _tcstok_s(NULL, _T(" \r\n"), &context)) != NULL);

    regdata_hash.unlock(); regdata_info.unlock();

    // トランザクション終了
    if(!nadb.tr_commit()){
        nadb.tr_rollback();
        MessageBox(hDlg, _T("データベースの制御に失敗しました。"), PROGRAM_NAME, MB_OK | MB_ICONERROR);
        return 0;
    }

    // 登録状況をメッセージ通知
    TCHAR tbuf[64];
    _stprintf_s(tbuf, _T("【登録追加】 成功:%u / 失敗:%u / 重複:%u"), cnt_sucess, cnt_fail, cnt_dup);
    msginfo_ind(tbuf);

    return mindbidx;
}

// 登録データの削除。
// 登録情報、ハッシュデータ、DBレコードを削除する。
static bool UnRegistration(unsigned int dbidx){
    bool ret = false;

    regdata_info.lock(); regdata_hash.lock();

    do {
        ITER(regdata_info) it = regdata_info.find(dbidx);
        if(it == regdata_info.end()) break;
        if(it->second.idx != dbidx) break;
        if(!nadb.deleteregdata(it->second)) break;
        regdata_hash.erase(it->second.key);
        regdata_info.erase(it);
        ret = true;
    } while(0);

    regdata_hash.unlock(); regdata_info.unlock();

    return ret;
}

// エディットボックス フック
// CTRL-Aショートカットキーの処理
static LRESULT CALLBACK EditHookProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg) {

    case WM_KEYDOWN:
        if(wp == 'A' && GetKeyState(VK_CONTROL) < 0){
            PostMessage(hWnd, EM_SETSEL, 0, -1);
        }
        break;

    case WM_CHAR:
        _dbg(_T("WM_CHAR hWnd = 0x%08x, wp = 0x%08x, lp = 0x%08x\n"), hWnd, wp, lp);
        if(wp == 0x01) return 0; // 警告音の回避
        break;
    }

    FARPROC OrgWndProc = (FARPROC)GetWindowLong(hWnd, GWL_USERDATA);
    return CallWindowProc((WNDPROC)OrgWndProc, hWnd, msg, wp, lp);
}

// エディットボックスフック
static void SetEditBoxHook(HWND hWnd){
    WindowSubClass(hWnd, (FARPROC)EditHookProc);
}

// 登録追加ダイアログ
static BOOL CALLBACK RegAddDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    static HWND hToolTipWnd = NULL;
    static HWND hWndEdit = NULL;
    static int iInputLimit;

    switch (msg) {
    case WM_INITDIALOG:
        {
            unsigned int checked = ReadOptionInt(OPTION_RA_ANMODE, DEF_OPTION_RA_ANMODE);
            CheckDlgButton(hDlgWnd, IDC_RA_CHECK, checked);

            hWndEdit = GetDlgItem(hDlgWnd, IDC_RA_INPUT);
            Edit_LimitText(hWndEdit, 0);

            // エディットボックス メッセージフック
            SetEditBoxHook(GetDlgItem(hDlgWnd, IDC_RA_INPUT));

            // 登録ボタンにアイコン追加(Vista以上のみ有効)
            SendMessage(GetDlgItem(hDlgWnd, IDC_RA_REGIST), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_BOOKMARK_PLUS));

            // ツールチップ表示データ登録
            if(ReadOptionInt(OPTION_TOOLTIP_HELP, DEF_OPTION_TOOLTIP_HELP)){
                ToolTipInfo uRegAddDlgIds[] = {
                    { IDC_RA_INPUT,                 _T("登録するコミュニティ番号、チャンネル番号、ユーザーID、放送番号を入力してください。\n入力された内容から登録可能なキーを自動的に抽出するため、不要部分を含んでいてもそのまま貼り付けできます。") },
                    { IDC_RA_ANALYSE,               _T("入力されたテキストを解析し、登録可能なキーを抽出します。") },
                    { IDC_RA_REGIST,                _T("入力されたキーを登録します。") },
                    { IDC_RA_CHECK,                 _T("孤立した数字列をユーザーIDとして認識できるようにします。") },
                };
                hToolTipWnd = ToolTipInit(hDlgWnd, hInst, uRegAddDlgIds, ESIZEOF(uRegAddDlgIds));
            }
        }
        break;

    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IDC_RA_INPUT:
            _dbg(_T("IDC_RA_INPUT 0x%08x,0x%08x\n"), HIWORD(wp), lp);
            // 入力ボックス変更時に文字数表示
            if(HIWORD(wp) == EN_CHANGE){
                TCHAR msg[128];
                int len = Edit_GetTextLength(hWndEdit);
                _stprintf_s(msg, _T("入力済み文字数: %d"), len);
                SetDlgItemText(hDlgWnd, IDC_RA_MSG, msg);
            }
            break;

        case IDC_RA_REGIST:
        case IDC_RA_ANALYSE:
            {
                // 解析/登録ボタン押下時
                int len = Edit_GetTextLength(hWndEdit);
                if(len < 1) break;
                int bufsize = len + 1;

                vector<TCHAR> buf(bufsize);
                GetDlgItemText(hDlgWnd, IDC_RA_INPUT, &buf[0], bufsize);

                unsigned int num = RegistrationInfoAnalyse(buf, IsDlgButtonChecked(hDlgWnd, IDC_RA_CHECK), GetDlgItem(hDlgWnd, IDC_RA_MSG));

                if(num > 0){
                    if(LOWORD(wp) == IDC_RA_REGIST){
                        // 登録ボタンだったら登録処理してクローズ
                        unsigned int mindbidx = Registration(&buf[0]);
                        EndDialog(hDlgWnd, mindbidx);
                    } else {
                        TCHAR msg[128];
                        SetDlgItemText(hDlgWnd, IDC_RA_INPUT, _T("反映中"));

                        // 解析ボタンだったら解析結果を表示して継続
                        SetDlgItemText(hDlgWnd, IDC_RA_INPUT, &buf[0]);
                        _stprintf_s(msg, _T("%u 件のデータを検出しました。"), num);
                        SetDlgItemText(hDlgWnd, IDC_RA_MSG, msg);
                    }
                }
            }
            break;

        case IDC_RA_CHECK:
            {
                // 孤立数字列をユーザIDとして認識するチェックボックス
                unsigned int checked = IsDlgButtonChecked(hDlgWnd, IDC_RA_CHECK);
                SaveOptionInt(OPTION_RA_ANMODE, checked);
            }
            break;

        default:
            return FALSE;
        }
        break;

    case WM_SYSCOMMAND:
        if(wp == SC_CLOSE){
            // ×ボタン(破棄して閉じる)
            EndDialog(hDlgWnd, 0);
            break;
        }

        return FALSE;
        break;


    case WM_DESTROY:
        if(hToolTipWnd) DestroyWindow(hToolTipWnd);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

// チェックボックスの動作および初期値を設定
// uMode = 1 : 選択範囲が全て非チェック、2StateボックスでチェックOFF
// uMode = 2 : 選択範囲が全てチェック済み、2StateボックスでチェックON
// uMode = 3 : チェック状態混合、3StateボックスでINDETERMINATE設定
static void SetDlgCheckbox(HWND hDlgWnd, UINT uDlgID, UINT uMode){
    if(uMode < 1 || uMode > 3) return;
    HWND hButtonWnd = GetDlgItem(hDlgWnd, uDlgID);
    LONG dwStyle = GetWindowLong(hButtonWnd, GWL_STYLE) & ~BS_TYPEMASK;
    UINT uCheck = BST_UNCHECKED;

    switch(uMode){
    case 1: dwStyle |= BS_AUTOCHECKBOX; uCheck = BST_UNCHECKED;     break;
    case 2: dwStyle |= BS_AUTOCHECKBOX; uCheck = BST_CHECKED;       break;
    case 3: dwStyle |= BS_AUTO3STATE;   uCheck = BST_INDETERMINATE; break;
    }

    SetWindowLong(hButtonWnd, GWL_STYLE, dwStyle);
    CheckDlgButton(hDlgWnd, uDlgID, uCheck);
}


// アイテム設定ダイアログ
static BOOL CALLBACK RegSetDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    // 呼び出し元から送られてくる選択アイテムのdbidxのリストのポインタを保持
    static vector <unsigned int> *p_idx;
    // 登録名、メモの変更ボタン押下状態(一度でも押されればtrue)
    static bool bmemo, bkeyname;
    // ツールチップヘルプ
    static HWND hToolTipWnd = NULL;

    switch (msg) {
    case WM_INITDIALOG:
        {
            // 呼び出し元から送られてくる選択アイテムのdbidxのリスト
            p_idx = (__typeof__(p_idx))lp;
            if(p_idx->size() == 0) break;

#ifdef _DEBUG
            FOREACH(it, *p_idx){
                _dbg(_T("dbidx = %d\n"), *it);
            }
#endif
            // 選択アイテム数を表示
            TCHAR buf[256];
            _stprintf_s(buf, _T("%d 件のアイテムが選択されています。"), p_idx->size());
            SetDlgItemText(hDlgWnd, IDC_RS_MSG, buf);

            // 選択アイテムが1個だけの場合と複数の場合で、
            // 設定可能項目が異なるので分岐
            if(p_idx->size() == 1){
                // 1個だけの場合変更ボタンを無効化し、変わりに取得ボタンを有効化
                // ただし、ユーザIDの場合は取得できないので、無効化のまま。
                _stprintf_s(buf, _T(""));

                regdata_info.lock();
                ITER(regdata_info) it = regdata_info.find(p_idx->front());
                if(it != regdata_info.end()){
                    c_regdata *rd = &(it->second);

                    SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEY,     rd->key.c_str());
                    SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEYNAME, rd->key_name.c_str());
                    SetDlgItemText(hDlgWnd, IDC_RS_EDIT_MEMO,    rd->memo.c_str());

                    if(rd->key.substr(0,USERID_PREFIX_LEN) != _T(USERID_PREFIX)){
                        ShowWindow(GetDlgItem(hDlgWnd, IDC_RS_CB_KEYNAME), SW_HIDE);
                    } else {
                        ShowWindow(GetDlgItem(hDlgWnd, IDC_RS_KEYNAME_ACQ), SW_HIDE);
                        EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_CB_KEYNAME), FALSE);
                    }
                    EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_CB_MEMO),    FALSE);

                    if(rd->notify & NOTIFY_BALLOON){
                        CheckDlgButton(hDlgWnd, IDC_RS_CHECK_BALLOON,   TRUE);
                    }
                    if(rd->notify & NOTIFY_BROWSER){
                        CheckDlgButton(hDlgWnd, IDC_RS_CHECK_BROWSER,   TRUE);
                    }
                    if(rd->notify & NOTIFY_SOUND){
                        CheckDlgButton(hDlgWnd, IDC_RS_CHECK_SOUND,     TRUE);
                    }
                    if(rd->notify & NOTIFY_EXTAPP){
                        CheckDlgButton(hDlgWnd, IDC_RS_CHECK_EXTAPP,    TRUE);
                    }
                }
                regdata_info.unlock();

            } else {
                // 複数選択の場合、入力ボックスをデフォルト無効化し、
                // 変更ボタン押下により入力可能とする。
                ShowWindow(GetDlgItem(hDlgWnd, IDC_RS_KEYNAME_ACQ), SW_HIDE);
                SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEY,     _T("<複数選択>"));
                SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEYNAME, _T("<複数選択>"));
                SetDlgItemText(hDlgWnd, IDC_RS_EDIT_MEMO,    _T("<複数選択>"));
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_EDIT_KEYNAME), FALSE);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_EDIT_MEMO),    FALSE);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_CB_KEYNAME),   TRUE);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_CB_MEMO),      TRUE);
            }

            // 通知チェックボックスの現状態の反映(共通)
            int ntf[4];
            for(int i = 0; i < ESIZEOF(ntf); i++){ ntf[i] = 0; }
            regdata_info.lock();
            FOREACH(it, *p_idx){
                unsigned int dbidx = *it;
                ITER(regdata_info) dbit = regdata_info.find(dbidx);
                if(dbit == regdata_info.end()) continue;

                unsigned int notify = dbit->second.notify;
                ntf[0] |= (notify & NOTIFY_BALLOON) ? 2 : 1;
                ntf[1] |= (notify & NOTIFY_BROWSER) ? 2 : 1;
                ntf[2] |= (notify & NOTIFY_SOUND  ) ? 2 : 1;
                ntf[3] |= (notify & NOTIFY_EXTAPP ) ? 2 : 1;
            }
            regdata_info.unlock();

            SetDlgCheckbox(hDlgWnd, IDC_RS_CHECK_BALLOON, ntf[0]);
            SetDlgCheckbox(hDlgWnd, IDC_RS_CHECK_BROWSER, ntf[1]);
            SetDlgCheckbox(hDlgWnd, IDC_RS_CHECK_SOUND,   ntf[2]);
            SetDlgCheckbox(hDlgWnd, IDC_RS_CHECK_EXTAPP,  ntf[3]);

            // OKボタンにアイコン追加(Vista以上のみ有効)
            SendMessage(GetDlgItem(hDlgWnd, IDC_RS_SAVE), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_TICK));

            // ツールチップデータ登録
            if(ReadOptionInt(OPTION_TOOLTIP_HELP, DEF_OPTION_TOOLTIP_HELP)){
                ToolTipInfo uRegSetDlgIds[] = {
                    { IDC_RS_EDIT_KEYNAME,          _T("登録名を変更できます。複数のアイテムが選択されている場合、\n全てのアイテムに適用されます。") },
                    { IDC_RS_CB_KEYNAME,            _T("登録名を変更します。無効状態の場合、変更は適用されません。") },
                    { IDC_RS_KEYNAME_ACQ,           _T("コミュニティ名、チャンネル名、放送番組名を取得します。") },
                    { IDC_RS_EDIT_MEMO,             _T("メモ内容を変更できます。複数のアイテムが選択されている場合、\n全てのアイテムに適用されます。") },
                    { IDC_RS_CB_MEMO,               _T("メモ内容を変更します。無効状態の場合、変更は適用されません。") },
                    { IDC_RS_CHECK_BALLOON,         _T("放送開始時にバルーンを表示します。") },
                    { IDC_RS_CHECK_BROWSER,         _T("放送開始時にブラウザで放送ページを開きます。") },
                    { IDC_RS_CHECK_SOUND,           _T("放送開始時にサウンドファイルを再生します。") },
                    { IDC_RS_CHECK_EXTAPP,          _T("放送開始時に外部アプリケーションを起動します。") },
                    { IDC_RS_SAVE,                  _T("変更を保存して閉じます。") },
                };
                hToolTipWnd = ToolTipInit(hDlgWnd, hInst, uRegSetDlgIds, ESIZEOF(uRegSetDlgIds));
            }

            // エディットボックスのCtrl-A全選択対応
            SetEditBoxHook(GetDlgItem(hDlgWnd, IDC_RS_EDIT_KEYNAME));
            SetEditBoxHook(GetDlgItem(hDlgWnd, IDC_RS_EDIT_MEMO));

            bmemo = bkeyname = false;
        }
        break;

    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IDC_RS_CB_KEYNAME:
            if(HIWORD(wp) == BN_CLICKED){
                // 登録名変更ボタン押下によりエディットボックス有効無効切り替え
                UINT bCheck = IsDlgButtonChecked(hDlgWnd, IDC_RS_CB_KEYNAME);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_EDIT_KEYNAME), bCheck);
                if(bCheck == BST_CHECKED && !bkeyname){
                    SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEYNAME, _T(""));
                    bkeyname = true;
                }
            }
            break;

        case IDC_RS_CB_MEMO:
            if(HIWORD(wp) == BN_CLICKED){
                // メモ変更ボタン押下によりエディットボックス有効無効切り替え
                UINT bCheck = IsDlgButtonChecked(hDlgWnd, IDC_RS_CB_MEMO);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_EDIT_MEMO), bCheck);
                if(bCheck == BST_CHECKED && !bmemo){
                    SetDlgItemText(hDlgWnd, IDC_RS_EDIT_MEMO, _T(""));
                    bmemo = true;
                }
            }
            break;

        case IDC_RS_KEYNAME_ACQ:
            {
                // 取得ボタン押下(1件選択の場合のみ有効)
                TCHAR key_str[16];
                EnableWindow(GetDlgItem(hDlgWnd, IDC_RS_KEYNAME_ACQ), FALSE);
                SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEYNAME, _T("<取得中>"));
                InvalidateRect(GetDlgItem(hDlgWnd, IDC_RS_EDIT_KEYNAME), NULL, FALSE);
                UpdateWindow(GetDlgItem(hDlgWnd, IDC_RS_EDIT_KEYNAME));

                // キー名取得要求
                GetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEY, key_str, ESIZEOF(key_str));
                tstring key = key_str;
                tstring keyname;
                if(nicoalert_getkeyname(key, keyname) != CMM_SUCCESS){
                    keyname = _T("<取得失敗>");
                }
                SetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEYNAME, keyname.c_str());
            }
            break;

        case IDC_RS_SAVE:
            {
                // 保存ボタン押下
                UINT bBtnBalloon = IsDlgButtonChecked(hDlgWnd, IDC_RS_CHECK_BALLOON);
                UINT bBtnBrowser = IsDlgButtonChecked(hDlgWnd, IDC_RS_CHECK_BROWSER);
                UINT bBtnSound   = IsDlgButtonChecked(hDlgWnd, IDC_RS_CHECK_SOUND);
                UINT bBtnExtApp  = IsDlgButtonChecked(hDlgWnd, IDC_RS_CHECK_EXTAPP);
                unsigned int notify_or = 0, notify_and = ~0;
                if(bBtnBalloon == BST_CHECKED)   notify_or  |=  NOTIFY_BALLOON;
                if(bBtnBalloon == BST_UNCHECKED) notify_and &= ~NOTIFY_BALLOON;
                if(bBtnBrowser == BST_CHECKED)   notify_or  |=  NOTIFY_BROWSER;
                if(bBtnBrowser == BST_UNCHECKED) notify_and &= ~NOTIFY_BROWSER;
                if(bBtnSound   == BST_CHECKED)   notify_or  |=  NOTIFY_SOUND;
                if(bBtnSound   == BST_UNCHECKED) notify_and &= ~NOTIFY_SOUND;
                if(bBtnExtApp  == BST_CHECKED)   notify_or  |=  NOTIFY_EXTAPP;
                if(bBtnExtApp  == BST_UNCHECKED) notify_and &= ~NOTIFY_EXTAPP;

                UINT bBtnKeyName = IsDlgButtonChecked(hDlgWnd, IDC_RS_CB_KEYNAME);
                UINT bBtnMemo    = IsDlgButtonChecked(hDlgWnd, IDC_RS_CB_MEMO);
                if(p_idx->size() == 1) bBtnKeyName = bBtnMemo = TRUE;

                TCHAR strKeyName[REGDATA_KEYNAME_MAXLEN + 8];
                TCHAR strMemo[REGDATA_MEMO_MAXLEN + 8];
                GetDlgItemText(hDlgWnd, IDC_RS_EDIT_KEYNAME, strKeyName, ESIZEOF(strKeyName));
                GetDlgItemText(hDlgWnd, IDC_RS_EDIT_MEMO,    strMemo,    ESIZEOF(strMemo));
                // 64文字以上の場合に切断して...を付加
                if(_tcslen(strKeyName) > REGDATA_KEYNAME_MAXLEN){
                    strKeyName[REGDATA_KEYNAME_MAXLEN-3] = '.';
                    strKeyName[REGDATA_KEYNAME_MAXLEN-2] = '.';
                    strKeyName[REGDATA_KEYNAME_MAXLEN-1] = '.';
                    strKeyName[REGDATA_KEYNAME_MAXLEN-0] = '\0';
                }
                // 64文字以上の場合に切断して...を付加
                if(_tcslen(strMemo) > REGDATA_MEMO_MAXLEN){
                    strMemo[REGDATA_MEMO_MAXLEN-3] = '.';
                    strMemo[REGDATA_MEMO_MAXLEN-2] = '.';
                    strMemo[REGDATA_MEMO_MAXLEN-1] = '.';
                    strMemo[REGDATA_MEMO_MAXLEN-0] = '\0';
                }

                // メモリ上のデータに変更を反映(DBへの反映はメイン側)
                regdata_info.lock();
                FOREACH(it, *p_idx){
                    unsigned int dbidx = *it;
                    ITER(regdata_info) dbit = regdata_info.find(dbidx);
                    if(dbit == regdata_info.end()) continue;
                    if(bBtnKeyName) dbit->second.key_name = strKeyName;
                    if(bBtnMemo)    dbit->second.memo     = strMemo;
                    dbit->second.notify   = dbit->second.notify & notify_and | notify_or;
                }
                regdata_info.unlock();

                // 変更ありを通知してダイアログ終了
                EndDialog(hDlgWnd, TRUE);
            }
            break;

        default:
            return FALSE;
        }
        break;

    case WM_SYSCOMMAND:
        if (wp == SC_CLOSE){
            // 変更なしを通知してダイアログ終了
            EndDialog(hDlgWnd, FALSE);
            break;
        }

        return FALSE;
        break;


    case WM_DESTROY:
        if(hToolTipWnd) DestroyWindow(hToolTipWnd);
        break;

    default:
        return FALSE;
    }
    return TRUE;
}


// 全体設定ダイアログ
static BOOL CALLBACK SettingDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    static unsigned int color_lsort_tx, color_lsort_bg;
    static unsigned int color_start_tx, color_start_bg;
    static HBRUSH hBrushColor[4];
    static DWORD dwCustColors[16] = {
        DEF_OPTION_LSORT_BGCOLOR, DEF_OPTION_START_BGCOLOR,
        0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF,
        0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF,
    };
    static HWND hToolTipWnd = NULL;

    switch (msg) {
    case WM_INITDIALOG:
        {
            // 各設定をダイアログアイテムに反映
            CheckDlgButton(hDlgWnd, IDC_SE_MINIMIZE_ON_CLOSE, ReadOptionInt(OPTION_MINIMIZE_ON_CLOSE, DEF_OPTION_MINIMIZE_ON_CLOSE));
            CheckDlgButton(hDlgWnd, IDC_SE_CONFIRM_ON_CLOSE,  ReadOptionInt(OPTION_CONFIRM_ON_CLOSE,  DEF_OPTION_CONFIRM_ON_CLOSE));
            CheckDlgButton(hDlgWnd, IDC_SE_MINIMIZE_TO_TRAY,  ReadOptionInt(OPTION_MINIMIZE_TO_TRAY,  DEF_OPTION_MINIMIZE_TO_TRAY));
            CheckDlgButton(hDlgWnd, IDC_SE_MINIMIZE_ON_BOOT,  ReadOptionInt(OPTION_MINIMIZE_ON_BOOT,  DEF_OPTION_MINIMIZE_ON_BOOT));
            CheckDlgButton(hDlgWnd, IDC_SE_BALLOON_OPEN,      ReadOptionInt(OPTION_BALLOON_OPEN,      DEF_OPTION_BALLOON_OPEN));
            CheckDlgButton(hDlgWnd, IDC_SE_BALLOON_BOUYOMI,   ReadOptionInt(OPTION_BALLOON_BOUYOMI,   DEF_OPTION_BALLOON_BOUYOMI));
            CheckDlgButton(hDlgWnd, IDC_SE_AUTO_KEYNAME_ACQ,  ReadOptionInt(OPTION_AUTO_KEYNAME_ACQ,  DEF_OPTION_AUTO_KEYNAME_ACQ));
            CheckDlgButton(hDlgWnd, IDC_SE_TOOLTIP_HELP,      ReadOptionInt(OPTION_TOOLTIP_HELP,      DEF_OPTION_TOOLTIP_HELP));
            CheckDlgButton(hDlgWnd, IDC_SE_ITEM_DC_ENABLE,    ReadOptionInt(OPTION_ITEM_DC_ENABLE,    DEF_OPTION_ITEM_DC_ENABLE));

            int item_dc_select = ReadOptionInt(OPTION_ITEM_DC_SELECT, DEF_OPTION_ITEM_DC_SELECT);
            CheckDlgButton(hDlgWnd, IDC_SE_ITEM_DC_ITEMSETTING, item_dc_select == 0);
            CheckDlgButton(hDlgWnd, IDC_SE_ITEM_DC_OPEN_LV,     item_dc_select == 1);
            CheckDlgButton(hDlgWnd, IDC_SE_ITEM_DC_OPEN_ID,     item_dc_select == 2);

            unsigned int notify = ReadOptionInt(OPTION_DEFAULT_NOTIFY, DEF_OPTION_DEFAULT_NOTIFY);
            CheckDlgButton(hDlgWnd, IDC_SE_CHECK_BALLOON, (notify & NOTIFY_BALLOON)?TRUE:FALSE);
            CheckDlgButton(hDlgWnd, IDC_SE_CHECK_BROWSER, (notify & NOTIFY_BROWSER)?TRUE:FALSE);
            CheckDlgButton(hDlgWnd, IDC_SE_CHECK_SOUND,   (notify & NOTIFY_SOUND  )?TRUE:FALSE);
            CheckDlgButton(hDlgWnd, IDC_SE_CHECK_EXTAPP,  (notify & NOTIFY_EXTAPP )?TRUE:FALSE);

            unsigned int defaultbrowser = ReadOptionInt(OPTION_DEFAULT_BROWSER_USE, DEF_OPTION_DEFAULT_BROWSER_USE);
            CheckDlgButton(hDlgWnd, IDC_SE_DEFAULT_BROWSER_USE, defaultbrowser);

            tstring browser_path = ReadOptionString(OPTION_BROWSER_PATH, _T(""));
            SetDlgItemText(hDlgWnd, IDC_SE_BROWSER_PATH, browser_path.c_str());
            tstring sound_path = ReadOptionString(OPTION_SOUND_PATH, DEF_OPTION_SOUND_PATH);
            SetDlgItemText(hDlgWnd, IDC_SE_SOUND_PATH, sound_path.c_str());
            tstring extapp_path = ReadOptionString(OPTION_EXTAPP_PATH, _T(""));
            SetDlgItemText(hDlgWnd, IDC_SE_EXTAPP_PATH, extapp_path.c_str());

            HWND hWndTB = GetDlgItem(hDlgWnd, IDC_SE_SOUND_VOL);
            SendMessage(hWndTB, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
            SendMessage(hWndTB, TBM_SETPOS, TRUE, ReadOptionInt(OPTION_SOUND_VOL, DEF_OPTION_SOUND_VOL));
            SendMessage(GetDlgItem(hDlgWnd, IDC_SE_SOUND_PLAY), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_PLAY));

            color_lsort_tx = ReadOptionInt(OPTION_LSORT_TXCOLOR, DEF_OPTION_LSORT_TXCOLOR);
            color_lsort_bg = ReadOptionInt(OPTION_LSORT_BGCOLOR, DEF_OPTION_LSORT_BGCOLOR);
            color_start_tx = ReadOptionInt(OPTION_START_TXCOLOR, DEF_OPTION_START_TXCOLOR);
            color_start_bg = ReadOptionInt(OPTION_START_BGCOLOR, DEF_OPTION_START_BGCOLOR);
            hBrushColor[0] = CreateSolidBrush(color_lsort_tx);
            hBrushColor[1] = CreateSolidBrush(color_lsort_bg);
            hBrushColor[2] = CreateSolidBrush(color_start_tx);
            hBrushColor[3] = CreateSolidBrush(color_start_bg);

            // OKボタンにアイコン追加(Vista以上のみ有効)
            SendMessage(GetDlgItem(hDlgWnd, IDC_SE_SAVE), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_TICK));
            SendMessage(GetDlgItem(hDlgWnd, IDC_SE_CANCEL), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_CROSS));

            PostMessage(hDlgWnd, WM_COMMAND, IDC_SE_DEFAULT_BROWSER_USE, 0);
            PostMessage(hDlgWnd, WM_COMMAND, IDC_SE_ITEM_DC_ENABLE, 0);

            // ツールチップデータ登録
            if(ReadOptionInt(OPTION_TOOLTIP_HELP, DEF_OPTION_TOOLTIP_HELP)){
                ToolTipInfo uSettingDlgIds[] = {
                    { IDC_SE_MINIMIZE_ON_CLOSE,     _T("ウィンドウが閉じられる場合、終了せず最小化します。") },
                    { IDC_SE_CONFIRM_ON_CLOSE,      _T("プログラムを終了するとき確認します。") },
                    { IDC_SE_MINIMIZE_TO_TRAY,      _T("最小化したとき、タスクバーに表示しません。") },
                    { IDC_SE_MINIMIZE_ON_BOOT,      _T("プログラムを最小化された状態で起動します。") },
                    { IDC_SE_BALLOON_OPEN,          _T("放送開始通知のバルーン、またはバルーン表示中にトレイアイコンをクリックすると、\n放送ページがブラウザで開かれます。") },
                    { IDC_SE_BALLOON_BOUYOMI,       _T("バルーン通知設定がされた放送開始時に、音声読み上げを行います。\n(棒読みちゃんが起動されている必要があります)") },
                    { IDC_SE_AUTO_KEYNAME_ACQ,      _T("コミュニティ、チャンネル、放送番号の登録時に、登録名を自動的に取得します。\n既に手動で入力されていた場合は取得しません。") },
                    { IDC_SE_TOOLTIP_HELP,          _T("チェックボックス、ボタン等にマウスカーソルを合わせた際に説明を表示します。") },
                    { IDC_SE_ITEM_DC_ENABLE,        _T("登録アイテムをダブルクリックした際の動作を設定します。") },
                    { IDC_SE_CHECK_BALLOON,         _T("放送開始時にバルーンを表示します。") },
                    { IDC_SE_CHECK_BROWSER,         _T("放送開始時にブラウザで放送ページを開きます。") },
                    { IDC_SE_CHECK_SOUND,           _T("放送開始時にサウンドファイルを再生します。") },
                    { IDC_SE_CHECK_EXTAPP,          _T("放送開始時に外部アプリケーションを起動します。") },
                    { IDC_SE_DEFAULT_BROWSER_USE,   _T("標準として設定されているブラウザでWEBページを開きます。") },
                    { IDC_SE_BROWSER_PATH,          _T("ブラウザの実行ファイルを指定します。\nパラメータとして開くWEBページのURLが渡されます。") },
                    { IDC_SE_SOUND_PATH,            _T("音声ファイルを指定します。") },
                    //{ IDC_SE_SOUND_VOL,             LPSTR_TEXTCALLBACK },
                    { IDC_SE_SOUND_PLAY,            _T("テスト再生") },
                    { IDC_SE_EXTAPP_PATH,           _T("外部アプリケーションの実行ファイルを指定します。\nパラメータとして、lvから始まる放送番号が渡されます。") },
                    { IDC_SE_CANCEL,                _T("変更を破棄して閉じます。") },
                    { IDC_SE_SAVE,                  _T("変更を保存して閉じます。") },
                };
                hToolTipWnd = ToolTipInit(hDlgWnd, hInst, uSettingDlgIds, ESIZEOF(uSettingDlgIds));
            }

            // エディットボックスのCtrl-A全選択対応
            SetEditBoxHook(GetDlgItem(hDlgWnd, IDC_SE_BROWSER_PATH));
            SetEditBoxHook(GetDlgItem(hDlgWnd, IDC_SE_SOUND_PATH));
            SetEditBoxHook(GetDlgItem(hDlgWnd, IDC_SE_EXTAPP_PATH));
        }
        break;

    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IDC_SE_SAVE:
            {
                // 保存ボタン押下

                // 各設定を保存
                SaveOptionInt(OPTION_MINIMIZE_ON_CLOSE, IsDlgButtonChecked(hDlgWnd, IDC_SE_MINIMIZE_ON_CLOSE));
                SaveOptionInt(OPTION_CONFIRM_ON_CLOSE,  IsDlgButtonChecked(hDlgWnd, IDC_SE_CONFIRM_ON_CLOSE));
                SaveOptionInt(OPTION_MINIMIZE_TO_TRAY,  IsDlgButtonChecked(hDlgWnd, IDC_SE_MINIMIZE_TO_TRAY));
                SaveOptionInt(OPTION_MINIMIZE_ON_BOOT,  IsDlgButtonChecked(hDlgWnd, IDC_SE_MINIMIZE_ON_BOOT));
                SaveOptionInt(OPTION_BALLOON_OPEN,      IsDlgButtonChecked(hDlgWnd, IDC_SE_BALLOON_OPEN));
                SaveOptionInt(OPTION_BALLOON_BOUYOMI,   IsDlgButtonChecked(hDlgWnd, IDC_SE_BALLOON_BOUYOMI));
                SaveOptionInt(OPTION_AUTO_KEYNAME_ACQ,  IsDlgButtonChecked(hDlgWnd, IDC_SE_AUTO_KEYNAME_ACQ));
                SaveOptionInt(OPTION_TOOLTIP_HELP,      IsDlgButtonChecked(hDlgWnd, IDC_SE_TOOLTIP_HELP));
                SaveOptionInt(OPTION_ITEM_DC_ENABLE,    IsDlgButtonChecked(hDlgWnd, IDC_SE_ITEM_DC_ENABLE));

                int item_dc_select =
                    IsDlgButtonChecked(hDlgWnd, IDC_SE_ITEM_DC_OPEN_LV) ? 1 :
                    IsDlgButtonChecked(hDlgWnd, IDC_SE_ITEM_DC_OPEN_ID) ? 2 : 0;
                SaveOptionInt(OPTION_ITEM_DC_SELECT, item_dc_select);

                SaveOptionInt(OPTION_LSORT_TXCOLOR, color_lsort_tx);
                SaveOptionInt(OPTION_LSORT_BGCOLOR, color_lsort_bg);
                SaveOptionInt(OPTION_START_TXCOLOR, color_start_tx);
                SaveOptionInt(OPTION_START_BGCOLOR, color_start_bg);

                unsigned int notify = (
                    ((IsDlgButtonChecked(hDlgWnd, IDC_SE_CHECK_BALLOON))?NOTIFY_BALLOON:0) |
                    ((IsDlgButtonChecked(hDlgWnd, IDC_SE_CHECK_BROWSER))?NOTIFY_BROWSER:0) |
                    ((IsDlgButtonChecked(hDlgWnd, IDC_SE_CHECK_SOUND))  ?NOTIFY_SOUND:0) |
                    ((IsDlgButtonChecked(hDlgWnd, IDC_SE_CHECK_EXTAPP)) ?NOTIFY_EXTAPP:0)
                    );
                SaveOptionInt(OPTION_DEFAULT_NOTIFY, notify);
                SaveOptionInt(OPTION_DEFAULT_BROWSER_USE, IsDlgButtonChecked(hDlgWnd, IDC_SE_DEFAULT_BROWSER_USE));

                TCHAR path[_MAX_PATH + 1];
                if(GetDlgItemText(hDlgWnd, IDC_SE_BROWSER_PATH, path, sizeof(path)) >= 0){
                    SaveOptionString(OPTION_BROWSER_PATH, path);
                }
                if(GetDlgItemText(hDlgWnd, IDC_SE_SOUND_PATH, path, sizeof(path)) >= 0){
                    SaveOptionString(OPTION_SOUND_PATH, path);
                }
                if(GetDlgItemText(hDlgWnd, IDC_SE_EXTAPP_PATH, path, sizeof(path)) >= 0){
                    SaveOptionString(OPTION_EXTAPP_PATH, path);
                }

                int vol = SendMessage(GetDlgItem(hDlgWnd, IDC_SE_SOUND_VOL), TBM_GETPOS, 0, 0);
                SaveOptionInt(OPTION_SOUND_VOL, vol);

            }
            EndDialog(hDlgWnd, TRUE);
            break;

        case IDC_SE_DEFAULT_BROWSER_USE:
            {
                // 標準のブラウザで起動する チェックボタン
                // OFFの場合に入力ボックスを参照ボタンを有効化
                BOOL fCheck = IsDlgButtonChecked(hDlgWnd, IDC_SE_DEFAULT_BROWSER_USE);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_SE_BROWSER_PATH), !fCheck);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_SE_BROWSER_REF), !fCheck);
            }
            break;

        case IDC_SE_ITEM_DC_ENABLE:
            {
                BOOL fCheck = IsDlgButtonChecked(hDlgWnd, IDC_SE_ITEM_DC_ENABLE);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_SE_ITEM_DC_ITEMSETTING), fCheck);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_SE_ITEM_DC_OPEN_LV), fCheck);
                EnableWindow(GetDlgItem(hDlgWnd, IDC_SE_ITEM_DC_OPEN_ID), fCheck);
            }
            break;

        case IDC_SE_CANCEL:
            {
                // キャンセルボタン押下
                PostMessage(hDlgWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            }
            break;

        case IDC_SE_SORT_COLOR_TX:
        case IDC_SE_SORT_COLOR_BG:
        case IDC_SE_BC_COLOR_TX:
        case IDC_SE_BC_COLOR_BG:
            {
                // 色設定ボタン押下
                unsigned int *color = NULL;
                unsigned int idx;
                WORD dlgid = LOWORD(wp);
                if(HIWORD(wp) != STN_CLICKED) break;

                // 押されたボタンを特定、および反映させる変数を決定
                switch(dlgid){
                case IDC_SE_SORT_COLOR_TX:  idx = 0; color = &color_lsort_tx; break;
                case IDC_SE_SORT_COLOR_BG:  idx = 1; color = &color_lsort_bg; break;
                case IDC_SE_BC_COLOR_TX:    idx = 2; color = &color_start_tx; break;
                case IDC_SE_BC_COLOR_BG:    idx = 3; color = &color_start_bg; break;
                default: return FALSE;
                }

                // 色選択コモンダイアログ起動
                CHOOSECOLOR cc;
                cc.lStructSize      = sizeof(cc);
                cc.hwndOwner        = hDlgWnd;
                cc.hInstance        = NULL;
                cc.rgbResult        = *color;
                cc.lpCustColors     = dwCustColors;
                cc.Flags            = CC_RGBINIT | CC_FULLOPEN;
                cc.lCustData        = 0;
                cc.lpfnHook         = NULL;
                cc.lpTemplateName   = NULL;
                if(ChooseColor(&cc)){
                    // OKボタン押下の場合
                    *color = cc.rgbResult;
                    // 同一の色が登録されていない場合、カスタムカラーテーブルに格納
                    int i;
                    for(i = 0; i < ESIZEOF(dwCustColors); i++){
                        if(dwCustColors[i] == *color) break;
                    }
                    if(i == ESIZEOF(dwCustColors)){
                        memmove(&dwCustColors[1], &dwCustColors[0], sizeof(dwCustColors[0]) * (ESIZEOF(dwCustColors)-1));
                        dwCustColors[0] = *color;
                    }

                    if(hBrushColor[idx]) DeleteObject((HGDIOBJ)hBrushColor[idx]);
                    hBrushColor[idx] = CreateSolidBrush(*color);
                    InvalidateRect(GetDlgItem(hDlgWnd, dlgid), NULL, FALSE);
                }
            }
            break;

        case IDC_SE_BROWSER_REF:
        case IDC_SE_SOUND_REF:
        case IDC_SE_EXTAPP_REF:
            {
                // [参照]ボタン押下
                TCHAR szFileName[_MAX_PATH + 1];
                WORD dlgid = 0;
                unsigned int filter;
                TCHAR *filterstr[] = {
                    _T("実行ファイル (*.exe)\0*.exe\0全てのファイル(*.*)\0*.*\0"),
                    _T("サウンドファイル\0*.wav;*.mp3;*.m4a;\0全てのファイル(*.*)\0*.*\0")
                };

                // 押されたボタンを特定、ファイル選択フィルタを決定
                if(LOWORD(wp) == IDC_SE_BROWSER_REF){
                    dlgid = IDC_SE_BROWSER_PATH;    filter = 0;
                }
                if(LOWORD(wp) == IDC_SE_SOUND_REF){
                    dlgid = IDC_SE_SOUND_PATH;      filter = 1;
                }
                if(LOWORD(wp) == IDC_SE_EXTAPP_REF){
                    dlgid = IDC_SE_EXTAPP_PATH;     filter = 0;
                }
                if(!dlgid) break;

                // ファイル選択コモンダイアログ起動
                static OPENFILENAME ofn;
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = hDlgWnd;
                ofn.lpstrFilter = filterstr[filter];
                ofn.nFilterIndex = 1;
                ofn.lpstrFile = szFileName;
                ofn.nMaxFile = sizeof(szFileName);
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOVALIDATE | OFN_NOCHANGEDIR;
                GetDlgItemText(hDlgWnd, dlgid, szFileName, sizeof(szFileName));

                if(GetOpenFileName(&ofn)){
                    SetDlgItemText(hDlgWnd, dlgid, szFileName);
                } else {
                    _dbg(_T("CommDlgExtendedError = %d\n"), CommDlgExtendedError());
                }
            }
            break;

        case IDC_SE_SOUND_PLAY:
            {
                // サウンドテスト再生ボタン
                TCHAR szFileName[_MAX_PATH + 1];
                // サウンドファイル名取得
                GetDlgItemText(hDlgWnd, IDC_SE_SOUND_PATH, szFileName, sizeof(szFileName));
                // ボリューム値取得
                HWND hWndTB = GetDlgItem(hDlgWnd, IDC_SE_SOUND_VOL);
                int vol = SendMessage(hWndTB, TBM_GETPOS, 0, 0);
                // テスト再生要求
                bcc_soundtest(szFileName, vol);
            }
            break;

        default:
            return FALSE;
        }
        break;

#if 0
    case WM_NOTIFY:
        {
            NMHDR *pnmhdr = (NMHDR *)lp;
            if(pnmhdr->hwndFrom == hToolTipWnd){
                if(pnmhdr->code == TTN_GETDISPINFOW){
                    LPNMTTDISPINFO lpnmtdi = (LPNMTTDISPINFO)lp;
                    HWND hWndTB = GetDlgItem(hDlgWnd, IDC_SE_SOUND_VOL);
                    int vol = SendMessage(hWndTB, TBM_GETPOS, 0, 0);
                    _sntprintf_s(lpnmtdi->szText, ESIZEOF(lpnmtdi->szText), _T("音量: %d"), vol);
                }
            }
        }
        break;
#endif

    case WM_HSCROLL:
        {
            // 音量トラックコントロール
            HWND hWndTB = (HWND)lp;
            if(hWndTB == GetDlgItem(hDlgWnd, IDC_SE_SOUND_VOL)){
                _dbg(_T("WM_HSCROLL : wp = %x, lp = %x\n"), wp, lp);
                if(LOWORD(wp) == TB_ENDTRACK){
                    // 音量決定
                    int vol = SendMessage(hWndTB, TBM_GETPOS, 0, 0);
                    bcc_soundvol(vol);
                }
            }
        }
        break;

    case WM_CTLCOLORSTATIC:
        {
            // 色選択ボタンの色返却
            HWND hStWnd = (HWND)lp;
            unsigned int idx = 0;
            switch(GetDlgCtrlID(hStWnd)){
            case IDC_SE_SORT_COLOR_TX:  idx = 0; break;
            case IDC_SE_SORT_COLOR_BG:  idx = 1; break;
            case IDC_SE_BC_COLOR_TX:    idx = 2; break;
            case IDC_SE_BC_COLOR_BG:    idx = 3; break;
            default: return FALSE;
            }
            return (LRESULT)hBrushColor[idx];
        }
        break;

    case WM_SYSCOMMAND:
        if(wp == SC_CLOSE){
            EndDialog(hDlgWnd, FALSE);
            break;
        }
        return FALSE;
        break;


    case WM_DESTROY:
        if(hToolTipWnd) DestroyWindow(hToolTipWnd);
        for(int i = 0; i < ESIZEOF(hBrushColor); i++){
            if(hBrushColor[i]) DeleteObject((HGDIOBJ)hBrushColor[i]);
        }
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

// リストビューソート
static int CALLBACK fnCompare_(LPARAM idx1, LPARAM idx2, LPARAM col){
    int ret = 0;
    regdata_info.lock();

    ITER(regdata_info) it1 = regdata_info.find(idx1);
    ITER(regdata_info) it2 = regdata_info.find(idx2);
    if(it1 != regdata_info.end() && it2 != regdata_info.end()){
        c_regdata *cr1 = &(it1->second);
        c_regdata *cr2 = &(it2->second);

        switch(col){
        case COLINDEX_ENABLE:
            ret = (cr2->notify & NOTIFY_ENABLE)  - (cr1->notify & NOTIFY_ENABLE); break;
        case COLINDEX_BALLOON:
            ret = (cr2->notify & NOTIFY_BALLOON) - (cr1->notify & NOTIFY_BALLOON); break;
        case COLINDEX_BROWSER:
            ret = (cr2->notify & NOTIFY_BROWSER) - (cr1->notify & NOTIFY_BROWSER); break;
        case COLINDEX_SOUND:
            ret = (cr2->notify & NOTIFY_SOUND)   - (cr1->notify & NOTIFY_SOUND); break;
        case COLINDEX_EXTAPP:
            ret = (cr2->notify & NOTIFY_EXTAPP)  - (cr1->notify & NOTIFY_EXTAPP); break;
        case COLINDEX_KEY_NAME:
            ret = cr1->key_name.compare(cr2->key_name); break;
        case COLINDEX_KEY:
            ret = key_compare(cr1->key, cr2->key); break;
        case COLINDEX_MEMO:
            ret = cr1->memo.compare(cr2->memo); break;
        case COLINDEX_LASTSTART:
            {
                time_t td = cr2->last_start - cr1->last_start;
                ret = 0; if(td < 0) ret = -1; else ret = 1;
            }
            break;
        case COLINDEX_LABEL:
            ret = cr1->label - cr2->label; break;
        }
    }

    regdata_info.unlock();
    return ret;
}

// リストビューソート(昇順/降順の制御)
static int CALLBACK fnCompare(LPARAM idx1, LPARAM idx2, LPARAM col){
    if(col == -1) return idx1 - idx2;
    int rev = (col >> 8) & 0xff;
    int ret = fnCompare_(idx1, idx2, col & 0xff);
    if(ret == 0) ret = idx1 - idx2;

    if(rev) ret = -ret;
    return ret;
}


// キーボードフックプロシージャ
// リストビューコントロール上でのショートカットキー取得
static LRESULT CALLBACK HookProc(int nCode, WPARAM wp, LPARAM lp){
    MSG *msg;

    if(nCode >= 0){
        msg = (MSG *)lp;
        if( hAccel != NULL && msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN){
            if(msg->hwnd == hWndListView){
                _dbg(_T("HookProc: hWnd = 0x%08x\n"), msg->hwnd);
                if(wp == PM_REMOVE){
                    if(TranslateAccelerator(hDlg, hAccel, msg)){
                        msg->message = WM_NULL;
                    }
                }
            }
        }
    }
    return CallNextHookEx(HHook, nCode, wp, lp);
}

// リストビュー サブクラス
static LRESULT CALLBACK ListViewHookProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg) {
    case WM_RBUTTONDOWN:
        SendMessage(hDlg, WM_COMMAND, MAKEWPARAM(IDC_LISTVIEW, 0xffff), lp);
        return 0;

//#ifndef _DEBUG
    case WM_NOTIFY:
        {
            // アイコンのみのカラムに対して幅調整を制限(リリース版のみ)
            HD_NOTIFY *phdn = (HD_NOTIFY *)lp;
            switch(phdn->hdr.code)
            {
            case HDN_ITEMCHANGING:
            case HDN_BEGINTRACK:
            case HDN_DIVIDERDBLCLICK:
                if(phdn->iItem == COLINDEX_ENABLE ||
                    (phdn->iItem >= COLINDEX_BALLOON && phdn->iItem <= COLINDEX_EXTAPP))
                {
                    return TRUE;
                }
                break;
            }
        }
        break;
//#endif
    }

    return CallWindowProc((WNDPROC)Org_WndProc_Listview, hDlgWnd, msg, wp, lp);
}


// アイコンボタンのHoverを検出要求
static void IconHook_HoverTrack(HWND hWnd){
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_HOVER;
    tme.hwndTrack = hWnd;
    tme.dwHoverTime = HOVER_DEFAULT;
    TrackMouseEvent(&tme);
}
// アイコンボタンのLeaveを検出要求
static void IconHook_LeaveTrack(HWND hWnd){
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hWnd;
    tme.dwHoverTime = HOVER_DEFAULT;
    TrackMouseEvent(&tme);
}

// スタティックコントロール(アイコン) サブクラス
// マウスオーバー時の枠付け、および左クリックを検出
static LRESULT CALLBACK IconHookProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp){

    _dbg(_T("MSG(0x%08x, 0x%08x, 0x%08x, 0x%08x)\n"), hWnd, msg, wp, lp);
    switch (msg) {
    case WM_NCHITTEST:
        {
            IconHook_LeaveTrack(hWnd);
            LONG style = GetWindowLong(hWnd, GWL_EXSTYLE);
            if(! (style & WS_EX_STATICEDGE) ){
                SetWindowLong(hWnd, GWL_EXSTYLE, style | WS_EX_STATICEDGE);
                //InvalidateRect(hWnd, NULL, TRUE);
                SetWindowPos(hWnd, NULL, 0,0,0,0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_DRAWFRAME);
                //UpdateWindow(hWnd);
            }
            return HTCLIENT;
        }
        break;

    case WM_LBUTTONDOWN: 
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            _dbg(_T("DragDetect() start\n"));
            if(! DragDetect(hWnd, pt)){
                PostMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hWnd), STN_CLICKED), 0);
            }
            _dbg(_T("DragDetect() end\n"));
        }
        break;

    case WM_MOUSELEAVE:
        {
            int x = LOWORD(lp);
            int y = HIWORD(lp);
            _dbg(_T("WM_MOUSELEAVE(0x%08x, %d, %d)\n"), hWnd, x, y);

            SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) & ~WS_EX_STATICEDGE);
            //InvalidateRect(hWnd, NULL, FALSE);
            SetWindowPos(hWnd, NULL, 0,0,0,0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_DRAWFRAME);
            //UpdateWindow(hWnd);
        }
        break;

    }

    FARPROC OrgWndProc = (FARPROC)GetWindowLong(hWnd, GWL_USERDATA);
    return CallWindowProc((WNDPROC)OrgWndProc, hWnd, msg, wp, lp);
}

// アイコンボタンのサブクラス化設定
static void IconHookInit(HWND hDlgWnd){
    WindowSubClass(GetDlgItem(hDlgWnd, IDC_MSGINFO_ICON),   (FARPROC)IconHookProc);
    WindowSubClass(GetDlgItem(hDlgWnd, IDC_WRENCH),         (FARPROC)IconHookProc);
    WindowSubClass(GetDlgItem(hDlgWnd, IDC_BOOKMARK_PLUS),  (FARPROC)IconHookProc);
    WindowSubClass(GetDlgItem(hDlgWnd, IDC_LABELSET),       (FARPROC)IconHookProc);
}


// メッセージエリアの出力履歴保存
static deque_ts<tstring> msginfo;
static HWND hWndMsgInfo = NULL;

// メッセージエリア出力要求受付(コールバック)
static void msginfo_ind(const TCHAR *msg){

    TCHAR timebuf[24];
    timefmt(timebuf, ESIZEOF(timebuf), 0);
    // ウィンドウに反映
    SetDlgItemText(hDlg, IDC_MSG, msg);

    // タイムスタンプ付加
    tstring savemsg;
    savemsg += timebuf;
    savemsg += _T(" : ");
    savemsg += msg;

    // 履歴バッファに保存
    msginfo.lock();
    msginfo.push_back(savemsg);
    if(msginfo.size() > MSGINFO_BACKUP){
        msginfo.pop_front();
    }
    msginfo.unlock();
    _dbg(_T("MSGINFO: %s\n"), savemsg.c_str());

    // ログ表示ウィンドウに更新を通知
    if(hWndMsgInfo){
        PostMessage(hWndMsgInfo, WM_MSGINFO_UPDATE, 0, 0);
    }
}

// ログ表示ウィンドウ
static BOOL CALLBACK MsgInfoProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    switch(msg) {
    case WM_INITDIALOG:
        SetWindowPos(hDlgWnd, NULL, 0, 0, 640, 240, SWP_NOZORDER | SWP_NOMOVE);
        ShowWindow(hDlgWnd, SW_SHOW);
        hWndMsgInfo = hDlgWnd;

        // 初回の表示
        PostMessage(hDlgWnd, WM_MSGINFO_UPDATE, 0, 0);
        break;

    case WM_MSGINFO_UPDATE:
        {
            // 履歴バッファ更新通知によりウィンドウに反映
            HWND hEditWnd = GetDlgItem(hDlgWnd, IDC_MI_MSG);
            tstring msg;
            msginfo.lock();
            FOREACH(it, msginfo){
                msg += (*it);
                msg += _T("\r\n");
            }
            Edit_SetText(hEditWnd, msg.c_str());
            Edit_Scroll(hEditWnd, Edit_GetLineCount(hEditWnd), 0);

            msginfo.unlock();
        }
        break;

    case WM_SYSCOMMAND:
        switch(wp){
        case SC_CLOSE:
            DestroyWindow(hDlgWnd);
            break;
        default:
            return FALSE;
        }
        break;

    case WM_SIZE:
        {
            // エディットボックスをウィンドウサイズに追従
            int nWidth = LOWORD(lp); 
            int nHeight = HIWORD(lp);
            SetWindowPos(GetDlgItem(hDlgWnd, IDC_MI_MSG), NULL, 0, 0, nWidth, nHeight, SWP_NOZORDER);
        }
        break;

    case WM_DESTROY:
        hWndMsgInfo = NULL;
        break;
        
    default:
        return FALSE;
    }
    return TRUE;
}


// 登録データ変更通知(コールバック / IN: dbidx, fUpdate)
static void alertinfo_ntf(unsigned int dbidx, BOOL fUpdate){
    PostMessage(hDlg, WM_ALINFO_NOTIFY, dbidx, fUpdate);
}

// タスクトレイアイコン制御変数
static UINT uiTaskbarCreated;
static NOTIFYICONDATA nIcon;
static bool bTrayIconInit = false;
static tstring g_last_lv;

// リトライつきNotifyIcon()起動
static BOOL retry_Shell_NotifyIcon(DWORD t, NOTIFYICONDATA *i){
    BOOL ret = FALSE;
    int retry = 4;
    while(retry--){
        if(Shell_NotifyIcon(t, i)){
            ret = TRUE; break;
        }
        // failed
        if(GetLastError() != ERROR_TIMEOUT) break;
        // failed / timeout
        Sleep(500);        
    }
    return ret;
}

// タスクトレイアイコン追加
static void nawnd_addicon(void){
    retry_Shell_NotifyIcon(NIM_ADD, &nIcon);
}
// タスクトレイアイコン削除
static void nawnd_delicon(void){
    if(!bTrayIconInit) return;
    retry_Shell_NotifyIcon(NIM_DELETE, &nIcon);
    bTrayIconInit = false;
}
// タスクトレイバルーン出力
static void nawnd_msgicon(const TCHAR *msg, tstring &last_lv){
    nIcon.uFlags = nIcon.uFlags | NIF_INFO;
    _tcsncpy_s(nIcon.szInfo, msg, sizeof(nIcon.szInfo));
    nIcon.szInfo[ESIZEOF(nIcon.szInfo)-1] = '\0';
    nIcon.dwInfoFlags = NIIF_INFO;
    if(retry_Shell_NotifyIcon(NIM_MODIFY, &nIcon)){
        g_last_lv = last_lv;
    }
}
// タスクトレイアイコン初期化
static void nawnd_initicon(HWND hWnd){
    memset(&nIcon, 0, sizeof(nIcon)); 
    nIcon.cbSize    = sizeof(nIcon);
    nIcon.hWnd      = hWnd;
    nIcon.uID       = 0;
    nIcon.uFlags    = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nIcon.uCallbackMessage  = WM_TASKTRAY;
    nIcon.hIcon     = LoadIconRsc(IDI_NOTIFY);
    _stprintf_s(nIcon.szTip, _T("%s %s"), PROGRAM_NAME, VERSION_STRING);
    nIcon.uTimeout = 30000;
    _tcscpy_s(nIcon.szInfoTitle, PROGRAM_NAME);

    nawnd_addicon();
    // Explorer再起動対応
    uiTaskbarCreated = RegisterWindowMessage(_T("TaskbarCreated"));
    bTrayIconInit = true;
}

// dbidxからlvidxへの変換
static int dbidx2lvidx(HWND hWndListView, unsigned int dbidx){
    LV_FINDINFO fi;
    fi.flags = LVFI_PARAM;
    fi.lParam = dbidx;
    return ListView_FindItem(hWndListView, -1, &fi);
}

// lvidxからdbidxへの変換
static unsigned int lvidx2dbidx(HWND hWndListView, unsigned int lvidx){
    unsigned int ret = 0;
    LVITEM item;
    item.iItem = lvidx;
    item.iSubItem = 0;
    item.mask = LVIF_PARAM;
    if(ListView_GetItem(hWndListView, &item)){
        ret = item.lParam;
    }
    return ret;
}

// 登録データのリストビューおよびDBへの反映(IN: dbidx)
static void updatelvdb(HWND hWndListView, unsigned int dbidx){
    //InvalidateRect(hDlgWnd, NULL, FALSE);
    regdata_info.lock();
    do {
        if(dbidx <= 0) break;

        ITER(regdata_info) it = regdata_info.find(dbidx);
        if(it == regdata_info.end()) break;

        c_regdata *rd = &(it->second);
        if(rd->idx != dbidx) break;

        nadb.updateregdata(*rd);
        int lvidx = dbidx2lvidx(hWndListView, dbidx);
        if(lvidx < 0){
            // not found
            break;
        }
        UpdateListView(hWndListView, lvidx, rd);

    } while(0);
    regdata_info.unlock();
}

// アイコンからビットマップへの変換(トラックメニューへアイコン追加用(Vista以上))
HBITMAP CreateBmpFronIcon(HINSTANCE hInst, WORD icon){
    HICON hIcon = LoadIconRsc(icon);
    if(!hIcon) return NULL;

    HBITMAP hBmp, hTBmp;
    LPVOID           pbits;
    BITMAPINFO       bmi;
    BITMAPINFOHEADER bmiHdr;

    memset(&bmiHdr, 0, sizeof(bmiHdr));
    bmiHdr.biSize      = sizeof(bmiHdr);
    bmiHdr.biWidth     = ICO_SW;
    bmiHdr.biHeight    = ICO_SH;
    bmiHdr.biPlanes    = 1;
    bmiHdr.biBitCount  = 32;
    bmi.bmiHeader = bmiHdr;
    hBmp = CreateDIBSection(NULL, (LPBITMAPINFO)&bmi, DIB_RGB_COLORS, &pbits, NULL, 0);
    if(!hBmp) return NULL;

    HDC hMemDC = CreateCompatibleDC(NULL);
    hTBmp = (HBITMAP)SelectObject(hMemDC, hBmp);
    DrawIconEx(hMemDC, 0, 0, hIcon, ICO_SW, ICO_SH, 0, NULL, DI_NORMAL);
    SelectObject(hMemDC, hTBmp);
    DeleteDC(hMemDC);

    return hBmp;
}


// メインダイアログ
LRESULT CALLBACK MainDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    static int iSortedIdx = 0;
    static bool initializing = false;
    static HWND hToolTipWnd = NULL;
    static HWND hWndLvHdr = NULL;
	static IDropTarget *pDropTarget = NULL;
    static tstring sLabelName[NICOALERT_LABEL_MAX];

    static unsigned int default_color_tx = RGB(0,0,0);
    static unsigned int default_color_bg = RGB(255,255,255);
    static unsigned int startbc_color_tx = 0;
    static unsigned int startbc_color_bg = 0;
    static unsigned int sortcol_color_tx = 0;
    static unsigned int sortcol_color_bg = 0;

    switch(msg) {
    case WM_INITDIALOG:
        {
            InitCommonControls();

            initializing = true;
            hDlg = hDlgWnd;

            // データベース読み出し(設定/登録情報)
            if(!LoadDatabase()){
                MessageBox(hDlgWnd, _T("データベースの読み込みに失敗しました。"), PROGRAM_NAME, MB_OK | MB_ICONERROR);
                PostQuitMessage(1);
                return FALSE;
            }

            // 再描画タイマ設定
            InitRedrawTimer(hDlgWnd);

#ifdef _DEBUG
            setting_info.lock();
            FOREACH(it, setting_info){
                _dbg(_T("OPTION: %s = %s\n"), it->first.c_str(), it->second.c_str());
            }
            setting_info.unlock();
#endif

#ifdef BETA_VERSION
            // 開発版警告
            if(ReadOptionInt(OPTION_BETA_VERSION_CONFIRM, 0) == 0){
                if(MessageBox(hDlgWnd, _T("本バージョンは開発版です。\n不具合が発生する可能性があることを了承できない場合、\n使用しないようお願いします。継続しますか？"), PROGRAM_NAME, MB_YESNO | MB_ICONERROR) != IDYES){
                    PostQuitMessage(1);
                    return FALSE;
                }
                SaveOptionInt(OPTION_BETA_VERSION_CONFIRM, 1);
            }
#else
            DeleteOption(OPTION_BETA_VERSION_CONFIRM);
#endif
            // ウィンドウサイズ設定読み出し
            unsigned int width = ReadOptionInt(OPTION_WIDTH, WINDOWWIDTH);
            unsigned int height = ReadOptionInt(OPTION_HEIGHT, WINDOWHEIGHT);

            RECT clientrect;
            clientrect.left = clientrect.top = 0;
            clientrect.right = width;
            clientrect.bottom = height;

            AdjustWindowRect(&clientrect, GetWindowLong(hDlgWnd, GWL_STYLE), FALSE);
            width = clientrect.right - clientrect.left;
            height = clientrect.bottom - clientrect.top;
            if(width < WINDOWWIDTH) width = WINDOWWIDTH;
            if(height < WINDOWHEIGHT) height = WINDOWHEIGHT;

            SetWindowPos(hDlgWnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);

#ifdef _DEBUG
            //SetWindowPos(hDlgWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
#endif
            // ウィンドウ位置設定読み出し
            unsigned int xPos = ReadOptionInt(OPTION_WINDOW_XPOS, 0x7fffffff);
            unsigned int yPos = ReadOptionInt(OPTION_WINDOW_YPOS, 0x7fffffff);
            if(xPos != 0x7fffffff && yPos != 0x7fffffff){
                SetWindowPos(hDlgWnd, NULL, xPos, yPos, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            iSortedIdx = ReadOptionInt(OPTION_SORT_INDEX, DEF_OPTION_SORT_INDEX);

            // ラベル名読み出し
            SendMessage(hDlgWnd, WM_LABELRELOAD, 0, 0);

            // リストビュー構築
            hWndListView = GetDlgItem(hDlgWnd, IDC_LISTVIEW);
            InsertColumn(hWndListView);
            UpdateListViewAll(hWndListView);
            hWndLvHdr = ListView_GetHeader(hWndListView);

            // リストビューソートの反映
            PostMessage(hDlgWnd, WM_ALINFO_NOTIFY, 0, TRUE);

            // 起動時最小化処理
            if(ReadOptionInt(OPTION_MINIMIZE_ON_BOOT,  DEF_OPTION_MINIMIZE_ON_BOOT)){
                ShowWindow(hDlgWnd, SW_SHOWMINNOACTIVE);
                if(ReadOptionInt(OPTION_MINIMIZE_TO_TRAY,  DEF_OPTION_MINIMIZE_TO_TRAY)){
                    SetTimer(hDlgWnd, TID_MINIMIZE, 0, NULL);
                }
            } else {
                //InvalidateRect(hDlgWnd, NULL, TRUE);
                //UpdateWindow(hDlgWnd);
                //ShowWindow(hDlgWnd, SW_SHOWNORMAL);
            }

            // キーボードフック
            hAccel = LoadAccelerators(hInst, _T("IDR_ACCELERATOR"));
            HHook = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)HookProc, NULL, GetWindowThreadProcessId(hDlgWnd, NULL));

            // リストビュー メッセージフック(必須:カラム幅決定後)
            Org_WndProc_Listview = (FARPROC)SetWindowLong(hWndListView, GWL_WNDPROC, (long)ListViewHookProc);

            // アイコンボタン メッセージフック
            IconHookInit(hDlgWnd);

            // タスクトレイアイコン初期化
            nawnd_initicon(hDlgWnd);

            // ツールチップ
            {
                const ToolTipInfo uMainDlgIds[] = {
                    { IDC_MSGINFO_ICON,     _T("ログ表示") },
                    //{ IDC_MSG,              _T("-") },
                    { IDC_WRENCH,           _T("全体設定") },
                    { IDC_BOOKMARK_PLUS,    _T("アイテム登録追加") },
                    { IDC_LABELSET,         _T("ラベル管理") },
                };
                hToolTipWnd = ToolTipInit(hDlgWnd, hInst, uMainDlgIds, ESIZEOF(uMainDlgIds));
                
                // リストビューのツールチップのみ特殊設定
                // リストビュー上でのマウストラックにより範囲と文字列を随時反映
                TOOLINFO ti;
                memset(&ti, 0, sizeof(ti));
                ti.cbSize = sizeof(TOOLINFO);
                ti.hwnd = hWndListView;
                ti.hinst = hInst;
                ti.uFlags = TTF_SUBCLASS;

                ti.uId = 0;
                ti.lpszText = _T("");//LPSTR_TEXTCALLBACK;
                SendMessage(hToolTipWnd, TTM_ADDTOOL, 0, (LPARAM)&ti); 

                // ツールチップ表示ON/OFFを反映
                BOOL tip = ReadOptionInt(OPTION_TOOLTIP_HELP, DEF_OPTION_TOOLTIP_HELP)?TRUE:FALSE;
                SendMessage(hToolTipWnd, TTM_ACTIVATE, tip, 0);
            }

            TCHAR bootmsgbuf[128];
            _stprintf_s(bootmsgbuf, _T("%s %s : %s"), PROGRAM_NAME, VERSION_STRING, _T(UA_STRING));
            msginfo_ind(bootmsgbuf);

            // 設定保存インターバルタイマ
            SetTimer(hDlgWnd, TID_SETTING, 5000, NULL);

            // 各スレッド起動
            if(!bcc_start(alertinfo_ntf, nawnd_msgicon, msginfo_ind)){
                MessageBox(hDlgWnd, _T("スレッドが起動できません。(bcc_start)"), PROGRAM_NAME, MB_OK | MB_ICONERROR);
                PostQuitMessage(1);
                return FALSE;
            }
            if(!cmm_start(alertinfo_ind, msginfo_ind)){
                MessageBox(hDlgWnd, _T("スレッドが起動できません。(cmm_start)"), PROGRAM_NAME, MB_OK | MB_ICONERROR);
                PostQuitMessage(1);
                return FALSE;
            }
            if(!con_start(alertinfo_ntf, msginfo_ind)){
                MessageBox(hDlgWnd, _T("スレッドが起動できません。(con_start)"), PROGRAM_NAME, MB_OK | MB_ICONERROR);
                PostQuitMessage(1);
                return FALSE;
            }

            // カラー情報を保持
            startbc_color_tx  = ReadOptionInt(OPTION_START_TXCOLOR, DEF_OPTION_START_TXCOLOR);
            startbc_color_bg  = ReadOptionInt(OPTION_START_BGCOLOR, DEF_OPTION_START_BGCOLOR);
            sortcol_color_tx = ReadOptionInt(OPTION_LSORT_TXCOLOR, DEF_OPTION_LSORT_TXCOLOR);
            sortcol_color_bg = ReadOptionInt(OPTION_LSORT_BGCOLOR, DEF_OPTION_LSORT_BGCOLOR);

            initializing = false;

#ifdef _DEBUG
            tstring last_lv = _T("lv00");
            nawnd_msgicon(VERSION_STRING _T("(DebugBuild)") _T("\n") _T(UA_STRING), last_lv);
#endif
            // OLE D&Dの初期化
            OleInitialize(NULL);
            pDropTarget = new CDropTarget(hDlgWnd, WM_OLEDROP);
            RegisterDragDrop(hDlgWnd, pDropTarget);
        }
        break;


    case WM_COMMAND:
        switch(LOWORD(wp)){
        case IDC_LISTVIEW:
            {
                POINT pt;
                HMENU hmenu, hSubmenu;
                pt.x = LOWORD(lp);
                pt.y = HIWORD(lp);

                // 右クリック時に選択済みアイテムの上なら何もしない。
                // 非選択アイテムの右クリックの場合、該当アイテムのみ選択状態に変更
                do {
                    LVHITTESTINFO hi;
                    hi.pt = pt;
                    hi.flags = LVHT_NOWHERE;
                    int lvidx = ListView_HitTest(hWndListView, &hi);
                    if(lvidx < 0) break;
                    LVITEM item;
                    item.iItem = lvidx;
                    item.iSubItem = 0;
                    item.mask = LVIF_STATE;
                    item.state = 0;
                    item.stateMask = -1;
                    if(!ListView_GetItem(hWndListView, &item)) break;

                    if(!(item.state & LVIS_SELECTED)){
                        // 選択行をすべて解除
                        int lvidx = -1;
                        while(1){
                            lvidx = ListView_GetNextItem(hWndListView, lvidx, LVNI_ALL | LVNI_SELECTED);
                            if(lvidx < 0) break;
                            ListView_SetItemState(hWndListView, lvidx, 0, LVIS_SELECTED);
                        }
                    }
                    ListView_SetItemState(hWndListView, lvidx, LVIS_SELECTED, LVIS_SELECTED);

                } while(0);

                hmenu = LoadMenu(hInst, _T("IDR_TRACKMENU"));
                hSubmenu = GetSubMenu(hmenu, 0);

                // Vista以上のみ、トラックメニューにアイコン追加
                HBITMAP hBmp1 = NULL, hBmp2 = NULL, hBmp3 = NULL;
                if(GetOSVer() >= _WIN32_WINNT_VISTA){
                    hBmp1 = CreateBmpFronIcon(hInst, IDI_WRENCH);
                    SetMenuItemBitmaps(hSubmenu, IDM_SETTING, MF_BYCOMMAND, hBmp1, hBmp1);
                    hBmp2 = CreateBmpFronIcon(hInst, IDI_BOOKMARK_PLUS);
                    SetMenuItemBitmaps(hSubmenu, IDM_REGADD, MF_BYCOMMAND, hBmp2, hBmp2);
                    hBmp3 = CreateBmpFronIcon(hInst, IDI_LABEL);
                    SetMenuItemBitmaps(hSubmenu, IDM_LABELSET, MF_BYCOMMAND, hBmp3, hBmp3);
                }

                if(ListView_GetSelectedCount(hWndListView) == 1){
                    // 1件選択の場合、登録キーに適合するメニュー以外を削除
                    do {
                        int lvidx = ListView_GetNextItem(hWndListView, -1, LVNI_ALL | LVNI_SELECTED);
                        if(lvidx < 0) break;
                        unsigned int dbidx = lvidx2dbidx(hWndListView, lvidx);
                        if(dbidx == 0) break;

                        regdata_info.lock();
                        ITER(regdata_info) it = regdata_info.find(dbidx);
                        if(it != regdata_info.end()){
                            tstring &key = it->second.key;
                            tstring &last_lv = it->second.last_lv;

                            if(key.compare(0, COID_PREFIX_LEN, _T(COID_PREFIX))){
                                DeleteMenu(hSubmenu, IDM_OPEN_CO, MF_BYCOMMAND);
                            }
                            if(key.compare(0, CHID_PREFIX_LEN, _T(CHID_PREFIX))){
                                DeleteMenu(hSubmenu, IDM_OPEN_CH, MF_BYCOMMAND);
                            }
                            if(key.compare(0, USERID_PREFIX_LEN, _T(USERID_PREFIX))){
                                DeleteMenu(hSubmenu, IDM_OPEN_USER, MF_BYCOMMAND);
                            }
                            if(key.compare(0, LVID_PREFIX_LEN, _T(LVID_PREFIX)) && (last_lv == _T(""))){
                                DeleteMenu(hSubmenu, IDM_OPEN_LV, MF_BYCOMMAND);
                                DeleteMenu(hSubmenu, IDM_OPEN_LV_CB, MF_BYCOMMAND);
                            }
                        }
                        regdata_info.unlock();
                    } while(0);
                } else {
                    // 複数選択の場合、各ページを開くメニューを削除
                    DeleteMenu(hSubmenu, IDM_OPEN_CO, MF_BYCOMMAND);
                    DeleteMenu(hSubmenu, IDM_OPEN_CH, MF_BYCOMMAND);
                    DeleteMenu(hSubmenu, IDM_OPEN_USER, MF_BYCOMMAND);
                    DeleteMenu(hSubmenu, IDM_OPEN_LV, MF_BYCOMMAND);
                    DeleteMenu(hSubmenu, IDM_OPEN_LV_CB, MF_BYCOMMAND);
                }
                
                // ラベル追加/削除サブメニュー生成
                int menunum = GetMenuItemCount(hSubmenu);
                for(int menuidx = 0; menuidx < menunum; menuidx++){
                    MENUITEMINFO mii;
                    memset(&mii, 0, sizeof(mii));
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_SUBMENU;
                    if(GetMenuItemInfo(hSubmenu, menuidx, TRUE, &mii) && mii.hSubMenu){
                        HMENU hSubSubMenu = mii.hSubMenu;
                        mii.fMask = MIIM_ID;
                        if(!GetMenuItemInfo(hSubSubMenu, 0, TRUE, &mii)) continue;
                        if(mii.wID != IDM_LABELADD_ID_START && mii.wID != IDM_LABELDEL_ID_START) continue;
                        UINT wIDstart = mii.wID;
                        int menunums = GetMenuItemCount(hSubSubMenu);
                        for(int labelid = 0; labelid < NICOALERT_LABEL_MAX; labelid++){
                            TCHAR buf[NICOALERT_LABEL_LEN_MAX+1];
                            tstring labelname = ReadLabelName(labelid);
                            _tcscpy_s(buf, labelname.c_str());
                            mii.wID = wIDstart + labelid;
                            mii.fMask = MIIM_ID | MIIM_STRING;
                            mii.dwTypeData = buf;
                            InsertMenuItem(hSubSubMenu, menunums+labelid, TRUE, &mii);
                        }
                    }
                }
#ifdef _DEBUG
                {
                    MENUITEMINFO mii;
                    memset(&mii, 0, sizeof(mii));
                    mii.cbSize = sizeof(mii);
                    mii.wID = IDM_DEBUGNOTIFY;
                    mii.fMask = MIIM_ID | MIIM_STRING;
                    mii.dwTypeData = _T("デバッグ通知");
                    InsertMenuItem(hSubmenu, GetMenuItemCount(hSubmenu), TRUE, &mii);
                }
#endif

                // リストビュー右クリック トラックメニュー表示
                ClientToScreen(hWndListView, &pt);
                TrackPopupMenu(hSubmenu, TPM_LEFTALIGN, pt.x, pt.y, 0, hDlgWnd, NULL);
                DestroyMenu(hmenu);

                if(hBmp1) DeleteObject(hBmp1);
                if(hBmp2) DeleteObject(hBmp2);
                if(hBmp3) DeleteObject(hBmp3);
            }
            break;

        case IDC_MSGINFO_ICON:
            if(HIWORD(wp) == STN_CLICKED){
                // ログ表示アイコン押下
                if(!hWndMsgInfo){
                    CreateDialog(hInst, _T("DIALOG_MSGINFO"), hDlgWnd, MsgInfoProc);
                }
            }
            break;

        case IDC_WRENCH:
            if(HIWORD(wp) == STN_CLICKED){
                // 全体設定アイコン押下
                PostMessage(hDlgWnd, WM_COMMAND, IDM_SETTING, 0);
            }
            break;

        case IDC_BOOKMARK_PLUS:
            if(HIWORD(wp) == STN_CLICKED){
                // 登録追加アイコン押下
                PostMessage(hDlgWnd, WM_COMMAND, IDM_REGADD, 0);
            }
            break;

        case IDC_LABELSET:
            if(HIWORD(wp) == STN_CLICKED){
                // ラベル設定アイコン押下
                PostMessage(hDlgWnd, WM_COMMAND, IDM_LABELSET, 0);
            }
            break;


        case IDM_SETTING:
            {
                // 全体設定ダイアログ起動
                if(DialogBox(hInst, _T("DIALOG_SETTING"), hDlgWnd, SettingDlgProc)){
                    // 保存時
                    startbc_color_tx  = ReadOptionInt(OPTION_START_TXCOLOR, DEF_OPTION_START_TXCOLOR);
                    startbc_color_bg  = ReadOptionInt(OPTION_START_BGCOLOR, DEF_OPTION_START_BGCOLOR);
                    sortcol_color_tx = ReadOptionInt(OPTION_LSORT_TXCOLOR, DEF_OPTION_LSORT_TXCOLOR);
                    sortcol_color_bg = ReadOptionInt(OPTION_LSORT_BGCOLOR, DEF_OPTION_LSORT_BGCOLOR);

                    // ツールチップ表示ON/OFFを反映
                    BOOL tip = ReadOptionInt(OPTION_TOOLTIP_HELP, DEF_OPTION_TOOLTIP_HELP)?TRUE:FALSE;
                    SendMessage(hToolTipWnd, TTM_ACTIVATE, tip, 0); 

                    // カラー変更を反映
                    InvalidateRect(hWndListView, NULL, FALSE);
                    UpdateWindow(hWndListView);

                    // 自動キー名取得処理ON/OFFを反映
                    if(ReadOptionInt(OPTION_AUTO_KEYNAME_ACQ, DEF_OPTION_AUTO_KEYNAME_ACQ)){
                        con_start();
                    }
                }
            }
            break;

        case IDM_REGADD:
            {
                // アイテム登録追加ダイアログ起動
                unsigned int mindbidx = DialogBox(hInst, _T("DIALOG_REGADD"), hDlgWnd, RegAddDlgProc);
                if(mindbidx == 0) break;

                // アイテム追加あり
                initializing = true;
                InsertListViewAdd(hWndListView, mindbidx);
                con_add(mindbidx);
                initializing = false;

                // 追加アイテムの先頭を表示
                ListView_EnsureVisible(hWndListView, dbidx2lvidx(hWndListView, mindbidx), FALSE);
            }
            break;

        case IDM_REGSET:
        case IDM_REGDEL:
            {
                // 選択アイテム設定/削除
                int num = ListView_GetItemCount(hWndListView);
                unsigned int focusidx = 0;
                vector <unsigned int> dbidx_lst;
                LVITEM item;
                item.iSubItem = 0;
                item.mask = LVIF_PARAM | LVIF_STATE;
                item.state = 0;
                item.stateMask = -1;

                // 選択アイテムのlvidx→dbidxリストを生成
                int lvidx = -1;
                unsigned int dbidx;
                while(1){
                    lvidx = ListView_GetNextItem(hWndListView, lvidx, LVNI_ALL | LVNI_SELECTED);
                    if(lvidx < 0) break;
                    dbidx = lvidx2dbidx(hWndListView, lvidx);
                    if(dbidx > 0) dbidx_lst.push_back(dbidx);
                }

                // 選択アイテムがなければフォーカスアイテムを採用
                if(dbidx_lst.size() == 0){
                    lvidx = ListView_GetNextItem(hWndListView, -1, LVNI_ALL | LVNI_FOCUSED);
                    if(lvidx >= 0){
                        dbidx = lvidx2dbidx(hWndListView, lvidx);
                        if(dbidx > 0) dbidx_lst.push_back(dbidx);
                    }
                }

                // それでも何もなければ何もしない
                if(dbidx_lst.size() == 0) break;

                if(LOWORD(wp) == IDM_REGDEL){
                    // 選択アイテム削除
                    TCHAR buf[256];
                    _stprintf_s(buf, _T("%d 件の登録を削除しますか？"), dbidx_lst.size());
                    if(MessageBox(hDlgWnd, buf, PROGRAM_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) break;

                    // リストビューおよびDBから削除
                    nadb.tr_begin();
                    LV_Lock(hWndListView);
                    RFOREACH(it, dbidx_lst){
                        unsigned int dbidx = *it;
                        if(!UnRegistration(dbidx)) continue;
                        int lvidx = dbidx2lvidx(hWndListView, dbidx);
                        if(lvidx < 0) continue;
                        DeleteListView(hWndListView, lvidx);
                    }
                    LV_UnLock(hWndListView);
                    nadb.tr_commit();
                    break;
                }
                if(LOWORD(wp) == IDM_REGSET){
                    // 選択アイテム設定
                    int changed = DialogBoxParam(hInst, _T("DIALOG_REGSET"), hDlgWnd, RegSetDlgProc, (LPARAM)&dbidx_lst);
                    if(!changed) break;

                    // 変更あり、リストビューおよびDBへ反映
                    nadb.tr_begin();
                    LV_Lock(hWndListView);
                    FOREACH(it, dbidx_lst){
                        unsigned int dbidx = *it;
                        updatelvdb(hWndListView, dbidx);
                    }
                    LV_UnLock(hWndListView);
                    nadb.tr_commit();

                    // リストビューソートの反映
                    PostMessage(hDlgWnd, WM_ALINFO_NOTIFY, 0, FALSE);
                }
            }
            break;

        case IDM_ALLSEL:
            {
                // Ctrl-Aショートカットキー(リストビューアイテム全選択)
                _dbg(_T("IDM_ALLSEL\n"));
                int count = ListView_GetItemCount(hWndListView);
                for(int i = 0; i < count; i++){
                    ListView_SetItemState(hWndListView, i, LVNI_SELECTED, LVNI_SELECTED);
                }
            }
            break;

        case IDM_ENABLE:
        case IDM_DISABLE:
            {
                // 選択アイテム有効/無効切り替え
                _dbg(_T("IDM_ENABLE/IDM_DISABLE\n"));
                bool fCheck = (LOWORD(wp) == IDM_ENABLE) ? true : false;
                int lvidx = -1;
                nadb.tr_begin();
                while(1){
                    lvidx = ListView_GetNextItem(hWndListView, lvidx, LVNI_ALL | LVNI_SELECTED);
                    if(lvidx < 0) break;
                    ListView_SetCheckState(hWndListView, lvidx, fCheck);
                }
                nadb.tr_commit();
            }
            break;

        case IDM_HELP:
            {
                // オンラインマニュアルページを開く
                tstring url = NICOALERT_ONLINE_HELP; 
                exec_browser(url);
            }
            break;

        case IDM_LABELSET:
            {
                if(!hWndLabelDlg){
                    CreateDialog(hInst, _T("DIALOG_LABELSET"), hDlgWnd, LabelSetDlgProc);
                }
            }
            break;

        case IDM_RESTORE:
            // ウィンドウ最小化/トレイ収納から復帰
            ShowWindow(hDlgWnd, SW_SHOW);
            Sleep(100);
            ShowWindow(hDlgWnd, SW_RESTORE);
            SetForegroundWindow(hDlgWnd);
            SetFocus(hDlgWnd);
            break;

        case IDM_VERSION:
            MessageBox(hDlgWnd,
                PROGRAM_NAME _T(" ") VERSION_STRING _DEBUGT("(DebugBuild)") _T(" - ") _T(VERSION_LIC) _T("\n\n")
                _T("User-Agent: ") _T(UA_STRING) _T("\n"), _T("バージョン情報"), MB_OK);
            break;

        case IDM_DEBUGNOTIFY:
            {
                c_alertinfo ai;
                ai.infotype = INFOTYPE_OFFICIAL;
                ai.lvid = _T("lv0");
                ai.usrid = _T("user/0");
                alertinfo_ind(&ai);
            }
            break;

        case IDM_EXIT:
            // 終了確認
            if(ReadOptionInt(OPTION_CONFIRM_ON_CLOSE, DEF_OPTION_CONFIRM_ON_CLOSE)){
                if(MessageBox(hDlgWnd, _T("終了しますか？"), PROGRAM_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            }
            PostQuitMessage(0);
            break;

        case IDM_OPEN_CO:
        case IDM_OPEN_CH:
        case IDM_OPEN_USER:
        case IDM_OPEN_LV:
        case IDM_OPEN_LV_CB:
            {
                // キー種別に応じたページオープン要求
                if(ListView_GetSelectedCount(hWndListView) == 1){
                    do {
                        int lvidx = ListView_GetNextItem(hWndListView, -1, LVNI_ALL | LVNI_SELECTED);
                        if(lvidx < 0) break;
                        unsigned int dbidx = lvidx2dbidx(hWndListView, lvidx);
                        if(dbidx == 0) break;

                        regdata_info.lock();
                        ITER(regdata_info) it = regdata_info.find(dbidx);
                        if(it != regdata_info.end()){
                            if(LOWORD(wp) != IDM_OPEN_LV_CB){
                                tstring key = it->second.key;
                                if(LOWORD(wp) == IDM_OPEN_LV && it->second.last_start != 0){
                                    key = it->second.last_lv;
                                }
                                exec_browser_by_key(key);   // ブラウザで開く
                            } else {
                                tstring key = NICOLIVE_LVID_URL;
                                if(it->second.last_start != 0){
                                    key += it->second.last_lv;
                                } else {
                                    key += it->second.key;
                                }
                                _dbg(_T("copy to clipboard : %s"), key.c_str());
                                CopyToClipBoard(key.c_str());   // クリップボードにコピー
                            }
                        }
                        regdata_info.unlock();

                    } while(0);
                }
            }
            break;

        default:
            if(IDM_LABELADD_ID_START <= LOWORD(wp) && LOWORD(wp) < IDM_LABELADD_ID_START + NICOALERT_LABEL_MAX){
                _dbg(_T("IDM_LABELADD_ID_START : wp=%08x lp = %08x"), wp, lp);
                PostMessage(hDlgWnd, WM_LABELCTL, LABEL_ADD, LOWORD(wp) - IDM_LABELADD_ID_START);
                break;
            }
            if(IDM_LABELDEL_ID_START <= LOWORD(wp) && LOWORD(wp) < IDM_LABELDEL_ID_START + NICOALERT_LABEL_MAX){
                _dbg(_T("IDM_LABELDEL_ID_START : wp=%08x lp = %08x"), wp, lp);
                PostMessage(hDlgWnd, WM_LABELCTL, LABEL_DEL, LOWORD(wp) - IDM_LABELDEL_ID_START);
                break;
            }

            return FALSE;
        }
        break;

    case WM_ALINFO_NOTIFY:
        {
            // 登録情報変更通知、兼、リストビューソート要求
            unsigned int dbidx = (unsigned int)wp;
            BOOL lvUpdate = (BOOL)lp;

            if(dbidx){
                updatelvdb(hWndListView, dbidx);
                AddRedrawTimer(hDlgWnd, dbidx);
            }

            if(lvUpdate){
                // ソート条件を設定してソート実行
                //  iSortedIdx = 0 : ソートなし(登録順表示)
                //  iSortedIdx < 0 : カラム |iSortedIdx| で降順ソート
                //  iSortedIdx > 0 : カラム  iSortedIdx  で昇順ソート
                int rev = 0;
                int item = 0;
                if(iSortedIdx != 0){
                    if(iSortedIdx < 0){
                        rev = 1;
                        item = (-iSortedIdx) - 1;
                    } else {
                        item = iSortedIdx - 1;
                    }
                    ListView_SortItems(hWndListView, fnCompare, ((rev << 8) | item) );
                } else {
                    ListView_SortItems(hWndListView, fnCompare, -1);
                }
            }
        }
        break;

    case WM_TASKTRAY:
        {
            // タスクトレイ通知
            //_dbg(_T("wp = 0x%x, lp = 0x%x\n"), wp, lp);

            switch(lp){
            case WM_LBUTTONDOWN:
                // ウィンドウ復元
                PostMessage(hDlgWnd, WM_COMMAND, IDM_RESTORE, 0);
                break;

            case WM_RBUTTONUP:
                // トラックメニュー表示
                POINT pt;
                HMENU hMenu, hSubmenu;

                GetCursorPos(&pt);
                hMenu = LoadMenu(hInst, _T("IDR_TRAYMENU"));
                hSubmenu = GetSubMenu(hMenu, 0);

                TrackPopupMenu(hSubmenu, TPM_LEFTALIGN, pt.x, pt.y, 0, hDlgWnd, NULL);
                DestroyMenu(hMenu);
                break;

            case NIN_BALLOONUSERCLICK:
                // バルーンクリック
                if(ReadOptionInt(OPTION_BALLOON_OPEN, DEF_OPTION_BALLOON_OPEN)){
                    if(!exec_browser_by_key(g_last_lv)){
                        msginfo_ind(_T("ブラウザ起動に失敗しました。"));
                    }
                }
                break;
            }
        }
        break;

    case WM_NOTIFY:
        {
            NMHDR *pnmhdr = (NMHDR *)lp;
            if(pnmhdr->hwndFrom == hWndListView){
                /* アイテム選択変更 */
                if(pnmhdr->code == LVN_ITEMCHANGED){
                    if(initializing) break;
                    NMLISTVIEW *pnmlv = (NMLISTVIEW *)lp;

                    //_dbg(_T("LVN_ITEMCHANGED item=%u\n"), pnmlv->iItem);
                    //_dbg(_T("uOldState = %u, uNewState = %u\n"), pnmlv->uOldState, pnmlv->uNewState);

                    UINT os = pnmlv->uOldState & LVIS_STATEIMAGEMASK;
                    UINT ns = pnmlv->uNewState & LVIS_STATEIMAGEMASK;

                    if(os > 0 && ns > 0 && os != ns){
                        // 有効/無効切り替え通知
                        bool ischeck = (ListView_GetCheckState(hWndListView, pnmlv->iItem) != 0);

                        unsigned int dbidx = lvidx2dbidx(hWndListView, pnmlv->iItem);
                        if(dbidx > 0){
                            regdata_info.lock();
                            ITER(regdata_info) it = regdata_info.find(dbidx);
                            if(it != regdata_info.end()){
                                c_regdata *rd = &(it->second);
                                if((rd->notify & NOTIFY_ENABLE) != ischeck){
                                    rd->notify = (rd->notify & ~NOTIFY_ENABLE) | ((ischeck) ? NOTIFY_ENABLE : 0);
                                    nadb.updateregdata(*rd);
                                }
                            }
                            regdata_info.unlock();
                        }
                    }
                    break;
                }
                /* ヘッダクリック */
                if(pnmhdr->code == LVN_COLUMNCLICK){
                    NMLISTVIEW *pnmlv = (NMLISTVIEW *)lp;
                    int iSubItem = pnmlv->iSubItem+1;

                    if(iSortedIdx == iSubItem){
                        iSortedIdx = -iSortedIdx;
                    } else if(iSortedIdx == -iSubItem){
                        iSortedIdx = 0;
                    } else {
                        iSortedIdx = iSubItem;
                    }

                    SaveOptionInt(OPTION_SORT_INDEX, iSortedIdx);
                    PostMessage(hDlgWnd, WM_ALINFO_NOTIFY, 0, TRUE);
                    break;
                }

#if 0
                // リストビュー ダブルクリック
                if(pnmhdr->code == NM_DBLCLK){
                    _dbg(_T("NM_DBLCLK\n"));
                }
#endif

                if(pnmhdr->code == LVN_GETDISPINFO){
                    // _dbg(_T("LVN_GETDISPINFO iItem = %d, iSubItem = %d\n"), plvdi->item.iItem, plvdi->item.iSubItem);

                    NMLVDISPINFO *plvdi = (NMLVDISPINFO *)lp;
                    //_dbg(_T("mask = %x, iItem = %d, iSubItem = %d, \n"),
                    //    plvdi->item.mask, plvdi->item.iItem, plvdi->item.iSubItem);
                    //_dbg(_T("state = %x, stateMask = %x\n"),
                    //    plvdi->item.state, plvdi->item.stateMask);
                    //_dbg(_T("pszText = %p, cchTextMax = %d, iImage = %d\n"),
                    //    plvdi->item.pszText, plvdi->item.cchTextMax, plvdi->item.iImage);
                    //_dbg(_T("lParam = %d, iIndent = %d, iGroupId = %d\n"),
                    //    plvdi->item.lParam, plvdi->item.iIndent, plvdi->item.iGroupId);

                    // TCHAR szString[MAX_PATH];

                    if(plvdi->item.iSubItem == COLINDEX_LABEL){
                        if(plvdi->item.mask & LVIF_TEXT){
                            unsigned int dbidx = plvdi->item.lParam;
                            if(dbidx > 0){
                                regdata_info.lock();
                                ITER(regdata_info) it = regdata_info.find(dbidx);
                                if(it != regdata_info.end()){
                                    c_regdata *rd = &(it->second);
                                    unsigned int label = rd->label;
                                    unsigned int mask = 1;
                                    tstring labelstr;
                                    for(int i = 0; i < NICOALERT_LABEL_MAX; i++){
                                        if(label & mask){
                                            if(labelstr == _T("")){
                                                labelstr = sLabelName[i];
                                            } else {
                                                labelstr += _T(" ");
                                                labelstr += sLabelName[i];
                                            }
                                        }
                                        mask <<= 1;
                                    }
                                    _sntprintf_s(plvdi->item.pszText, plvdi->item.cchTextMax, plvdi->item.cchTextMax,
                                        _T("%s"), labelstr.c_str());
                                    plvdi->item.mask |= LVIF_DI_SETITEM; 
                                }
                                regdata_info.unlock();
                            }
                        }
                    } else {
#ifdef _DEBUG
                        __debugbreak();
#endif
                    }
                    break;
                }

                if(pnmhdr->code == NM_HOVER){
                    _dbg(_T("NM_HOVER : Wnd = 0x%x : wp = 0x%x, lp = 0x%x\n"), pnmhdr->hwndFrom, wp, lp);
                    // リストビューにLVS_EX_TRACKSELECTを設定しているが、
                    // LVN_HOTTRACK を検出するためだけなので、TRACKSELECTを無効化
                    SetWindowLong(hDlgWnd, DWL_MSGRESULT, (LONG)TRUE);
                    break;
                }

                if(pnmhdr->code == LVN_HOTTRACK){
                    // リストビュー上でマウスカーソル移動検出
                    static int olditem, oldsubitem;
                    LPNMLISTVIEW lpnmlv = (LPNMLISTVIEW)lp;
                    if(lpnmlv->iItem < 0) break;
                    // セル変更なしの場合は無視
                    if(olditem == lpnmlv->iItem && oldsubitem == lpnmlv->iSubItem) break;
                    if(!hToolTipWnd) break;

                    _dbg(_T("LVN_HOTTRACK : Wnd = 0x%x : wp = 0x%x, lp = 0x%x\n"), pnmhdr->hwndFrom, wp, lp);

                    // セル列/行からツールチップヘルプ文字列を生成
                    TCHAR msg[80]; msg[0] = '\0';
                    unsigned int dbidx = lvidx2dbidx(hWndListView, lpnmlv->iItem);
                    if(dbidx > 0){
                        regdata_info.lock();
                        ITER(regdata_info) it = regdata_info.find(dbidx);
                        if(it != regdata_info.end()){
                            c_regdata *rd = &(it->second);
                            switch(lpnmlv->iSubItem){
#ifdef _DEBUG
                            case COLINDEX_ENABLE:
                                _sntprintf_s(msg, ESIZEOF(msg), _T("dbidx = %u"), rd->idx); break;
#endif
                            case COLINDEX_KEY_NAME:
                                _sntprintf_s(msg, ESIZEOF(msg), _T("%s"), rd->key_name.c_str()); break;
                            case COLINDEX_LASTSTART:
                                if(rd->last_start){
                                    TCHAR timebuf[24];
                                    timefmt(timebuf, ESIZEOF(timebuf), rd->last_start);
                                    _sntprintf_s(msg, ESIZEOF(msg), _T("%s\n%s"), timebuf, rd->last_lv.c_str());
                                }
                                break;
                            case COLINDEX_MEMO:
                                _sntprintf_s(msg, ESIZEOF(msg), _T("%s"), rd->memo.c_str()); break;
                            }
                        }
                        regdata_info.unlock();
                    }

                    // リストビューツールチップの範囲と文字列を通知
                    RECT rect;
                    ListView_GetSubItemRect(hWndListView, lpnmlv->iItem, lpnmlv->iSubItem, LVIR_BOUNDS, &rect);

                    _dbg(_T("(%d,%d)-(%d,%d)\n"), rect.left, rect.top, rect.right, rect.bottom);
                    TOOLINFO ti;
                    memset(&ti, 0, sizeof(ti));
                    ti.cbSize = sizeof(TOOLINFO);
                    ti.hwnd = hWndListView;
                    ti.uId = 0;
                    ti.rect = rect;
                    ti.lpszText = msg;
                    SendMessage(hToolTipWnd, TTM_SETTOOLINFO, 0, (LPARAM)&ti);

                    olditem = lpnmlv->iItem;
                    oldsubitem = lpnmlv->iSubItem;
                    break;
                }

                /* カスタムドロー */
                if(pnmhdr->code == NM_CUSTOMDRAW){
                    NMLVCUSTOMDRAW *lplvcd = (NMLVCUSTOMDRAW *)lp;
                    switch(lplvcd->nmcd.dwDrawStage){
                    case CDDS_PREPAINT:
                        SetWindowLong(hDlgWnd, DWL_MSGRESULT, (LONG)(CDRF_NOTIFYITEMDRAW));
                        break;
                    case CDDS_ITEMPREPAINT:
                        SetWindowLong(hDlgWnd, DWL_MSGRESULT, (LONG)(CDRF_NOTIFYSUBITEMDRAW));
                        break;
                    case CDDS_ITEMPREPAINT|CDDS_SUBITEM:
                        {
                            LVITEM item;
                            memset(&item, 0, sizeof(item));
                            item.iItem = lplvcd->nmcd.dwItemSpec;
                            item.iSubItem = 0;
                            item.mask = LVIF_PARAM;

                            lplvcd->clrText   = default_color_tx;
                            lplvcd->clrTextBk = default_color_bg;
                            if(ListView_GetItem(pnmhdr->hwndFrom, &item)){
                                unsigned int dbidx = item.lParam;
                                time_t last_start = 0;

                                regdata_info.lock();
                                ITER(regdata_info) it = regdata_info.find(dbidx);
                                if(it != regdata_info.end()){
                                    last_start = it->second.last_start;
                                }
                                regdata_info.unlock();
                                // 放送開始から30分以内のアイテムに色を付ける
                                if(last_start + BROADCAST_PERIOD > time(NULL)){
                                    lplvcd->clrText   = startbc_color_tx;
                                    lplvcd->clrTextBk = startbc_color_bg;
                                }
                            }
                            if(iSortedIdx == (lplvcd->iSubItem+1) || -iSortedIdx == (lplvcd->iSubItem+1)){
                                // ソートカラムに色を付ける
                                lplvcd->clrText   = sortcol_color_tx;
                                lplvcd->clrTextBk = sortcol_color_bg;
                            }
                            // ペイント対象がアイコンカラムの場合、POSTPAINT通知を要求
                            if(lplvcd->iSubItem >= COLINDEX_BALLOON && lplvcd->iSubItem <= COLINDEX_EXTAPP){
                                SetWindowLong(hDlgWnd, DWL_MSGRESULT, (LONG)(CDRF_NEWFONT|CDRF_NOTIFYPOSTPAINT));
                            } else {
                                SetWindowLong(hDlgWnd, DWL_MSGRESULT, (LONG)(CDRF_NEWFONT));
                            }
                        }
                        break;

                    case CDDS_ITEMPOSTPAINT:
                        break;
                    case CDDS_ITEMPOSTPAINT|CDDS_SUBITEM:
                        // アイコンカラムのPOSTPAINT処理
                        if(lplvcd->iSubItem >= COLINDEX_BALLOON && lplvcd->iSubItem <= COLINDEX_EXTAPP){
                            LVITEM item;
                            memset(&item, 0, sizeof(item));
                            item.iItem = lplvcd->nmcd.dwItemSpec;
                            item.iSubItem = 0;
                            item.mask = LVIF_PARAM;
                            if(ListView_GetItem(pnmhdr->hwndFrom, &item)){
                                unsigned int dbidx = item.lParam;
                                unsigned int notify = 0;
                                regdata_info.lock();
                                ITER(regdata_info) it = regdata_info.find(dbidx);
                                if(it != regdata_info.end()){
                                    notify = it->second.notify;
                                }
                                regdata_info.unlock();

                                if( (lplvcd->iSubItem == COLINDEX_BALLOON && (notify & NOTIFY_BALLOON)) ||
                                    (lplvcd->iSubItem == COLINDEX_BROWSER && (notify & NOTIFY_BROWSER)) ||
                                    (lplvcd->iSubItem == COLINDEX_SOUND   && (notify & NOTIFY_SOUND  )) ||
                                    (lplvcd->iSubItem == COLINDEX_EXTAPP  && (notify & NOTIFY_EXTAPP )) )
                                {
                                    // 通知設定されているカラムにアイコンを描画
                                    RECT sirect;
                                    ListView_GetSubItemRect(pnmhdr->hwndFrom,
                                        lplvcd->nmcd.dwItemSpec, lplvcd->iSubItem, 
                                        LVIR_BOUNDS, &sirect);

                                    //_dbg(_T("%d,%d %d,%d\n"), sirect.left, sirect.top, sirect.right, sirect.bottom);
                                    int width = sirect.right - sirect.left;
                                    int height = sirect.bottom - sirect.top;
                                    int x = 0, y = 0;
                                    if(width  >= ICO_SW){ x = (width  - ICO_SW) / 2; }
                                    if(height >= ICO_SH){ y = (height - ICO_SH) / 2; }
                                    HICON hi = LoadIconRsc(IDI_NOTIFY);
                                    DrawIconEx(lplvcd->nmcd.hdc, sirect.left + x, sirect.top + y, hi,
                                        ICO_SW, ICO_SH, 0, NULL, DI_NORMAL);

                                    SetWindowLong(hDlgWnd, DWL_MSGRESULT, (LONG)CDRF_SKIPDEFAULT);
                                }
                            }
                        }
                        break;

                    }
                }
                // リストビューアイテム ダブルクリック
                if(pnmhdr->code == NM_DBLCLK){
                    _dbg(_T("NM_DBLCLK\n"));
                    int item_dc_select = ReadOptionInt(OPTION_ITEM_DC_SELECT, DEF_OPTION_ITEM_DC_SELECT);
                    switch(item_dc_select){
                    case 0:
                        PostMessage(hDlgWnd, WM_COMMAND, IDM_REGSET, 0);
                        break;
                    case 1:
                        PostMessage(hDlgWnd, WM_COMMAND, IDM_OPEN_LV, 0);
                        break;
                    case 2:
                        PostMessage(hDlgWnd, WM_COMMAND, IDM_OPEN_USER, 0);
                        break;
                    }
                }
            }
            else if(pnmhdr->hwndFrom == hWndLvHdr){
                // NMHEADER *phdn = (NMHEADER *)lp;
                // NMHDR   hdr;
                // int     iItem;
                // int     iButton;
                // HDITEMW *pitem;
                switch(pnmhdr->code){
                case HDN_ITEMCHANGED:       _dbg(_T("HDN_ITEMCHANGED\n"));      break;
                case HDN_TRACK:             _dbg(_T("HDN_TRACK\n"));            break;
                case HDN_BEGINTRACK:        _dbg(_T("HDN_BEGINTRACK\n"));       break;
                case HDN_ITEMCHANGING:      _dbg(_T("HDN_ITEMCHANGING\n"));     break;
                case NM_RELEASEDCAPTURE:    _dbg(_T("NM_RELEASEDCAPTURE\n"));   break;
                case NM_CUSTOMDRAW:         _dbg(_T("NM_CUSTOMDRAW\n"));        break;

                case HDN_ENDTRACK:
                    {
                        _dbg(_T("HDN_ENDTRACK\n"));
                        NMHEADER *phdn = (NMHEADER *)lp;
                        if(phdn->iItem != COLINDEX_ENABLE && 
                            !(phdn->iItem >= COLINDEX_BALLOON && phdn->iItem <= COLINDEX_EXTAPP) )
                        {
                            if(phdn->pitem->mask & HDI_WIDTH){
                                _dbg(_T("iItem = %d, cxy = %d\n"), phdn->iItem, phdn->pitem->cxy);
                                TCHAR szHeaderWidthKey[64];
                                _stprintf_s(szHeaderWidthKey, OPTION_HEADER_WIDTH_FORMAT, phdn->iItem);
                                SaveOptionInt(szHeaderWidthKey, phdn->pitem->cxy);
                            }
                        }
                    }
                    break;
                case HDN_DIVIDERDBLCLICK:
                    {
                        _dbg(_T("HDN_DIVIDERDBLCLICK\n"));
                        NMHEADER *phdn = (NMHEADER *)lp;
                        if(phdn->iItem != COLINDEX_ENABLE && 
                            !(phdn->iItem >= COLINDEX_BALLOON && phdn->iItem <= COLINDEX_EXTAPP) ){
                                _dbg(_T("iItem = %d, DBLCLK\n"), phdn->iItem);
                                TCHAR szHeaderWidthKey[64];
                                _stprintf_s(szHeaderWidthKey, OPTION_HEADER_WIDTH_FORMAT, phdn->iItem);
                                DeleteOption(szHeaderWidthKey); // AutoFit
                        }
                    }
                    break;

                default:
                    _dbg(_T("HEADER default: code = %08x\n"), pnmhdr->code);
                    break;
                }
            }
#if 0
            else if(pnmhdr->hwndFrom == hToolTipWnd){
                if(pnmhdr->code == TTN_GETDISPINFOW){
                    LPNMTTDISPINFO lpnmtdi = (LPNMTTDISPINFO)lp;
                }
            }
#endif
            else
            {
                _dbg(_T("Wnd = 0x%x : wp = 0x%x, lp = 0x%x\n"), pnmhdr->hwndFrom, wp, lp);
            }
        }
        break;

    case WM_DROPFILES:
        {
            HDROP hDrop = (HDROP)wp;
            // ドロップファイル数取得
            unsigned int num = DragQueryFile(hDrop, -1, NULL, 0);
            tstring regstr;

            for(unsigned int i = 0; i < num; i++){
                TCHAR path[_MAX_PATH+1];
                DragQueryFile(hDrop, i, path, sizeof(path));
                _dbg(_T("path = %s\n"), path);
                regstr += path; regstr += _T(" ");
            }
            DragFinish(hDrop);
            SendMessage(hDlgWnd, WM_OLEDROP, (WPARAM)regstr.c_str(), 0);
        }
        break;

    case WM_OLEDROP:
        {
            // ファイルおよびOLE D&D通知
            TCHAR *p = (TCHAR *)wp;
            unsigned int bufsize = _tcslen(p)+1;
            vector<TCHAR> buf(bufsize);
            memset(&buf[0], 0xff, bufsize * sizeof(TCHAR));
            _tcscpy_s(&buf[0], bufsize, p);
            unsigned int regnum = RegistrationInfoAnalyse(buf, FALSE, NULL);
            if(regnum > 0){
                unsigned int mindbidx = Registration(&buf[0]);
                if(mindbidx > 0){
                    initializing = true;
                    InsertListViewAdd(hWndListView, mindbidx);
                    con_add(mindbidx);
                    initializing = false;

                    //PostMessage(hDlgWnd, WM_ALINFO_NOTIFY, 0, TRUE);
                    ListView_EnsureVisible(hWndListView, dbidx2lvidx(hWndListView, mindbidx), FALSE);
                }
            }
        }
        break;

    case WM_LABELRELOAD:
        {
            _dbg(_T("WM_LABELRELOAD\n"));
            // ラベル名読み出し
            for(int i = 0; i < NICOALERT_LABEL_MAX; i++){
                sLabelName[i] = ReadLabelName(i);
            }

            LV_Lock(hWndListView);
            int num = ListView_GetItemCount(hWndListView);
            for(int i = 0; i < num; i++){
                ListView_SetItemText(hWndListView, i, COLINDEX_LABEL, LPSTR_TEXTCALLBACK);
            }
            LV_UnLock(hWndListView);
        }
        break;

    case WM_LABELCTL:
        {
            _dbg(_T("WM_LABELCTL\n"));
            BOOL mode = (BOOL)wp;
            int labelid = (int)lp;
            if(labelid < 0 || labelid >= NICOALERT_LABEL_MAX) break;
           
            if(!nadb.tr_begin()) break;

            // ラベル変更、リストビューおよびDBへ反映
            LV_Lock(hWndListView);
            regdata_info.lock();

            int lvidx = -1;
            while(1){
                lvidx = ListView_GetNextItem(hWndListView, lvidx, LVNI_ALL | LVNI_SELECTED);
                if(lvidx < 0) break;
                unsigned int dbidx = lvidx2dbidx(hWndListView, lvidx);
                if(dbidx > 0){
                    ITER(regdata_info) it = regdata_info.find(dbidx);
                    if(it != regdata_info.end()){
                        c_regdata *rd = &(it->second);
                        if(mode == LABEL_ADD){
                            rd->label |= (1 << labelid);
                        } else {
                            rd->label &= ( ~(1 << labelid) );
                        }
                        nadb.updateregdata(*rd);
                        UpdateListView(hWndListView, lvidx, rd);
                    }
                }
            }
            regdata_info.unlock();
            LV_UnLock(hWndListView);

            nadb.tr_commit();

            // リストビューソートの反映
            PostMessage(hDlgWnd, WM_ALINFO_NOTIFY, 0, FALSE);
        }
        break;

    case WM_LABELFILTER:
        {
            _dbg(_T("WM_LABELFILTER\n"));
            BOOL mode = (BOOL)wp;
            int labelid = (int)lp;
            if(labelid >= NICOALERT_LABEL_MAX) break;
            unsigned int labelmask = (1 << labelid);
            if(labelid < 0) labelmask = 0;

            // ラベルフィルタの適用。
            // 指定ラベルを持つエントリ以外をリストビューから削除
            LV_Lock(hWndListView);
            regdata_info.lock();

            int num = ListView_GetItemCount(hWndListView);
            for(int i = num-1; i >= 0; i--){
                unsigned int dbidx = lvidx2dbidx(hWndListView, i);
                if(dbidx == 0) continue;

                ITER(regdata_info) it = regdata_info.find(dbidx);
                if(it == regdata_info.end()) continue;
                c_regdata *rd = &(it->second);
                if( (labelmask == 0 && rd->label != 0) || (labelmask != 0 && !(rd->label & labelmask)) ){
                    ListView_DeleteItem(hWndListView, i);
                }
            }

            regdata_info.unlock();
            LV_UnLock(hWndListView);
        }
        break;

    case WM_CLEARFILTER:
        {
            _dbg(_T("WM_CLEARFILTER\n"));
            // ラベルフィルタの全解除(リストビュー再構築)
            UpdateListViewAll(hWndListView);
            // リストビューソートの反映
            PostMessage(hDlgWnd, WM_ALINFO_NOTIFY, 0, TRUE);
        }
        break;

    case WM_SIZING:
        {
            // ウィンドウサイズの最小制限
            LPRECT lprect = (LPRECT)lp;
            if ( (lprect->right - lprect->left) < WINDOWWIDTH ){
                switch(wp){
                case WMSZ_LEFT: case WMSZ_TOPLEFT: case WMSZ_BOTTOMLEFT:
                    lprect->left = lprect->right - WINDOWWIDTH; break;
                case WMSZ_RIGHT: case WMSZ_TOPRIGHT: case WMSZ_BOTTOMRIGHT:
                    lprect->right = lprect->left + WINDOWWIDTH; break;
                }
            }
            if ( (lprect->bottom - lprect->top) < WINDOWHEIGHT ){
                switch(wp){
                case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT:
                    lprect->top = lprect->bottom - WINDOWHEIGHT; break;
                case WMSZ_BOTTOM: case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOMRIGHT:
                    lprect->bottom = lprect->top + WINDOWHEIGHT; break;
                }
            }
            break;
        }

    case WM_SIZE:
        {
            // サイズ変更時、リストビューウィンドウ、右上部アイコンの追従
            // 移動先座標の保存。最大化、最小化時は無視。
            int nWidth = LOWORD(lp); 
            int nHeight = HIWORD(lp);
#if 0
            SetWindowPos(hWndListView, NULL, 0, STATUS_BAR_SIZE, nWidth, nHeight-STATUS_BAR_SIZE, SWP_NOZORDER);
            SetWindowPos(GetDlgItem(hDlgWnd, IDC_WRENCH), NULL, nWidth - ICO_BORDER_SW*2, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE/* | SWP_NOCOPYBITS */);
            SetWindowPos(GetDlgItem(hDlgWnd, IDC_BOOKMARK_PLUS), NULL, nWidth - ICO_BORDER_SW, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE/* | SWP_NOCOPYBITS */);
#else
            HDWP hDWP = BeginDeferWindowPos(5);
            DeferWindowPos(hDWP, hWndListView, NULL, 0, STATUS_BAR_SIZE, nWidth, nHeight-STATUS_BAR_SIZE, SWP_NOZORDER);
            DeferWindowPos(hDWP, GetDlgItem(hDlgWnd, IDC_MSG),           NULL, ICO_BORDER_SW, 0, nWidth - ICO_BORDER_SW*4, STATUS_BAR_SIZE, SWP_NOZORDER);
            DeferWindowPos(hDWP, GetDlgItem(hDlgWnd, IDC_WRENCH),        NULL, nWidth - ICO_BORDER_SW*3, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOCOPYBITS );
            DeferWindowPos(hDWP, GetDlgItem(hDlgWnd, IDC_BOOKMARK_PLUS), NULL, nWidth - ICO_BORDER_SW*2, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOCOPYBITS );
            DeferWindowPos(hDWP, GetDlgItem(hDlgWnd, IDC_LABELSET),      NULL, nWidth - ICO_BORDER_SW*1, 0, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOCOPYBITS );
            EndDeferWindowPos(hDWP);
#endif

            if(!IsZoomed(hDlgWnd) && !IsMinimized(hDlgWnd)){
                _dbg(_T("Window Resize : %d,%d\n"), nWidth, nHeight);
                SaveOptionInt(OPTION_WIDTH, nWidth);
                SaveOptionInt(OPTION_HEIGHT, nHeight);
            }
            break;
        }

    case WM_MOVE:
        {
            // 変更サイズの保存。最大化、最小化時は無視。
            if(!IsZoomed(hDlgWnd) && !IsMinimized(hDlgWnd)){
                RECT rect;
                GetWindowRect(hDlgWnd, &rect);
                _dbg(_T("Window Move : %d,%d\n"), rect.left, rect.top);
                SaveOptionInt(OPTION_WINDOW_XPOS, rect.left);
                SaveOptionInt(OPTION_WINDOW_YPOS, rect.top);
            }
        }

    case WM_SYSCOMMAND:
        if(wp == SC_CLOSE){
            // ウィンドウクローズ時の処理。
            // オプション設定に従い、終了確認または最小化処理を行う。
            if(ReadOptionInt(OPTION_MINIMIZE_ON_CLOSE, DEF_OPTION_MINIMIZE_ON_CLOSE)){
                PostMessage(hDlgWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                break;
            }
            if(ReadOptionInt(OPTION_CONFIRM_ON_CLOSE, DEF_OPTION_CONFIRM_ON_CLOSE)){
                if(MessageBox(hDlgWnd, _T("終了しますか？"), PROGRAM_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            }
            PostQuitMessage(0);
            break;
        }
        if(wp == SC_MINIMIZE){
            // 最小化時の処理。
            // オプション設定に従い、タスクトレイに入れる処理を行う。
            // (タイマセットし、遅延後にウィンドウHIDE)
            if(ReadOptionInt(OPTION_MINIMIZE_TO_TRAY, DEF_OPTION_MINIMIZE_TO_TRAY)){
                SetTimer(hDlgWnd, TID_MINIMIZE, 250, NULL);
            }
        }

        return FALSE;
        break;


    case WM_DESTROY:
        {
            // ウィンドウクローズ確定、終了処理
            if(hToolTipWnd) DestroyWindow(hToolTipWnd);

            // 各処理スレッド停止
            _dbg(_T("con_exit()\n"));
            con_exit();
            _dbg(_T("com_exit()\n"));
            cmm_exit();
            _dbg(_T("bcc_exit()\n"));
            bcc_exit();

            // タスクトレイアイコン消去
            nawnd_delicon();

            // 未保存設定のDB保存
            if(setting_info_darty){
                if(nadb.savesetting(setting_info)){
                    setting_info_darty = false;
                }
            }
            // DBクローズ、登録情報のメモリ解放
            CloseDataBase();

            // 処理メモリの解放
            msginfo.clear();
            g_last_lv.clear();

            // OLE D&Dの登録解除
            RevokeDragDrop(hDlgWnd);
            if(pDropTarget) pDropTarget->Release();
            OleUninitialize();

        }
        break;

    case WM_TIMER:
        switch(wp){
        case TID_SETTING:
            // 設定の定期保存タイマ(interval)
            if(setting_info_darty){
                if(nadb.savesetting(setting_info)){
                    setting_info_darty = false;
                }
            }
#if 0
            unsigned int count, period;
            if(bcc_startstat(&count, &period)){
                HWND hWnd = GetDlgItem(hDlgWnd, IDC_MSG);
                TCHAR msg[256];

                if(period > 0){
                    _dbg(_T("%u %u %u\n"), count, period, count * 1800 / period);
                    _stprintf_s(msg, _T("現在の放送数(推定):%u"), count * 1800 / period);
                    TOOLINFO ti;
                    memset(&ti, 0, sizeof(ti));
                    ti.cbSize = sizeof(TOOLINFO);
                    ti.hwnd = hDlgWnd;
                    ti.uId = (UINT_PTR)hWnd;
                    ti.lpszText = msg;
                    SendMessage(hToolTipWnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
                }
            }
#endif
            break;

        case TID_MINIMIZE:
            // タスクトレイに入れる際の遅延タイマ(one shot)
            KillTimer(hDlgWnd, TID_MINIMIZE);
            ShowWindow(hDlgWnd, SW_HIDE);
            break;

        case TID_REDRAW:
            InvalidateRect(hWndListView, NULL, FALSE);
            UpdateWindow(hWndListView);
            SetRedrawTimer(hDlgWnd);
            break;

        }

        break;

    default:
        if(msg == uiTaskbarCreated){
            // エクスプローラ再起動通知
            nawnd_addicon();
            break;
        }
        return FALSE;
    }
    return TRUE;
}
