#include <windows.h>
#include <strsafe.h>
#include <assert.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iterator>
#include <functional>
#include <utility>
#include <sstream>

#include <inttypes.h>

#include "ResourceMine.h"

#include "itemview.h"
#include "virtdesktop2.h"

#include <iostream>

using namespace std;

enum class Cmd
{
	MoveAway = 1,
	MoveAllAway,
	MoveBack,
	MoveSwap,
	RestoreTo,
	NextDesktop,
	PrevDesktop,
	NextGroup,
	PrevGroup,
	NewGroup,
	DeleteGroup,
};

struct scope_guard
{
private:
	std::function<void()> run;
	bool active;
public:

	scope_guard(std::function<void()>&& fn)
		: run(std::move(fn))
		, active(true)
	{
	}

	void Dismiss()
	{
		active = false;
	}

	~scope_guard()
	{
		if (active)
		{
			auto r = std::move(run);
			r();
		}
	}
};

void ShowLastError(LPCTSTR lpszFunction)
{
  LPVOID lpMsgBuf = NULL;
  LPVOID lpDisplayBuf = NULL;
  DWORD dw = GetLastError(); 

  FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | 
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      dw,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR) &lpMsgBuf,
      0, NULL );

  // Display the error message and exit the process

  lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
      (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));

  assert(lpDisplayBuf);

  StringCchPrintf((LPTSTR)lpDisplayBuf, 
      LocalSize((HLOCAL)lpDisplayBuf) / sizeof(TCHAR),
      TEXT("%s failed with error %d: %s"), 
      lpszFunction, dw, (LPCTSTR)lpMsgBuf); 

  MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);
	
  LocalFree(lpMsgBuf);
  LocalFree(lpDisplayBuf);
}

namespace std {
	template<> struct hash<GUID>
	{
		size_t operator()(const GUID& guid) const noexcept {
			const std::uint64_t* p = reinterpret_cast<const std::uint64_t*>(&guid);
			std::hash<std::uint64_t> hash;
			return hash(p[0]) ^ hash(p[1]);
		}
	};
}

namespace {
	constexpr size_t MaxMoveHistory = 15;

	std::unordered_map<std::wstring, std::vector<HWND>> m_Groups;

	std::vector<HWND> m_Moved;

	HWND m_hWnd;
	IVHandle m_hList;

	IServiceProvider* pServiceProvider = NULL;
	IApplicationViewCollection* viewCollection = NULL;
	IVirtualDesktopManager* pDesktopManager = NULL;
	IVirtualDesktopManagerInternal* pDesktopManagerInternal = NULL;

	HINSTANCE mHInstance;
}

void MoveToScratch(HWND hWin, BOOL track = FALSE);
void MoveToCurrent(HWND hWin);


size_t WrapIdx(size_t curIdx, size_t max, int dir)
{
	int idx = ((int)curIdx) + dir;
	while (idx < 0) { idx += (int)max; }
	while (idx >= max) { idx -= (int)max; }
	return (size_t)idx;
}

void MoveDesktop(int dir)
{
	IVirtualDesktop* current = nullptr;
	if (!SUCCEEDED(pDesktopManagerInternal->GetCurrentDesktop(&current))) return;
	GUID currentId{ 0 };
	if (!SUCCEEDED(current->GetID(&currentId))) return;

	IObjectArray* pObjectArray = nullptr;
	if (!SUCCEEDED(pDesktopManagerInternal->GetDesktops(&pObjectArray))) return;
	UINT count = 0;
	if (!SUCCEEDED(pObjectArray->GetCount(&count))) return;

	std::vector<IVirtualDesktop*> desktops;

	IVirtualDesktop* pCur = nullptr;
	UINT curIdx = 0;
	for (UINT i = 0; i < count; i++)
	{
		if (FAILED(pObjectArray->GetAt(i, __uuidof(IVirtualDesktop), (void**)&pCur)))
			continue;
		GUID id = { 0 };
		if (FAILED(pCur->GetID(&id)))
			continue;

		desktops.push_back(pCur);
		if (id == currentId)
		{
			curIdx = i;
		}
	}

	IVirtualDesktop* pTarget = NULL;

	pTarget = desktops[WrapIdx(curIdx, desktops.size(), dir)];

	pDesktopManagerInternal->SwitchDesktop(pTarget);
}

void NextDesktop()
{
	MoveDesktop(1);
}

void PrevDesktop()
{
	MoveDesktop(-1);
}

BOOL CALLBACK EnumCurrent(HWND hwnd, LPARAM lparam)
{
	BOOL onDesk = FALSE;
	if (SUCCEEDED(pDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &onDesk)))
	{
		if (!onDesk)
			return TRUE;

		std::vector<HWND>& wins = *(std::vector<HWND>*)lparam;

		wins.push_back(hwnd);
	}

	return TRUE;
}

BOOL CALLBACK EnumNotCurrent(HWND hwnd, LPARAM lparam)
{
	BOOL onDesk = FALSE;
	if (SUCCEEDED(pDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &onDesk)))
	{
		if (onDesk)
			return TRUE;

		std::vector<HWND>& wins = *(std::vector<HWND>*)lparam;

		wins.push_back(hwnd);
	}

	return TRUE;
}

std::wstring NextGroupName()
{
	static int agroupidx = 0;

	std::wstring ret;

	while (ret.empty() || m_Groups.find(ret) != m_Groups.end())
	{
		std::wstringstream str;
		str << TEXT("AutoGroup") << (++agroupidx);
		ret = str.str();
	}

	return ret;
}

void ShowTopGroup()
{
	std::wstring name;
	auto success = ListViewGetTop(m_hList, name);
	assert(success);

	auto it = m_Groups.find(name);
	assert(it != m_Groups.end());

	std::vector<HWND> currentWin;

	EnumWindows(EnumCurrent, (LPARAM)&currentWin);

	std::vector<HWND>& showWin = (*it).second;

	for (const auto& hwnd : currentWin)
	{
		if (std::ranges::find(showWin, hwnd) == showWin.end())
			MoveToScratch(hwnd);
	}

	for (const auto& hwnd : showWin)
	{
		if (std::ranges::find(currentWin, hwnd) == currentWin.end())
			MoveToCurrent(hwnd);
	}
}

void MoveGroup(int dir)
{
	// all top level windows go into the current window
	std::wstring name;
	if (!ListViewGetTop(m_hList, name))
	{
		name = NextGroupName();
		ListViewAddItemTop(m_hList, name);
	}

	auto it = m_Groups.find(name);

	if (m_Groups.find(name) == m_Groups.end())
	{
		auto added = m_Groups.emplace(
			std::piecewise_construct,
			std::forward_as_tuple(name),
			std::forward_as_tuple<std::vector<HWND>>({})
		);

		assert(added.second);
		it = added.first;
	}

	(*it).second.clear();

	EnumWindows(EnumCurrent, (LPARAM) & ((*it).second));

	if (m_Groups.size() < 2) // nothing to rotate to
		return;

	for (;dir > 0; --dir)
		if (!ListViewRotateUp(m_hList))
		{
			MessageBox(NULL, TEXT("Failed to rotate Lists"), NULL, MB_OK | MB_ICONERROR);
			return;
		}

	for (;dir < 0; ++dir)
		if (!ListViewRotateDown(m_hList))
		{
			MessageBox(NULL, TEXT("Failed to rotate Lists"), NULL, MB_OK | MB_ICONERROR);
			return;
		}

	name.clear();
	if (!ListViewGetTop(m_hList, name))
	{
		MessageBox(NULL, TEXT("Failed to get top after rotate Lists"), NULL, MB_OK | MB_ICONERROR);
		return;
	}

	auto itG = m_Groups.find(name);
	if (itG == m_Groups.end())
	{
		MessageBox(NULL, TEXT("Groups out of sync"), NULL, MB_OK | MB_ICONERROR);
		return;
	}

	if (!(*itG).second.empty())
	{
		for (const auto& hwnd : (*it).second)
		{
			if (std::ranges::find( (*itG).second, hwnd) == (*itG).second.end())
				MoveToScratch(hwnd);
		}

		for (const auto& hwnd : (*itG).second)
		{
			if (std::ranges::find((*it).second, hwnd) == (*it).second.end())
				MoveToCurrent(hwnd);
		}
	}

	// in the case of the target group being empty we'll keep the same windows
}

void NextGroup()
{
	MoveGroup(1);
}

void PrevGroup()
{
	MoveGroup(-1);
}

void DeleteGroup()
{
	std::wstring name;

	if (!ListViewDelItemTop(m_hList, name))
	{
		MessageBox(NULL, TEXT("No group to delete"), NULL, MB_OK | MB_ICONERROR);
		return;
	}

	auto it = m_Groups.find(name);
	assert(it != m_Groups.end());
	m_Groups.erase(it);

	ShowTopGroup();
}

void NewGroup()
{
	// Capture current
	if (!m_Groups.empty())
	{
		std::wstring top;
		auto success = ListViewGetTop(m_hList, top);
		assert(success);

		auto& windows = m_Groups[top];
		windows.clear();

		EnumWindows(EnumCurrent, (LPARAM)&windows);
	}

	// Make new
	auto name = NextGroupName();
	if (!ListViewAddItemTop(m_hList, name))
	{
		MessageBox(NULL, TEXT("Failed to allocate new group"), NULL, MB_OK | MB_ICONERROR);
		return;
	}

	auto added = m_Groups.emplace(
		std::piecewise_construct,
		std::forward_as_tuple(name),
		std::forward_as_tuple<std::vector<HWND>>({})
	);

	assert(added.second);
}

void MoveWinToDesktop(HWND hWin, IVirtualDesktop* pTarget)
{
	IApplicationView* app = NULL;
	if (!SUCCEEDED(viewCollection->GetViewForHwnd(hWin, &app))) return;
	if (!SUCCEEDED(pDesktopManagerInternal->MoveViewToDesktop(app, pTarget))) return;
}

void RestoreScratched()
{
	IVirtualDesktop* current = nullptr;
	if (!SUCCEEDED(pDesktopManagerInternal->GetCurrentDesktop(&current))) return;
	GUID currentId{ 0 };
	if (!SUCCEEDED(current->GetID(&currentId))) return;

	std::vector<HWND> list;

	EnumWindows(EnumNotCurrent, (LPARAM)&list);

	for (const auto& item : list)
	{
		MoveToCurrent(item);
	}
}

void MoveToCurrent(HWND hWin)
{
	if (hWin == m_hWnd) return;

	BOOL onDesk = FALSE;
	if (!SUCCEEDED(pDesktopManager->IsWindowOnCurrentVirtualDesktop(hWin, &onDesk))) return;
	if (onDesk) return;

	IVirtualDesktop* current = nullptr;
	if (!SUCCEEDED(pDesktopManagerInternal->GetCurrentDesktop(&current))) return;
	GUID currentId{ 0 };
	if (!SUCCEEDED(current->GetID(&currentId))) return;

	IApplicationView* app = NULL;
	if (!SUCCEEDED(viewCollection->GetViewForHwnd(hWin, &app))) return;
	if (!SUCCEEDED(pDesktopManagerInternal->MoveViewToDesktop(app, current))) return;
}

void MoveBackFromOther()
{
	if (m_Moved.empty())
		return;
	MoveToCurrent(*m_Moved.end() - 1);
	m_Moved.erase(m_Moved.end() - 1);
}

void MoveAllToOther()
{
	std::vector<HWND> list;
	EnumWindows(EnumCurrent, (LPARAM)&list);
	for (const auto& win : list)
	{
		MoveToScratch(win, FALSE);
	}
}

void MoveToScratch(HWND hWin, BOOL track)
{
	if (hWin == m_hWnd) return; // ignore ourself
	//if (m_Desktops.size() < 2) return; // if we don't have enough desktops

	IVirtualDesktop* current = nullptr;
	if (!SUCCEEDED(pDesktopManagerInternal->GetCurrentDesktop(&current))) return;
	GUID currentId{ 0 };
	if (!SUCCEEDED(current->GetID(&currentId))) return;

	IObjectArray* pObjectArray = nullptr;
	if (!SUCCEEDED(pDesktopManagerInternal->GetDesktops(&pObjectArray))) return;
	UINT count = 0;
	if (!SUCCEEDED(pObjectArray->GetCount(&count))) return;

	IVirtualDesktop* pTarget = nullptr;
	for (UINT i = 0; i < count; i++)
	{
		if (FAILED(pObjectArray->GetAt(i, __uuidof(IVirtualDesktop), (void**)&pTarget)))
			continue;
		GUID id = { 0 };
		if (SUCCEEDED(pTarget->GetID(&id)) && id == currentId)
			continue;
		break;
	}

	if (!pTarget) return;

	IApplicationView* app = NULL;
	if (!SUCCEEDED(viewCollection->GetViewForHwnd(hWin, &app))) return;
	if (!SUCCEEDED(pDesktopManagerInternal->MoveViewToDesktop(app, pTarget))) return;

	if (track)
	{
		while (m_Moved.size() > MaxMoveHistory)
			m_Moved.erase(m_Moved.begin());

		m_Moved.push_back(hWin);
	}
}

void MoveSwap()
{
	std::vector<HWND> current;
	EnumWindows(EnumCurrent, (LPARAM)&current);

	std::vector<HWND> notcurrent;
	EnumWindows(EnumNotCurrent, (LPARAM)&notcurrent);

	for (const auto& hwnd : current)
	{
		MoveToScratch(hwnd);
	}

	for (const auto& hwnd : notcurrent)
	{
		MoveToCurrent(hwnd);
	}
}

void DestoryScratchDesktop()
{
	if (pDesktopManagerInternal)
	{
		pDesktopManagerInternal->Release();
		pDesktopManagerInternal = NULL;
	}
	if (pDesktopManager)
	{
		pDesktopManager->Release();
		pDesktopManager = NULL;
	}
	if (viewCollection)
	{
		viewCollection->Release();
		viewCollection = NULL;
	}
	if (pServiceProvider)
	{
		pServiceProvider->Release();
		pServiceProvider = NULL;
	}
	CoUninitialize();
}

bool CreateScratchDesktop(HWND hWin)
{
	HRESULT hr;

	hr = ::CoInitialize(NULL);
	if (!SUCCEEDED(hr))
	{
		return false;
	}

	scope_guard guard([]() {DestoryScratchDesktop();});

	if (!SUCCEEDED(::CoCreateInstance(CLSID_ImmersiveShell, NULL, CLSCTX_LOCAL_SERVER, __uuidof(IServiceProvider), (PVOID*)&pServiceProvider)))
	{
		return false;
	}

	if (!SUCCEEDED(pServiceProvider->QueryService(__uuidof(IApplicationViewCollection), &viewCollection)))
	{
		return false;
	}

	if (!SUCCEEDED(pServiceProvider->QueryService(__uuidof(IVirtualDesktopManager), &pDesktopManager)))
	{
		return false;
	}

	if (!SUCCEEDED(pServiceProvider->QueryService(CLSID_VirtualDesktopManagerInternal, __uuidof(IVirtualDesktopManagerInternal), (PVOID*)&pDesktopManagerInternal)))
	{
		return false;
	}

	if (hr != S_OK)
	{
		return false;
	}

	guard.Dismiss();

	return true;
}

void OnRename(const std::wstring& oldName, const std::wstring& newName)
{
	auto it = m_Groups.find(oldName);
	assert(it != m_Groups.end());
	auto newIt = m_Groups.find(newName);
	assert(newIt == m_Groups.end());
	m_Groups[newName] = std::move((*it).second);
	m_Groups.erase(it);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
		case WM_CREATE:
		{
			m_hList = ListViewCreate(hWnd, mHInstance, OnRename);
			if (!ListViewGetHwnd(m_hList))
				MessageBox(NULL, TEXT("Listview not created!"), NULL, MB_OK);
		} break;
		case WM_NOTIFY:
			if (!ListViewNotifyHandler(hWnd, msg, wParam, lParam))
			{
				return DefWindowProc(hWnd, msg, wParam, lParam);
			}
		case WM_SIZE:
		{
			HWND hList = ListViewGetHwnd(m_hList);
			if (hList)
			{
				MoveWindow(hList, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
			}
		} break;
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case ID_QUIT:
					DestroyWindow(hWnd);
					return 0;
			}
		} break;
		case WM_CLOSE:
		{
			DestroyWindow(hWnd);
			return 0;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

void SwitchToAnchorDesktop()
{
	GUID anchorDesk = { 0 };

	if (FAILED(pDesktopManager->GetWindowDesktopId(m_hWnd, &anchorDesk)))
		return;

	IVirtualDesktop* pCur = nullptr;
	GUID id = { 0 };
	if (FAILED(pDesktopManagerInternal->GetCurrentDesktop(&pCur)))
		return;
	if (FAILED(pCur->GetID(&id)))
		return;

	if (anchorDesk == id)
		return;

	IObjectArray* pObjectArray = nullptr;
	if (FAILED(pDesktopManagerInternal->GetDesktops(&pObjectArray))) return;
	UINT count = 0;
	if (FAILED(pObjectArray->GetCount(&count))) return;

	std::vector<IVirtualDesktop*> desktops;

	for (UINT i = 0; i < count; i++)
	{
		if (FAILED(pObjectArray->GetAt(i, __uuidof(IVirtualDesktop), (void**)&pCur)))
			continue;
		if (FAILED(pCur->GetID(&id)))
			continue;

		if (anchorDesk == id)
		{
			pDesktopManagerInternal->SwitchDesktop(pCur);
			break;
		}
	}
}

void BindHotKeys()
{
	UnregisterHotKey(NULL, (UINT)Cmd::MoveAllAway);
	UnregisterHotKey(NULL, (UINT)Cmd::MoveAway);
	UnregisterHotKey(NULL, (UINT)Cmd::MoveBack);
	UnregisterHotKey(NULL, (UINT)Cmd::MoveSwap);
	UnregisterHotKey(NULL, (UINT)Cmd::RestoreTo);
	UnregisterHotKey(NULL, (UINT)Cmd::PrevDesktop);
	UnregisterHotKey(NULL, (UINT)Cmd::NextDesktop);
	UnregisterHotKey(NULL, (UINT)Cmd::NextGroup);
	UnregisterHotKey(NULL, (UINT)Cmd::PrevGroup);
	UnregisterHotKey(NULL, (UINT)Cmd::NewGroup);
	UnregisterHotKey(NULL, (UINT)Cmd::DeleteGroup);

	RegisterHotKey(NULL, (UINT)Cmd::MoveAllAway, MOD_ALT | MOD_NOREPEAT, 'Q');
	RegisterHotKey(NULL, (UINT)Cmd::MoveAway, MOD_ALT | MOD_NOREPEAT, 'X');
	RegisterHotKey(NULL, (UINT)Cmd::MoveBack, MOD_ALT | MOD_NOREPEAT, 'Z');
	RegisterHotKey(NULL, (UINT)Cmd::MoveSwap, MOD_ALT | MOD_NOREPEAT, 'P');
	RegisterHotKey(NULL, (UINT)Cmd::RestoreTo, MOD_ALT | MOD_NOREPEAT, 'R');
	RegisterHotKey(NULL, (UINT)Cmd::PrevDesktop, MOD_ALT | MOD_NOREPEAT, '3');
	RegisterHotKey(NULL, (UINT)Cmd::NextDesktop, MOD_ALT | MOD_NOREPEAT, '4');
	RegisterHotKey(NULL, (UINT)Cmd::NextGroup, MOD_ALT | MOD_NOREPEAT, '1');
	RegisterHotKey(NULL, (UINT)Cmd::PrevGroup, MOD_ALT | MOD_NOREPEAT, '2');
	RegisterHotKey(NULL, (UINT)Cmd::NewGroup, MOD_ALT | MOD_NOREPEAT, 'T');
	RegisterHotKey(NULL, (UINT)Cmd::DeleteGroup, MOD_ALT | MOD_NOREPEAT, 'D');
}

int WINAPI WinMain(HINSTANCE _In_ hInstance, HINSTANCE _In_opt_ hPrev, LPSTR _In_ lpCmdLine, int _In_ nCmdShow)
{
	WNDCLASSEX wc;
	LPCTSTR className = TEXT("Win Groups");
	HWND hWnd;
	MSG msg;
	HACCEL hAccelerators;

	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hCursor = (HCURSOR) LoadImage(NULL, IDC_ARROW, IMAGE_CURSOR, 0, 0, LR_SHARED);
	wc.hIcon = (HICON)LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.lpszClassName = className;
	wc.hbrBackground = (HBRUSH) GetStockObject(DKGRAY_BRUSH);


	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, TEXT("Failed Register Class"), TEXT("Error"), MB_OK);
		return 0;
	}

	hWnd = CreateWindowEx(0, className, className, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 100, 200, NULL, NULL, hInstance, NULL);
	if (!hWnd)
	{
		MessageBox(NULL, TEXT("Failed Creating Window"), TEXT("Error"), MB_OK);
		return 0;
	}

	m_hWnd = hWnd;

	hAccelerators = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));
	
	if (!CreateScratchDesktop(hWnd))
	{
		MessageBox(NULL, TEXT("Failed Creating Scratch Desktop"), TEXT("Error"), MB_OK);
		return 0;
	}

	mHInstance = hInstance;

	scope_guard guard([]() {DestoryScratchDesktop();});

	BindHotKeys();

	ShowWindow(hWnd, nCmdShow);

	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		if (msg.message == WM_HOTKEY)
		{
			switch ((Cmd)msg.wParam)
			{
				case Cmd::MoveAway:
				{
					MoveToScratch(GetForegroundWindow(), TRUE);
				} break;
				case Cmd::MoveAllAway:
				{
					MoveAllToOther();
				} break;
				case Cmd::MoveBack:
				{
					MoveBackFromOther();
				} break;
				case Cmd::MoveSwap:
				{
					MoveSwap();
				} break;
				case Cmd::RestoreTo:
				{
					RestoreScratched();
				} break;
				case Cmd::NextDesktop:
				{
					NextDesktop();
				} break;
				case Cmd::PrevDesktop:
				{
					PrevDesktop();
				} break;
				case Cmd::NextGroup:
				{
					SwitchToAnchorDesktop();
					NextGroup();
				} break;
				case Cmd::PrevGroup:
				{
					SwitchToAnchorDesktop();
					PrevGroup();
				} break;
				case Cmd::NewGroup:
				{
					NewGroup();
				} break;
				case Cmd::DeleteGroup:
				{
					DeleteGroup();
				} break;
			}

			continue;
		}

		if (TranslateAccelerator(msg.hwnd, hAccelerators, &msg))
		{
			continue;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	
	return (int)msg.wParam;
}
