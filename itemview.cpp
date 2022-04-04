#include "itemview.h"

#include "ResourceMine.h"

#include <strsafe.h>
#include <CommCtrl.h>

#include <ranges>
#include <algorithm>
#include <utility>
#include <unordered_map>

constexpr int TextLimit = 80;

struct ItemViewData
{
    HWND hWnd;
    std::vector<std::wstring> mItems;
    FnRenamed onRename;

    ItemViewData(HWND wnd, FnRenamed&& rename)
        : hWnd(wnd)
        , onRename(rename)
    {}
};

namespace {
    std::vector<std::shared_ptr<ItemViewData>> mViews;

    std::unordered_map<int,std::weak_ptr<ItemViewData>> mViewIds;

    UINT GetLVItemState(HWND hwnd, int i, UINT mask)
    {
        return ListView_GetItemState(hwnd, i, mask);
    }

    void GetLVItemText(HWND hwnd, int iItem, int iSubItem, LPTSTR pszText, int cchTextMax)
    {
        ListView_GetItemText(hwnd, iItem, iSubItem, pszText, cchTextMax);
    }

    void SetLVItemText(HWND hwnd, int i, int iSubItem, LPTSTR pszText)
    {
        ListView_SetItemText(hwnd, i, iSubItem, pszText);
    }


    BOOL GetLVItem(HWND hListView, UINT mask, int iItem, int iSubItem,
        LPLVITEM pitem, UINT stateMask)
    {
        pitem->mask = mask;
        pitem->stateMask = stateMask;
        pitem->iItem = iItem;
        pitem->iSubItem = iSubItem;
        return ListView_GetItem(hListView, pitem);
    }


    int GetHeaderItemCount(HWND hwndHD)
    {
        return Header_GetItemCount(hwndHD);
    }

    HWND GetLVHeaderControl(HWND hListView)
    {
        return ListView_GetHeader(hListView);
    }

    int GetLVColumnsCount(HWND hListView)
    {
        return (GetHeaderItemCount(GetLVHeaderControl(hListView)));
    }

    void SwapLVItems(HWND hListView, int iItem1, int iItem2)
    {
        constexpr size_t LOCAL_BUFFER_SIZE = 4096;
        LVITEM lvi1{}, lvi2{};

        UINT uMask = LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT | LVIF_PARAM | LVIF_STATE;

        std::vector<TCHAR> szBuffer1;
        szBuffer1.resize(LOCAL_BUFFER_SIZE + 1);

        std::vector<TCHAR> szBuffer2;
        szBuffer2.resize(LOCAL_BUFFER_SIZE + 1);

        lvi1.pszText = szBuffer1.data();
        lvi2.pszText = szBuffer2.data();
        lvi1.cchTextMax = (int)szBuffer1.size();
        lvi2.cchTextMax = (int)szBuffer2.size();

        BOOL bResult1 = GetLVItem(hListView, uMask, iItem1, 0, &lvi1, (UINT)-1);
        BOOL bResult2 = GetLVItem(hListView, uMask, iItem2, 0, &lvi2, (UINT)-1);

        if (bResult1 && bResult2)
        {
            lvi1.iItem = iItem2;
            lvi2.iItem = iItem1;
            lvi1.mask = uMask;
            lvi2.mask = uMask;
            lvi1.stateMask = (UINT)-1;
            lvi2.stateMask = (UINT)-1;
            //swap the items
            ListView_SetItem(hListView, &lvi1);
            ListView_SetItem(hListView, &lvi2);

            int iColCount = GetLVColumnsCount(hListView);
            //Loop for swapping each column in the items.
            for (int iIndex = 1; iIndex < iColCount; iIndex++)
            {
                szBuffer1[0] = '\0';
                szBuffer2[0] = '\0';
                GetLVItemText(hListView, iItem1, iIndex,
                    szBuffer1.data(), LOCAL_BUFFER_SIZE);
                GetLVItemText(hListView, iItem2, iIndex,
                    szBuffer2.data(), LOCAL_BUFFER_SIZE);
                SetLVItemText(hListView, iItem2, iIndex, szBuffer1.data());
                SetLVItemText(hListView, iItem1, iIndex, szBuffer2.data());
            }
        }
    }

    void RotateUp(HWND hListView)
    {
        int iCount = ListView_GetItemCount(hListView);

        for (int iIndex = 1; iIndex < iCount; iIndex++)
            SwapLVItems(hListView, iIndex, iIndex - 1);
    }

    void RotateDown(HWND hListView)
    {
        int iCount = ListView_GetItemCount(hListView);

        for (int iIndex = iCount - 1; iIndex >= 0; iIndex--)
            SwapLVItems(hListView, iIndex, iIndex + 1);
    }

    //Move up the selected items
    void MoveLVSelectedItemsUp(HWND hListView)
    {
        int iCount = ListView_GetItemCount(hListView);

        for (int iIndex = 1; iIndex < iCount; iIndex++)
            if (GetLVItemState(hListView, iIndex, LVIS_SELECTED) != 0)
                SwapLVItems(hListView, iIndex, iIndex - 1);

    }

    //Move down the selected items
    void MoveLVSelectedItemsDown(HWND hListView)
    {
        int iCount = ListView_GetItemCount(hListView);

        for (int iIndex = iCount - 1; iIndex >= 0; iIndex--)
            if (GetLVItemState(hListView, iIndex, LVIS_SELECTED) != 0)
                SwapLVItems(hListView, iIndex, iIndex + 1);

    }
}

int ViewNext()
{
    return IDC_LISTVIEW_START + (int)mViewIds.size();
}

void ViewTrack(int id, std::weak_ptr<ItemViewData> handle)
{
    mViewIds[id] = handle;
}

std::weak_ptr<ItemViewData> ViewHandle(int id)
{
    auto it = mViewIds.find(id);
    if (it == mViewIds.end())
        return std::weak_ptr<ItemViewData>();
    return (*it).second;
}

HWND ListViewGetHwnd(IVHandle h)
{
    if (h.expired())
        return 0;
    auto ptr = h.lock();
    return ptr->hWnd;
}

void ListViewGetItems(IVHandle h, std::vector<std::wstring>& items)
{
    if (h.expired())
        return;
    auto ptr = h.lock();

    std::copy((*ptr).mItems.begin(), (*ptr).mItems.end(), items.end());
}

BOOL ListViewRotateUp(IVHandle h)
{
    if (h.expired())
        return FALSE;

    auto ptr = h.lock();
    if (ptr->mItems.empty())
        return FALSE;

    if (ptr->mItems.size() == 1)
        return TRUE;

    RotateUp(ptr->hWnd);

    std::rotate(ptr->mItems.begin(), ptr->mItems.begin() + 1, ptr->mItems.end());

    return TRUE;
}

BOOL ListViewRotateDown(IVHandle h)
{
    if (h.expired())
        return FALSE;

    auto ptr = h.lock();
    if (ptr->mItems.empty())
        return FALSE;

    if (ptr->mItems.size() == 1)
        return TRUE;

    RotateDown(ptr->hWnd);

    std::rotate(ptr->mItems.begin(), ptr->mItems.end() - 1, ptr->mItems.end());

    return TRUE;
}

BOOL ListViewGetTop(IVHandle h, std::wstring& name)
{
    if (h.expired())
        return FALSE;

    auto ptr = h.lock();
    if (ptr->mItems.empty())
        return FALSE;

    name = ptr->mItems[0];
    return TRUE;
}

IVHandle ListViewCreate(HWND hwndParent, HINSTANCE hInst, FnRenamed&& rename)
{
    INITCOMMONCONTROLSEX icex;           // Structure for control initialization.
    icex.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    RECT rcClient;                       // The parent window's client area.

    GetClientRect(hwndParent, &rcClient);

    int nViewId = ViewNext();

    // Create the list-view window in report view with label editing enabled.
    HWND hWndListView = CreateWindow(WC_LISTVIEW,
        L"",
        WS_VISIBLE | WS_CHILD | WS_BORDER | LVS_REPORT | LVS_EDITLABELS | WS_EX_CLIENTEDGE,
        0, 0,
        rcClient.right - rcClient.left,
        rcClient.bottom - rcClient.top,
        hwndParent,
        (HMENU)(nViewId),
        hInst,
        NULL);

    if (!hWndListView)
        return IVHandle();

    LV_COLUMN lvC;
    TCHAR szText[MAX_PATH] = {};    // Place to store some text

    lvC.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvC.fmt = LVCFMT_LEFT;  // left align the column
    lvC.cx = rcClient.right - rcClient.left - 5;            // width of the column, in pixels
    lvC.pszText = szText;

    StringCchPrintf(szText, MAX_PATH, TEXT("Group"));

    if (ListView_InsertColumn(hWndListView, 0, &lvC) == -1)
        return IVHandle();

    auto& handle = mViews.emplace_back(std::make_shared<ItemViewData>(hWndListView, std::move(rename)));

    ListView_SetBkColor(hWndListView, 0xa9a9a9);

    ViewTrack(nViewId, handle);

    return handle;
}

BOOL ListViewDelItemTop(IVHandle h, std::wstring& deleted)
{
    if (h.expired())
        return FALSE;

    auto ptr = h.lock();

    if (ptr->mItems.empty())
        return FALSE;

    HWND hWnd = (*ptr).hWnd;

    if (!ListView_DeleteItem(hWnd, 0))
    {
        return FALSE;
    }

    deleted = ptr->mItems[0];

    ptr->mItems.erase(ptr->mItems.begin());
    return TRUE;
}

BOOL ListViewAddItemTop(IVHandle h, const std::wstring& text)
{
    if (h.expired())
        return FALSE;

    auto ptr = h.lock();

    HWND hWnd = (*ptr).hWnd;

    LV_ITEM lvI;
    lvI.mask = LVIF_TEXT | /*LVIF_IMAGE | LVIF_PARAM | */ LVIF_STATE;
    lvI.state = 0;
    lvI.stateMask = 0;

    lvI.iItem = 0;
    lvI.iSubItem = 0;
    lvI.pszText = LPSTR_TEXTCALLBACK;
    lvI.cchTextMax = TextLimit;

    (*ptr).mItems.insert((*ptr).mItems.begin(), text);

    if (ListView_InsertItem(hWnd, &lvI) == -1)
        return FALSE;

    return TRUE;
}

BOOL ListViewAddSecondItem(IVHandle h, const std::wstring& text)
{
    if (h.expired())
        return FALSE;

    auto ptr = h.lock();

    if (ptr->mItems.empty())
        return FALSE;

    HWND hWnd = (*ptr).hWnd;

    LV_ITEM lvI;
    lvI.mask = LVIF_TEXT | /*LVIF_IMAGE | LVIF_PARAM | */ LVIF_STATE;
    lvI.state = 0;
    lvI.stateMask = 0;

    lvI.iItem = 1;
    lvI.iSubItem = 0;
    lvI.pszText = LPSTR_TEXTCALLBACK;
    lvI.cchTextMax = TextLimit;

    (*ptr).mItems.insert((*ptr).mItems.begin() + 1, text);

    if (ListView_InsertItem(hWnd, &lvI) == -1)
        return FALSE;

    return TRUE;
}

LRESULT ListViewNotifyHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    NMLVDISPINFO* pLvdi = (NMLVDISPINFO*)lParam;

    //NM_LISTVIEW* pNm = (NM_LISTVIEW*)lParam;

    auto h = ViewHandle((int)(wParam));
    if (h.expired())
        return 0L;

    auto sh = h.lock();

    ItemViewData& data = (*sh);

    switch (pLvdi->hdr.code)
    {
    case LVN_GETDISPINFO:
    {
        std::wstring& name = data.mItems[pLvdi->item.iItem];
        switch (pLvdi->item.iSubItem)
        {
        case 0:     // Address
            pLvdi->item.pszText = name.data();
            break;
        default:
            break;
        }
    }
    break;

    case LVN_BEGINLABELEDIT:
    {
        std::wstring& name = data.mItems[pLvdi->item.iItem];
        HWND hWndEdit;

        // Get the handle to the edit box.
        hWndEdit = (HWND)SendMessage(hWnd, LVM_GETEDITCONTROL, 0, 0);
        // Limit the amount of text that can be entered.
        SendMessage(hWndEdit, EM_SETLIMITTEXT, (WPARAM)TextLimit, 0);
    }
    break;

    case LVN_ENDLABELEDIT:
    {
        std::wstring& name = data.mItems[pLvdi->item.iItem];
        // Save the new label information
        if ((pLvdi->item.iItem != -1) &&
            (pLvdi->item.pszText != NULL))
        {
            size_t len = 0;
            if (SUCCEEDED(StringCchLength(pLvdi->item.pszText, TextLimit, &len)))
            {
                std::wstring oldName = name;

                std::wstring newName;
                newName.resize(len + 1);

                name.clear();

                std::copy(pLvdi->item.pszText, pLvdi->item.pszText + len, newName.begin());

                if (std::ranges::find(data.mItems, newName) != std::end(data.mItems))
                {
                    name = oldName;
                    ListView_SetItemText(data.hWnd, pLvdi->item.iItem, 0, name.data());
                    return FALSE;
                }

                name = newName;

                data.onRename(oldName, name);

                ListView_SetItemText(data.hWnd, pLvdi->item.iItem, 0, name.data());

                return TRUE;
            }
        }
    }
    break;
    case LVN_INSERTITEM:
    {
        ListView_RedrawItems(data.hWnd, 0, data.mItems[pLvdi->item.iItem].size());
        UpdateWindow(data.hWnd);
        UpdateWindow(hWnd); /* the parent window */
    }
    break;
        /*
    case LVN_COLUMNCLICK:
        // The user clicked on one of the column headings - sort by
        // this column.
        ListView_SortItems(pNm->hdr.hwndFrom,
            ListViewCompareProc,
            (LPARAM)(pNm->iSubItem));
        break;
        */

    default:
        return 0L;
    }

    return 1L;
}