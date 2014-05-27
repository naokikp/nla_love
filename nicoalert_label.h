
#ifndef NICOALERT_LABEL_H
#define NICOALERT_LABEL_H

extern HWND hWndLabelDlg;

BOOL CALLBACK LabelSetDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp);

#define LABEL_ADD   1
#define LABEL_DEL   0

#endif
