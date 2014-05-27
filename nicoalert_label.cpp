//	�j�R���A���[�g(Love)
//	Copyright (C) 2012 naoki.kp

#include "nicoalert.h"
#include "nicoalert_label.h"
#include "nicoalert_wnd.h"

// �_�C�A���O�\�����n���h��
HWND hWndLabelDlg;

// ���X�g�r���[�p�J�����f�[�^
static const struct COLUMN{
    int width;
    int format;
    TCHAR *str;
} header[] = {
    {100, LVCFMT_LEFT, _T("���x����")},
};

// ���X�g�r���[�ɃJ�����ǉ�
static void InsertColumn(HWND hWndListView){
    int i;
    LV_COLUMN lvcol;

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
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
}

#define LISTVIEW_STRINGBUFFER 256 // �����񐶐��p�����o�b�t�@

// ���X�g�r���[�̑S�X�V
static void UpdateListViewAll(HWND hWndListView){

    LV_ITEM item;
    TCHAR szStringBuffer[LISTVIEW_STRINGBUFFER];

    LockWindowUpdate(hWndListView);
    ListView_DeleteAllItems(hWndListView);

    tstring labelname;
    for(int i = 0; i < NICOALERT_LABEL_MAX; i++){
        labelname = ReadLabelName(i);
        _stprintf_s(szStringBuffer, _T("%s"), labelname.c_str());

        item.iItem = i;
        item.mask = LVIF_TEXT;
        item.pszText = szStringBuffer;
        item.iSubItem = 0;
        item.lParam = 0;
        ListView_InsertItem(hWndListView, &item);
    }

    // �J�����̎�������
    int itemcount = Header_GetItemCount(ListView_GetHeader(hWndListView));
    for(int i = 0; i < itemcount; i++){
        ListView_SetColumnWidth(hWndListView, i, LVSCW_AUTOSIZE_USEHEADER);
    }

    LockWindowUpdate(NULL);
    ListView_Scroll(hWndListView, 0, 0);
}

// ���X�g�r���[ �T�u�N���X
static LRESULT CALLBACK ListViewHookProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp){
    switch (msg) {

    case WM_KEYDOWN:
        {
            switch(wp){
            case VK_F2:
            case VK_RETURN:
                int lvidx = ListView_GetNextItem(hWnd, -1, LVNI_SELECTED);
                if(lvidx < 0) break;
                ListView_EditLabel(hWnd, lvidx);
                break;
            }
        }
        break;

    case WM_NOTIFY:
        {
            // �������𐧌�
            HD_NOTIFY *phdn = (HD_NOTIFY *)lp;
            switch(phdn->hdr.code)
            {
            case HDN_ITEMCHANGING:
            case HDN_BEGINTRACK:
            case HDN_DIVIDERDBLCLICK:
                return TRUE;
                break;
            }
        }
        break;

    }
    FARPROC OrgWndProc = (FARPROC)GetWindowLong(hWnd, GWL_USERDATA);
    return CallWindowProc((WNDPROC)OrgWndProc, hWnd, msg, wp, lp);
}


BOOL CALLBACK LabelSetDlgProc(HWND hDlgWnd, UINT msg, WPARAM wp, LPARAM lp){
    static HWND hWndListView;
    static HWND hToolTipWnd = NULL;

    switch (msg) {
    case WM_INITDIALOG:
        {
            hWndLabelDlg = hDlgWnd;

            hWndListView = GetDlgItem(hDlgWnd, IDC_LS_LISTVIEW);

            // ���X�g�r���[�\�����e�ǉ�
            InsertColumn(hWndListView);
            UpdateListViewAll(hWndListView);
            //SetWindowPos(hDlgWnd, NULL, 0, 0, 320, 240, SWP_NOZORDER|SWP_NOMOVE);

            // ���X�g�r���[ �T�u�N���X��(�K�{:�J�����������)
            WindowSubClass(hWndListView, (FARPROC)ListViewHookProc);

            // �擪��I����Ԃɐݒ�
            ListView_SetItemState(hWndListView, 0, LVIS_SELECTED, LVIS_SELECTED);

            // �{�^���ɃA�C�R���ǉ�(Vista�ȏ�̂ݗL��)
            SendMessage(GetDlgItem(hDlgWnd, IDC_LS_LABELSET), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_TICK));
            SendMessage(GetDlgItem(hDlgWnd, IDC_LS_LABELUNSET), BM_SETIMAGE, IMAGE_ICON, (LPARAM)LoadIconRsc(IDI_CROSS));

            // �c�[���`�b�v�f�[�^�o�^
            if(ReadOptionInt(OPTION_TOOLTIP_HELP, DEF_OPTION_TOOLTIP_HELP)){
                ToolTipInfo uSettingDlgIds[] = {
                    { IDC_LS_LISTVIEW,      _T("�t�^/�������郉�x����I�����܂��B\n���x�����͕ύX�ł��܂��B") },
                    { IDC_LS_LABELSET,      _T("�I������Ă���o�^�A�C�e���ɁA���x����t�^���܂��B") },
                    { IDC_LS_LABELUNSET,    _T("�I������Ă���o�^�A�C�e������A���x�����������܂��B") },
                    { IDC_LS_LABELFILTER,   _T("�I�����ꂽ���x�������o�^�A�C�e���݂̂�\�����܂��B") },
                    { IDC_LS_CLEARFILTER,   _T("�t�B���^���������A���ׂĂ̓o�^�A�C�e����\�����܂��B") },
                };
                hToolTipWnd = ToolTipInit(hDlgWnd, hInst, uSettingDlgIds, ESIZEOF(uSettingDlgIds));
            }

        }
        break;

    case WM_NOTIFY:
        {
            NMHDR *pnmhdr = (NMHDR *)lp;
            if(pnmhdr->hwndFrom == hWndListView){
                /* �A�C�e���I��ύX */
                switch(pnmhdr->code){
                case LVN_BEGINLABELEDIT:
                    {
                        HWND hEdit = ListView_GetEditControl(hWndListView);
                        Edit_LimitText(hEdit, NICOALERT_LABEL_LEN_MAX);
                    }
                    break;

                case LVN_ENDLABELEDIT:
                    {
                        TCHAR buf[NICOALERT_LABEL_LEN_MAX+1];
                        NMLVDISPINFO *pdi = (NMLVDISPINFO *)lp; 
                        HWND hEdit = ListView_GetEditControl(hWndListView);
                        GetWindowText(hEdit, buf, sizeof(buf));
                        ListView_SetItemText(hWndListView, pdi->item.iItem, 0, buf);

                        TCHAR szLabelKey[64];
                        _stprintf_s(szLabelKey, OPTION_LABEL_FORMAT, pdi->item.iItem+1);
                        SaveOptionString(szLabelKey, buf);

                        SendMessage(GetParent(hDlgWnd), WM_LABELRELOAD, 0, 0);
                    }
                    break;
                }
            }
        }
        break;

    case WM_COMMAND:
        {
            WORD dlgid = LOWORD(wp);
            switch(dlgid){
            case IDC_LS_LABELSET:
            case IDC_LS_LABELUNSET:
                {
                    int lvidx = ListView_GetNextItem(hWndListView, -1, LVNI_SELECTED);
                    if(lvidx < 0) break;
                    PostMessage(GetParent(hDlgWnd), WM_LABELCTL,
                        (dlgid == IDC_LS_LABELSET ? LABEL_ADD : LABEL_DEL), lvidx);
                }
                break;

            case IDC_LS_LABELFILTER:
                {
                    int lvidx = ListView_GetNextItem(hWndListView, -1, LVNI_SELECTED);
                    PostMessage(GetParent(hDlgWnd), WM_LABELFILTER, 0, lvidx);
                }
                break;
            case IDC_LS_CLEARFILTER:
                PostMessage(GetParent(hDlgWnd), WM_CLEARFILTER, 0, 0);
                break;

            default:
                return FALSE;
            }
        }
        break;

    case WM_SYSCOMMAND:
        if (wp == SC_CLOSE){
            DestroyWindow(hDlgWnd);
            break;
        }

        return FALSE;
        break;

    case WM_DESTROY:
        if(hToolTipWnd) DestroyWindow(hToolTipWnd);
        hWndLabelDlg = NULL;
        break;

    default:
        return FALSE;
    }
    return TRUE;
}