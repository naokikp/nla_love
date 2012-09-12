
#ifndef NICOALERT_WND_H
#define NICOALERT_WND_H

// ツールチップ用管理データ
struct ToolTipInfo {
    UINT uDlgItem;
    TCHAR *msg;
};

LRESULT CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND ToolTipInit(HWND hDlgWnd, HINSTANCE hInstance, const ToolTipInfo *tti, int num);

#endif
