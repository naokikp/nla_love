
#ifndef NICOALERT_LABEL_H
#define NICOALERT_LABEL_H

#define OPTION_LABEL_FORMAT         _T("label_name_%02u")
#define DEF_OPTION_LABEL_FORMAT     _T("ƒ‰ƒxƒ‹ %02u")

#define NICOALERT_LABEL_MAX         16  // ƒ‰ƒxƒ‹”
#define NICOALERT_LABEL_LEN_MAX     16  // ƒ‰ƒxƒ‹Å‘å•¶š”

extern HWND hWndLabelDlg;

BOOL CALLBACK LabelSetDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp);

#define LABEL_ADD   1
#define LABEL_DEL   0

#endif
