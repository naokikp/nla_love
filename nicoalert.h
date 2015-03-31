
#ifndef NICOALERT_H
#define NICOALERT_H

#define _WIN32_WINNT 0x501  // WINXP

#include <cstdio>
#include <cstring>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <process.h>
#include <commctrl.h>
#include <time.h>
#include <tchar.h>
#include "resource.h"
#include "mutex.hpp"

#include "nicoalert_ver.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

using namespace std;

extern HINSTANCE hInst;

#ifdef _DEBUG
//#define DEBUG_NOT_CONNECT
#endif

void _dbg(const TCHAR *fmt, ...);
void timefmt(TCHAR *p, int len, time_t t);
void doevent(void);

#define WINDOWWIDTH 480
#define WINDOWHEIGHT 240

#define ESIZEOF(A) (sizeof(A)/sizeof(A[0]))

typedef basic_string<TCHAR> tstring;

struct c_alertinfo {
    int infotype;
    tstring lvid;
    tstring coid;
    tstring chid;
    tstring usrid;
};

enum infotype {
    INFOTYPE_NODATA,
    INFOTYPE_USER,
    INFOTYPE_CHANNEL,
    INFOTYPE_OFFICIAL,

    INFOTYPE_IND_LVID,
    INFOTYPE_IND_COID,
    INFOTYPE_IND_CHID,
    INFOTYPE_IND_USRID,
};

struct c_streaminfo {
    tstring title;
    tstring desc;
};


struct c_regdata {
    unsigned int idx;
    tstring key;
    tstring key_name;
    tstring last_lv;
    time_t last_start;
    unsigned int notify;
    tstring memo;
    unsigned int label;
};

template <class T, class U> class map_ts : public map<T, U> {
private:
    mutex m;
public:
    void lock(){ m.lock(); }
    void unlock(){ m.unlock(); }
};
template <class T, class U> class unordered_map_ts : public unordered_map<T, U> {
private:
    mutex m;
public:
    void lock(){ m.lock(); }
    void unlock(){ m.unlock(); }
};
template <class T> class deque_ts : public deque<T> {
private:
    mutex m;
public:
    void lock(){ m.lock(); }
    void unlock(){ m.unlock(); }
};


typedef map_ts<tstring, tstring> setting;
typedef unordered_map_ts<unsigned int, c_regdata> regdata;
typedef map_ts<tstring, unsigned int> regdata_hash_t;


extern setting setting_info;
extern bool setting_info_darty;
extern regdata regdata_info;
extern regdata_hash_t regdata_hash;


tstring mb2ts(const char *buf);
string ts2mb(tstring &tstr);
void splitpath_opt(tstring &path, tstring &opt);
bool exec_browser(tstring &url);
bool exec_browser_by_key(tstring &key);

tstring ReadOptionString(const TCHAR *key, const TCHAR *default_val);
unsigned int ReadOptionInt(const TCHAR *key, unsigned int default_val);
void SaveOptionString(const TCHAR *key, const TCHAR *value);
void SaveOptionInt(const TCHAR *key, unsigned int value);
void DeleteOption(const TCHAR *key);
tstring ReadLabelName(unsigned int labelid);

HICON LoadIconRsc(WORD resid);
unsigned int GetOSVer(void);
bool CopyToClipBoard(const TCHAR *str);
bool WindowSubClass(HWND hWnd, FARPROC HookProc);

#ifdef _UNICODE
#define NICOALERT_CLIPBOARD_FORMAT  CF_UNICODETEXT
#else
#define NICOALERT_CLIPBOARD_FORMAT  CF_TEXT
#endif

#define ICO_SW      16
#define ICO_SH      16
#define ICO_BORDER  5
#define ICO_BORDER_SW   (ICO_SW + ICO_BORDER)
#define ICO_BORDER_SH   (ICO_SH + ICO_BORDER)

#define BROADCAST_PERIOD        (30*60)
#define STATUS_BAR_SIZE         22
#define MSGINFO_BACKUP          120

#define REGDATA_KEYNAME_MAXLEN  64
#define REGDATA_MEMO_MAXLEN     64

#define WM_ALINFO_NOTIFY        (WM_APP+1)
#define WM_TASKTRAY             (WM_APP+2)
#define WM_OLEDROP              (WM_APP+3)
#define WM_MSGINFO_UPDATE       (WM_APP+4)
#define WM_LABELRELOAD          (WM_APP+5)
#define WM_LABELCTL             (WM_APP+6)
#define WM_LABELFILTER          (WM_APP+7)
#define WM_CLEARFILTER          (WM_APP+8)

#define NOTIFY_ENABLE           0x00000001
#define NOTIFY_BALLOON          0x00000002
#define NOTIFY_BROWSER          0x00000004
#define NOTIFY_SOUND            0x00000008
#define NOTIFY_EXTAPP           0x00000010
#define NOTIFY_AUTOCOACQ        0x00010000

#define NICOLIVE_LVID_URL       _T("http://live.nicovideo.jp/watch/")
#define NICOLIVE_COID_URL       _T("http://com.nicovideo.jp/community/")
#define NICOLIVE_CHID_URL       _T("http://ch.nicovideo.jp/lives/")
#define NICOLIVE_USERID_URL     _T("http://www.nicovideo.jp/")

#define NICOALERT_ONLINE_HELP   _T("http://nlalove.web.fc2.com/")

// http://live.nicovideo.jp/recent/rss?&p=18

#define OPTION_BETA_VERSION_CONFIRM     _T("beta_version_confirm")

// スキーマバージョン
#define OPTION_DATABASE_VERSION         _T("db_ver")
#define OPTION_DATABASE_VERSION_DEF     1
#define OPTION_DATABASE_VERSION_NEW     2

// メイン画面設定情報
#define OPTION_WIDTH                    _T("width")
#define OPTION_HEIGHT                   _T("height")
#define OPTION_WINDOW_XPOS              _T("window_xpos")
#define OPTION_WINDOW_YPOS              _T("window_ypos")
#define OPTION_SORT_INDEX               _T("sort_index")
#define DEF_OPTION_SORT_INDEX           0

#define OPTION_HEADER_WIDTH_FORMAT      _T("header_width_%02u")


// 設定画面情報
#define OPTION_MINIMIZE_ON_CLOSE        _T("minimize_on_close")
#define OPTION_CONFIRM_ON_CLOSE         _T("confirm_on_close")
#define OPTION_MINIMIZE_TO_TRAY         _T("minimize_to_tray")
#define OPTION_MINIMIZE_ON_BOOT         _T("minimize_on_boot")
#define DEF_OPTION_MINIMIZE_ON_CLOSE    0
#define DEF_OPTION_CONFIRM_ON_CLOSE     1
#define DEF_OPTION_MINIMIZE_TO_TRAY     1
#define DEF_OPTION_MINIMIZE_ON_BOOT     0

#define OPTION_BALLOON_OPEN             _T("balloon_open")
#define OPTION_AUTO_KEYNAME_ACQ         _T("auto_keyname_acq")
#define OPTION_BALLOON_BOUYOMI          _T("balloon_bouyomi")
#define OPTION_TOOLTIP_HELP             _T("tooltip_help")
#define OPTION_ITEM_DC_ENABLE           _T("item_dc_enable")
#define OPTION_ITEM_DC_SELECT           _T("item_dc_select")
#define DEF_OPTION_BALLOON_OPEN         0
#define DEF_OPTION_AUTO_KEYNAME_ACQ     1
#define DEF_OPTION_BALLOON_BOUYOMI      0
#define DEF_OPTION_TOOLTIP_HELP         1
#define DEF_OPTION_ITEM_DC_ENABLE       0
#define DEF_OPTION_ITEM_DC_SELECT       0

#define OPTION_DEFAULT_NOTIFY           _T("default_notify") 
#define OPTION_BROWSER_PATH             _T("browser_path")
#define OPTION_DEFAULT_BROWSER_USE      _T("default_browser_use")
#define OPTION_SOUND_PATH               _T("sound_path")
#define OPTION_EXTAPP_PATH              _T("extapp_path")
#define OPTION_SOUND_VOL                _T("sound_vol")
#define OPTION_START_TXCOLOR            _T("start_txcolor")
#define OPTION_START_BGCOLOR            _T("start_bgcolor")
#define OPTION_LSORT_TXCOLOR            _T("lsort_txcolor")
#define OPTION_LSORT_BGCOLOR            _T("lsort_bgcolor")

#define DEF_OPTION_DEFAULT_NOTIFY       NOTIFY_BALLOON
#define DEF_OPTION_DEFAULT_BROWSER_USE  1
#define DEF_OPTION_SOUND_PATH           _T("")
#define DEF_OPTION_SOUND_VOL            80
#define DEF_OPTION_START_TXCOLOR        0x000000    // BBGGRR
#define DEF_OPTION_START_BGCOLOR        0xD0D0FF    // BBGGRR
#define DEF_OPTION_LSORT_TXCOLOR        0x000000    // BBGGRR
#define DEF_OPTION_LSORT_BGCOLOR        0xFFE0E0    // BBGGRR

// ラベル設定
#define OPTION_LABEL_FORMAT             _T("label_name_%02u")
#define DEF_OPTION_LABEL_FORMAT         _T("ラベル %02u")
#define NICOALERT_LABEL_MAX             16  // ラベル数
#define NICOALERT_LABEL_LEN_MAX         16  // ラベル最大文字数

// データ登録設定
#define OPTION_RA_ANMODE                _T("ra_anmode")
#define DEF_OPTION_RA_ANMODE            0

#define COID_PREFIX                     "co"
#define CHID_PREFIX                     "ch"
#define LVID_PREFIX                     "lv"
#define USERID_PREFIX                   "user/"
#define COID_PREFIX_LEN                 (strlen(COID_PREFIX))
#define CHID_PREFIX_LEN                 (strlen(CHID_PREFIX))
#define LVID_PREFIX_LEN                 (strlen(LVID_PREFIX))
#define USERID_PREFIX_LEN               (strlen(USERID_PREFIX))

enum TIMERID {
    TID_SETTING = 0,
    TID_MINIMIZE,
    TID_REDRAW,
};

enum COLINDEX {
    COLINDEX_ENABLE = 0,
    COLINDEX_KEY_NAME,
    COLINDEX_KEY,
    COLINDEX_LASTSTART,
    COLINDEX_BALLOON,
    COLINDEX_BROWSER,
    COLINDEX_SOUND,
    COLINDEX_EXTAPP,
    COLINDEX_MEMO,
    COLINDEX_LABEL,
};

#define PROCESS_EXIT_WAITCOUNT  10

#ifdef _MSC_VER
#define __typeof__ decltype
#endif
#define ITER(c) __typeof__((c).begin())
#define RITER(c) __typeof__((c).rbegin())
#define FOREACH(it, c) for (ITER(c) it=(c).begin(); it != (c).end(); ++it)
#define RFOREACH(it, c) for (RITER(c) it=(c).rbegin(); it != (c).rend(); ++it)

#endif
