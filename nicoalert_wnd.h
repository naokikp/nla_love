
#ifndef NICOALERT_WND_H
#define NICOALERT_WND_H

// ツールチップ用管理データ
struct ToolTipInfo {
    UINT uDlgItem;
    TCHAR *msg;
};

#ifdef _DEBUG
#define IDM_DEBUGNOTIFY 10000
#define IDM_DEBUGPOPUP  10001
#endif

enum {
    POPUPLOC_X = 1<<0,
    POPUPLOC_Y = 1<<1,
};

#define MSGPOPUPWINDOW_WIDTH        320
#define MSGPOPUPWINDOW_HEIGHT       320
#define MSGPOPUPWINDOW_MARGIN       8
#define MSGPOPUPWINDOW_ROUND_SIZE   9
#define MSGPOPUPWINDOW_ROUND_SIZE2  7

#define MSGPOPUPWINDOWMAX 12
#define MSGPOPUPWINDOW_FONT _T("メイリオ")
#define MSGPOPUPWINDOW_FONT_HEIGHT 11
#define MSGPOPUPWINDOW_TEXT_MARGIN 8

#define MSGPOPUPWINDOW_EDGE_COLOR           RGB(128,128,128)
#define MSGPOPUPWINDOW_EDGE_COLOR2          RGB(192,192,192)

#define MSGPOPUP_CLOSE_TIME         10000
#define MSGPOPUP_FADEIN_TIME        1000
#define MSGPOPUP_FADEINFAST_TIME    0
#define MSGPOPUP_FADEOUT_TIME       2000
#define MSGPOPUP_FADEOUTFAST_TIME   200
#define MSGPOPUP_FADE_DIV           50

struct MsgPopupWindowInfo_t {
    bool busy;
    int x, y, w, h;
    int popuploc;
    tstring msg, link;
    bool fade_dir;
    int fade_num;
    bool closing;
    HDC hDC;
    HBITMAP hBmp;
};

LRESULT CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND ToolTipInit(HWND hDlgWnd, HINSTANCE hInstance, const ToolTipInfo *tti, int num);

#endif
