#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

struct ItemViewData;

using IVHandle = std::weak_ptr<ItemViewData>;

using FnRenamed = std::function<void(const std::wstring& oldName, const std::wstring& newName)>;

IVHandle ListViewCreate(HWND hwndParent, HINSTANCE hInst, FnRenamed&& rename);

HWND ListViewGetHwnd(IVHandle);
void ListViewGetItems(IVHandle, std::vector<std::wstring>& items);

LRESULT ListViewNotifyHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL ListViewAddItemTop(IVHandle h, const std::wstring& text);
BOOL ListViewAddSecondItem(IVHandle h, const std::wstring& text);
BOOL ListViewDelItemTop(IVHandle h, std::wstring& deleted);


BOOL ListViewRotateUp(IVHandle h);
BOOL ListViewRotateDown(IVHandle h);

BOOL ListViewGetTop(IVHandle h, std::wstring& name);