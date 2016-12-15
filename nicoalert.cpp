//	ニコ生アラート(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_wnd.h"
#include "nicoalert_db.h"
#include "nicoalert_snd.h"

BOOL InitApp(HINSTANCE, LPCTSTR);

TCHAR IconName[]    = _T("APPICON");
TCHAR szClassName[] = _T("APPLICATIONCLASS_nicoalert");
TCHAR szMutexName[] = _T("APPLICATIONMUTEX_nicoalert") _DEBUGT("_DEBUG");

HINSTANCE hInst;
HWND hWnd;

setting setting_info;
bool setting_info_darty;
regdata regdata_info;
regdata_hash_t regdata_hash;


// デバッグ出力
void _dbg(const TCHAR *fmt, ...){
#ifdef _DEBUG
	TCHAR buf[1024];
    _stprintf_s(buf, _T("%u :"), GetTickCount());
	OutputDebugString(buf);
	va_list ap;
	va_start(ap, fmt);
	_vsntprintf_s(buf, ESIZEOF(buf)-1, fmt, ap);
	OutputDebugString(buf);
	va_end(ap);
#endif
}

// UTF-8からUTF-16へ変換
tstring mb2ts(const char *buf){
    tstring str;
#ifdef UNICODE
    TCHAR tbuf[1024];
    int ret = MultiByteToWideChar(CP_UTF8, 0, buf, -1, tbuf, ESIZEOF(tbuf));
    if(ret) str = tbuf;
#else
    str = buf;
#endif
    return str;
}

// UTF-16からUTF-8へ変換
string ts2mb(tstring &tstr){
    string str;
#ifdef UNICODE
    char buf[1024];
    int ret = WideCharToMultiByte(CP_UTF8, 0, tstr.c_str(), -1, buf, ESIZEOF(buf), NULL, NULL);
    if(ret) str = buf;
#else
    str = tstr;
#endif
    return str;
}

// 文字列先頭の空白文字を除去
void trim_leadws(tstring &s){
    while(!s.empty()){
        TCHAR c = s[0];
        if(c == ' ' || c == '\r' || c == '\n' || c == '\t'){
            s.erase(s.begin());
        } else break;
    }
}

// 文字列末尾の空白文字を除去
void trim_trailws(tstring &s){
    while(!s.empty()){
        TCHAR c = s[s.size()-1];
        if(c == ' ' || c == '\r' || c == '\n' || c == '\t'){
            s.erase(s.end()-1);
        } else break;
    }
}

// プログラム起動パスのオプション分離
void splitpath_opt(tstring &path, tstring &opt){
    opt = _T("");
    int loc = path.rfind(_T(".exe "));
    if(loc != tstring::npos){
        opt = path.substr(loc + 5, -1);
        path.replace(loc + 4, -1, _T(""));
    }
    _dbg(_T("path = %s, opt = %s\n"), path.c_str(), opt.c_str());
}

// ブラウザ起動
bool exec_browser(tstring &url){
    int ret = 0;
    tstring &p = ReadOptionString(OPTION_BROWSER_PATH, _T(""));
    if(ReadOptionInt(OPTION_DEFAULT_BROWSER_USE, DEF_OPTION_DEFAULT_BROWSER_USE) || p == _T("")){
        // デフォルトブラウザで起動
        ret = (int)ShellExecute(NULL, _T("open"), url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else {
        // 指定ブラウザで起動
        tstring opt;
        splitpath_opt(p, opt);
        opt += _T(" ");
        opt += url;
        ret = (int)ShellExecute(NULL, _T("open"), p.c_str(), opt.c_str(), NULL, SW_SHOWNORMAL);
    }
    if(ret < 32){
        return false; // 起動失敗
    }
    return true;
}

// 指定されたキーに対応するWEBページを開く
bool exec_browser_by_key(tstring &key){
    tstring param;
    if(key.compare(0, COID_PREFIX_LEN, _T(COID_PREFIX)) == 0){
        param = NICOLIVE_COID_URL;
    }
    else if(key.compare(0, CHID_PREFIX_LEN, _T(CHID_PREFIX)) == 0){
        param = NICOLIVE_CHID_URL;
    }
    else if(key.compare(0, USERID_PREFIX_LEN, _T(USERID_PREFIX)) == 0){
        param = NICOLIVE_USERID_URL;
    }
    else if(key.compare(0, LVID_PREFIX_LEN, _T(LVID_PREFIX)) == 0){
        param = NICOLIVE_LVID_URL;
    }
    param += key;

    return exec_browser(param);
}


// タイプスタンプ文字列取得
void timefmt(TCHAR *p, int len, time_t t){
    time_t tim = t;
    errno_t ecode = 0;
    tm ltm;
    if(t == 0){
        time(&tim);
    }
    ecode = localtime_s(&ltm, &tim);
    if(ecode != 0){
        _dbg(_T("errno = %d\n"), ecode);
        _stprintf_s(p, len, _T("不正時刻: %I64d"), t);
        return;
    }
    _stprintf_s(p, len, _T("%04d/%02d/%02d %02d:%02d:%02d"),
        ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
        ltm.tm_hour, ltm.tm_min, ltm.tm_sec);
}

// オプションパラメータ読み出し(文字列)
tstring ReadOptionString(const TCHAR *key, const TCHAR *default_val){
    tstring ret = default_val;
    setting_info.lock();
    ITER(setting_info) it = setting_info.find(key);
    if(it != setting_info.end()){
        _dbg(_T("%s = %s\n"), key, setting_info[key].c_str());
        ret = setting_info[key];
    }
    setting_info.unlock();
    return ret;
}

// オプションパラメータ読み出し(整数)
unsigned int ReadOptionInt(const TCHAR *key, unsigned int default_val){
    unsigned int ret = default_val;
    setting_info.lock();
    ITER(setting_info) it = setting_info.find(key);
    if(it != setting_info.end()){
        _dbg(_T("%s = %s\n"), key, setting_info[key].c_str());
        ret = _tcstoul(setting_info[key].c_str(), NULL, 10);
    } 
    setting_info.unlock();
    return ret;
}

// オプションパラメータ書き込み(文字列)
void SaveOptionString(const TCHAR *key, const TCHAR *value){
    setting_info.lock();
    if(setting_info[key] != value){
        setting_info[key] = value;
        setting_info_darty = true;
    }
    setting_info.unlock();
}

// オプションパラメータ書き込み(整数)
void SaveOptionInt(const TCHAR *key, unsigned int value){
    TCHAR buf[32];
    _stprintf_s(buf, _T("%u"), value);
    SaveOptionString(key, buf);
}

// オプションパラメータ削除
void DeleteOption(const TCHAR *key){
    setting_info.lock();
    if(setting_info.count(key)){
        setting_info.erase(key);
        setting_info_darty = true;
    }
    setting_info.unlock();
}

// ラベル名読み出し
tstring ReadLabelName(unsigned int labelid){
    TCHAR szLabelKey[64];
    TCHAR szLabelNameDef[64];
    tstring labelname;
    _stprintf_s(szLabelKey, OPTION_LABEL_FORMAT, labelid+1);
    _stprintf_s(szLabelNameDef, DEF_OPTION_LABEL_FORMAT, labelid+1);
    return ReadOptionString(szLabelKey, szLabelNameDef);
}


// アイコンリソース読み出し
HICON LoadIconRsc(WORD resid){
    return (HICON)LoadImage(hInst, MAKEINTRESOURCE (resid), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR | LR_SHARED);
}

// OSバージョン取得(WINNT系列のみ)
unsigned int GetOSVer(void){
    static unsigned int winver;
    if(winver != 0) return winver;

    OSVERSIONINFO osvi;
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if(GetVersionEx(&osvi) && osvi.dwPlatformId == VER_PLATFORM_WIN32_NT){
        winver = (osvi.dwMajorVersion << 8) + osvi.dwMinorVersion;
    }
    return winver;
}

// クリップボードにコピー
bool CopyToClipBoard(const TCHAR *str){
    unsigned int len = _tcslen(str);
    if(len == 0) return true;

    HGLOBAL hMemGA = NULL;
    TCHAR *hMem = NULL;
    unsigned int buflen = (len + 1) * sizeof(TCHAR);
    bool success = false;

    do {
        // グローバルメモリ取得
		hMemGA = GlobalAlloc(GMEM_MOVEABLE, buflen);
        if(!hMemGA) break;
        hMem = (TCHAR *)GlobalLock(hMemGA);
        if(hMem == NULL) break;
        memcpy(hMem, str, buflen);

   		if(!OpenClipboard(NULL)) break;
		if(!EmptyClipboard()) break;
        if(SetClipboardData(NICOALERT_CLIPBOARD_FORMAT, hMemGA) == NULL) break;
        hMemGA = hMem = NULL;
		CloseClipboard();

        success = true;
    } while(0);

    if(hMemGA){
        if(hMem) GlobalUnlock(hMemGA);
        GlobalFree(hMemGA);
    }
    return success;
}

// ウィンドウサブクラス化共通
// エディットボックスフック
bool WindowSubClass(HWND hWnd, FARPROC HookProc){
    FARPROC Org_WndProc;
    LONG ret;
    // 現在のウィンドウプロシージャを取得し、GWL_USERDATA に保存
    Org_WndProc = (FARPROC)GetWindowLongPtr(hWnd, GWL_WNDPROC);
    if(Org_WndProc == NULL) return false;

    SetLastError(0);
    ret = SetWindowLongPtr(hWnd, GWL_USERDATA, (LONG_PTR)Org_WndProc);
    if(ret == 0 && GetLastError() != 0) return false;

    // フックプロシージャを設定
    SetLastError(0);
    ret = SetWindowLong(hWnd, GWL_WNDPROC, (LONG)HookProc);
    if(ret == 0 && GetLastError() != 0) return false;

    return true;
}

// メッセージループ
void doevent(void){
	MSG msg;
	while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}


// エントリポイント
int WINAPI WinMain(HINSTANCE hCurInst, HINSTANCE hPrevInst, LPSTR lpsCmdLine, int nCmdShow){

    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);
    //_CrtSetBreakAlloc(229366);

    if(!hPrevInst){
		if(!InitApp(hCurInst, szClassName)){
			return FALSE;
        }
	}

	CreateMutex(NULL, FALSE, szMutexName);
	if(GetLastError() == ERROR_ALREADY_EXISTS){
		hWnd = FindWindow(szClassName, NULL);
		if (hWnd){
			SetForegroundWindow(GetLastActivePopup(hWnd));
		}
		return 0;
	}
	hInst = hCurInst;
	DialogBox(hInst, _T("DIALOG_MAIN"), NULL, (DLGPROC)MainDlgProc);

    _dbg(_T("process exit\n"));
    return(0);
}

BOOL InitApp(HINSTANCE hInst, LPCTSTR szClassName){
	WNDCLASS wc;
	wc.style = 0;//CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = DefDlgProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon(hInst, IconName);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = szClassName;
	return (RegisterClass(&wc));
}
