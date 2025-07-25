#include "Hook.h"
#include "Network.h"
#include "ResMan.h"
#include "Wnd.h"
#include "DamageSkin.h"

#include "Resources/Config.h"
#include "Share/Funcs.h"
#include "Share/Tool.h"

#include "HookEx.h"
#pragma comment(lib, "HookEx.lib")

#include <imm.h>
#pragma comment(lib, "imm32.lib")

#include<intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace {
	static Rosemary gMapleR;
	static bool bGetStartupInfoALoaded = false;
	static bool bCreateMutexALoaded = false;
	static bool bImmAssociateContextLoaded = false;

	void GetModuleEntryList(std::vector<MODULEENTRY32W>& entryList) {
		DWORD pid = GetCurrentProcessId();

		if (!pid) {
			return;
		}

		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);

		if (hSnapshot == INVALID_HANDLE_VALUE) {
			return;
		}

		MODULEENTRY32W me32;
		memset(&me32, 0, sizeof(me32));
		me32.dwSize = sizeof(me32);
		if (!Module32FirstW(hSnapshot, &me32)) {
			CloseHandle(hSnapshot);
			return;
		}

		entryList.clear();

		do {
			entryList.push_back(me32);
		} while (Module32NextW(hSnapshot, &me32));

		CloseHandle(hSnapshot);
	}

	// Make sure the executable unpacks itself.
	bool IsEXECaller(void* vReturnAddress) {
		if (gMapleR.IsInitialized()) {
			DEBUG(L"gMapleR has already been initialized");
			return true;
		}
		std::vector<MODULEENTRY32W> entryList;
		GetModuleEntryList(entryList);
		if (entryList.empty()) {
			DEBUG(L"EXE hasn't been call yet");
			return false;
		}
		// first module may be exe
		const MODULEENTRY32W& me32 = entryList[0];
		ULONG_PTR returnAddr = (ULONG_PTR)vReturnAddress;
		ULONG_PTR baseAddr = (ULONG_PTR)me32.modBaseAddr;
		ULONG_PTR endAddr = baseAddr + me32.modBaseSize;

		if (returnAddr >= baseAddr && returnAddr < endAddr) {
			gMapleR.Init(&entryList, L"MapleStory.exe");
			if (gMapleR.IsInitialized()) {
				DEBUG(L"gMapleR init ok");
				return true;
			}
			else {
				DEBUG(L"Failed to init gMapleR");
				return false;
			}
		}
		return false;
	}

	static auto _GetStartupInfoA = decltype(&GetStartupInfoA)(GetProcAddress(GetModuleHandle(L"KERNEL32"), "GetStartupInfoA"));
	VOID WINAPI GetStartupInfoA_Hook(LPSTARTUPINFOA lpStartupInfo) {
		if (!bGetStartupInfoALoaded && lpStartupInfo && IsEXECaller(_ReturnAddress())) {
			bGetStartupInfoALoaded = true;
			// Click MapleStory.exe
			if (!Network::InitWSAData()) {
				DEBUG(L"Unable to init WSAData");
			}
			if (!Network::FixDomain(gMapleR)) {
				DEBUG(L"Unable to fix domain");
			}
			if (!HookEx::RemoveLocaleCheck(gMapleR)) {
				DEBUG(L"Unable to remove locale check");
			}
			// Click Play button
			// Load AntiCheat
			if (!HookEx::RemoveSecurityClient(gMapleR)) {
				DEBUG(L"Unable to remove SecurityClient");
			}
			// Check if base.wz exists? wz default mode or img mode
			// Check if multiple.wz exists? wz multiple mode or wz default mode
			if (!HookEx::Mount(gMapleR, Network::GetMapleVersion(), &Config::MapleWZKey)) {
				DEBUG(L"Unable to mount ResMan");
			}
			if (!ResMan::Extend(gMapleR)) {
				DEBUG(L"Unable to extend ResMan");
			}
		}
		_GetStartupInfoA(lpStartupInfo);
	}

	// CreateMutexA is the first Windows library call after the executable unpacks itself. 
	// It is recommended to have all Maple hooking and memory editing inside or called from the CreateMutexA function.
	static auto _CreateMutexA = decltype(&CreateMutexA)(GetProcAddress(GetModuleHandleW(L"KERNEL32"), "CreateMutexA"));
	HANDLE WINAPI CreateMutexA_Hook(LPSECURITY_ATTRIBUTES lpMutexAttributes, BOOL bInitialOwner, LPCSTR lpName) {
		if (!bCreateMutexALoaded) {
			bCreateMutexALoaded = true;
			// Select game area
			if (!Network::Redirect(Config::ServerAddr, Config::LoginServerPort)) {
				DEBUG(L"Unable to redirect addr");
			}
			if (!Network::RecvXOR(Config::RecvXOR)) {
				DEBUG(L"Unable to hook recv");
			}
			if (!Network::SendXOR(Config::SendXOR)) {
				DEBUG(L"Unable to hook send");
			}
			// Login UI is loaded
			if (!HookEx::RemoveManipulatePacketCheck(gMapleR)) {
				DEBUG(L"Unable to remove Manipulate Packet Check");
			}
			if (!HookEx::RemoveRenderFrameCheck(gMapleR)) {
				DEBUG(L"Unable to remove Render Frame Check");
			}
			// Select character
			if (!HookEx::RemoveEnterFieldCheck(gMapleR)) {
				DEBUG(L"Unable to remove Enter Field Check");
			}
		}
		if (lpName && strstr(lpName, "WvsClientMtx")) {
			// MultiClient: faking HANDLE is 0xBADF00D(BadFood)
			return (HANDLE)0xBADF00D;
		}
		return _CreateMutexA(lpMutexAttributes, bInitialOwner, lpName);
	}

	static auto _CreateWindowExA = decltype(&CreateWindowExA)(GetProcAddress(LoadLibraryW(L"USER32"), "CreateWindowExA"));
	HWND WINAPI CreateWindowExA_Hook(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
		if (!Config::IsStartUpDlgSkipped) {
			return _CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
		}
		if (lpClassName && strstr(lpClassName, "StartUpDlgClass")) {
			// Found in ShowADBalloon 
			// AOB C7 45 FC 09 00 00 00 E8 ?? ?? FF FF
			// CMS88(00A1279A)
			// Remove StartUp Ads
			return NULL;
		}
		if (lpClassName && strstr(lpClassName, "MapleStoryClass")) {
			// Found in ShowStartUpWnd
			lpWindowName = Config::WindowTitle.c_str(); //Customize game window title 			
			Wnd::FixMinimizeButton(dwStyle); // Show minimize button for CMS79 - CMS84
			// Place the game window in the center of the screen
			RECT screenRect;
			GetWindowRect(GetDesktopWindow(), &screenRect);
			int centerX = screenRect.right / 2 - nWidth / 2;
			int centerY = screenRect.bottom / 2 - nHeight / 2;
			return _CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, centerX, centerY, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
		}
		if (lpClassName && strstr(lpClassName, "ShandaADBallon") || lpClassName && strstr(lpClassName, "ShandaADBrowser")) {
			// Found in WinMain
			// AOB 74 ?? 8D ?? ?? 6A 04 ?? 88
			// CMS88(00A13188)
			// Remove Exit Ads
			return NULL;
		}
		return _CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	}

	static auto _ImmAssociateContext = decltype(&ImmAssociateContext)(GetProcAddress(GetModuleHandleW(L"IMM32"), "ImmAssociateContext"));
	HIMC WINAPI ImmAssociateContext_Hook(HWND hWnd, HIMC hIMC) {
		// Call by CWndMan::CWndMan<-CWvsApp::CreateWndManager after CWvsApp::InitializeGameData in CWvsApp::SetUp 
		if (!bImmAssociateContextLoaded) {
			// TODO
			bImmAssociateContextLoaded = true;
			if (Config::DamageSkinID > 0) {
				if (!DamageSkin::Init()) {
					DEBUG(L"Unable to init Damage Skin");
				}
				else {
					DamageSkin::ApplyLocally(Config::DamageSkinID);
				}
			}
		}
		// Enable IME input
		HIMC overrideHIMC = ImmGetContext(hWnd);
		ImmSetOpenStatus(overrideHIMC, TRUE);
		return _ImmAssociateContext(hWnd, overrideHIMC);
	}

}

namespace Hook {

	void Install() {
		bool ok = SHOOK(true, &_GetStartupInfoA, GetStartupInfoA_Hook) &&
			SHOOK(true, &_CreateMutexA, CreateMutexA_Hook) &&
			SHOOK(true, &_CreateWindowExA, CreateWindowExA_Hook) &&
			SHOOK(true, &_ImmAssociateContext, ImmAssociateContext_Hook);
		if (!ok) {
			DEBUG(L"Failed to install hook");
		}
	}

	void Uninstall() {
		bool ok = SHOOK(false, &_GetStartupInfoA, GetStartupInfoA_Hook) &&
			SHOOK(false, &_CreateMutexA, CreateMutexA_Hook) &&
			SHOOK(false, &_CreateWindowExA, CreateWindowExA_Hook) &&
			SHOOK(false, &_ImmAssociateContext, ImmAssociateContext_Hook);
		if (!ok) {
			DEBUG(L"Failed to uninstall hook");
		}
		Network::ClearupWSA();
	}
}
