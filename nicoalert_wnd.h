
#ifndef NICOALERT_WND_H
#define NICOALERT_WND_H

// �c�[���`�b�v�p�Ǘ��f�[�^
struct ToolTipInfo {
    UINT uDlgItem;
    TCHAR *msg;
};

LRESULT CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
HWND ToolTipInit(HWND hDlgWnd, HINSTANCE hInstance, const ToolTipInfo *tti, int num);

#endif
