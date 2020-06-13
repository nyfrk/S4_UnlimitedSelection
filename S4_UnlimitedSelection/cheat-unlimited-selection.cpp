///////////////////////////////////////////////////////////////////////////////
// MIT License
//
// Copyright (c) 2020 nyfrk
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
///////////////////////////////////////////////////////////////////////////////

#include "cheat-unlimited-selection.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "console-helper.h"
#include "hlib.h"

#define LIMITED_BOXSELECT_LIMIT 250 // a new limit for the box select tool. 
#define UNLIMITED_BOX_SELECT_LIMIT 0 // set to 1 to completely remove the limit. Be prepared for crashes when using this.

using namespace std;
using namespace hlib;



static const DWORD S4_Main = (DWORD)GetModuleHandleA(NULL);
static const HANDLE hProcess = GetCurrentProcess();



/////////////////////////////////////////////////////////////////////////////////////////////
//// Patterns
/////////////////////////////////////////////////////////////////////////////////////////////

static const DWORD bufferOverflowFixPattern = (DWORD)FindPattern(hProcess, S4_Main, 
	StringPattern("8B 7D 10 8B 04 82 03 45 FC 78 46 8B 0D ? ? ? ? 8B 55 F8 8D 04 89 C1 E0 02"));

static const DWORD unlimitedBoxSelectPattern = (DWORD)FindPattern(hProcess, S4_Main,
	StringPattern("D1 F8 83 F8 64 0F 83 84 00 00 00 8B C6 25 00 FF FF 00 89 45 F8 0F B7 04 59 8B 3C 85"));

static const DWORD unlimitedUnitsPerRightclickPattern = (DWORD)FindPattern(hProcess, S4_Main,
	StringPattern("F7 40 14 00 40 00 00 75 DC B8 ? ? ? ? BA 02 00 00 00 3B F8 0F 4F F8 33 C9 8D 04 3F"));

static const DWORD clearSelectionPattern = (DWORD)FindPattern(hProcess, S4_Main,
	StringPattern("A1 ? ? ? ? 8B 0D ? ? ? ? 2B C1 D1 F8 74 61 56 33 F6 85 C0 74 53"));



/////////////////////////////////////////////////////////////////////////////////////////////
//// Patch Helper
/////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Return the address derived from the pattern if pattern is not null. 
 * Otherwise return the default address. We always prefer addresses
 * obtained by patterns over our hardcoded default.
 **/
static DWORD PICK(DWORD def, DWORD pattern, int offset = 0) {
	return pattern ? pattern + offset : def;
}

/**
 * Read a DWORD from memory or return 0 on failure.
 **/
static DWORD READ_AT(DWORD pattern, int offset = 0) {
	DWORD ret;
	if (pattern && ReadProcessMemory(hProcess, (LPCVOID)(pattern + offset), &ret, sizeof(DWORD), NULL)) {
		return ret;
	}
	return 0;
}



/////////////////////////////////////////////////////////////////////////////////////////////
//// Patches
/////////////////////////////////////////////////////////////////////////////////////////////

// This patch will completely remove the limit from the box select tool
static const DWORD unlimitedBoxSelectAddr = PICK(S4_Main + 0xED241, unlimitedBoxSelectPattern, 5);
static NopPatch unlimitedBoxSelectPatch(unlimitedBoxSelectAddr, 6);

// This patch will set a new limit to LIMITED_BOXSELECT_LIMIT
static const DWORD largerBoxSelectLimitAddr = PICK(S4_Main + 0xED23E, unlimitedBoxSelectPattern, 2);
static void __newBoxSelectLimit(); // hook procedure
static JmpPatch largerBoxSelectLimit(largerBoxSelectLimitAddr, (DWORD)__newBoxSelectLimit, 4);

// This patch fixes a buffer overflow related to the health bubbles caused by the increased limit
static void __bufferOverflowFix(); // hook procedure
static DWORD __bufferOverflowFixOffset = PICK(S4_Main + 0x105752C, READ_AT(bufferOverflowFixPattern, 13)); // this is used by the hook procedure.
static const DWORD bufferOverflowFixAddr = PICK(S4_Main + 0x26041B, bufferOverflowFixPattern, 11);
static const void* const largerBoxSelectLimitContinue = (const void *)(largerBoxSelectLimitAddr + 0x09);
static const void* const largerBoxSelectLimitBreak = (const void*)(largerBoxSelectLimitAddr + 0x8D);
static CallPatch bufferOverflowFixPatch(bufferOverflowFixAddr, (DWORD)__bufferOverflowFix, 1);

// This patch removes the limit of soldiers that can be commanded using right click
static const DWORD unlimitedUnitsPerRightclickAddr = PICK(S4_Main + 0xEC8CF + 1, unlimitedUnitsPerRightclickPattern, 10);
static Patch unlimitedUnitsPerRightclickPatch(unlimitedUnitsPerRightclickAddr, (DWORD)0x7FFFFFFFu, (DWORD)0x64u);

// This function allows to unselect all units
typedef void (__stdcall *clearSelection_t)();
static const clearSelection_t clearSelection = (clearSelection_t)PICK(S4_Main + 0xEE530, clearSelectionPattern);

// The vector where all the selections are stored.
// static const DWORD selectionVectorAddr = S4_Main + 0x10836f4; 


// The patch bundle that contains all the patches to enable for this cheat.
// Furthermore we assign names to each patch.
static struct {
	AbstractPatch* ptr;
	const char* name;
}const patchBundle[] = {
	{&bufferOverflowFixPatch,"BufferOverflowFixPatch"},
#if UNLIMITED_BOX_SELECT_LIMIT
	{&unlimitedBoxSelectPatch,"UnlimitedBoxSelectPatch"},
#else
	{&largerBoxSelectLimit,"LargerBoxSelectPatch"},
#endif
	{&unlimitedUnitsPerRightclickPatch,"UnlimitedUnitsPerRightclickPatch"},
};



/////////////////////////////////////////////////////////////////////////////////////////////
//// Cheat Controller
/////////////////////////////////////////////////////////////////////////////////////////////

static bool isCheatEnabled = false;

/**
 * This cheat will enable the selection of more than the default 100 units.
 **/
DllExport int __stdcall EnableUnlimitedSelectionCheat(const void* reserved) {
	UNREFERENCED_PARAMETER(reserved);

	#if VERBOSE
	cout << "EnableUnlimitedSelectionCheat() has been called" << endl
		 << endl << "  Pattern matching results:" << endl
		 << "  [ 0x" << hex << bufferOverflowFixPattern << " ] bufferOverflowFixPattern" << endl
		 << "  [ 0x" << hex << unlimitedBoxSelectPattern << " ] unlimitedBoxSelectPattern" << endl
		 << "  [ 0x" << hex << unlimitedUnitsPerRightclickPattern << " ] unlimitedUnitsPerRightclickPattern" << endl
		 << "  [ 0x" << hex << clearSelectionPattern << " ] unselectAllPatternFindPattern" << endl 
		 << endl;
	#endif


	if (isCheatEnabled) {
		#if VERBOSE
			cout << "Cheat is already enabled." << endl;
		#endif
		return 0;
	}
	isCheatEnabled = true;


	for (auto& patch : patchBundle) {
		#if VERBOSE
			cout << "  [ 0x" << hex << patch.ptr->getAddress() << " ] Apply " << patch.name << endl;
		#endif
		patch.ptr->patch(hProcess);
	}


	#if VERBOSE
		cout << endl << "EnableUnlimitedSelectionCheat() has completed" << endl << endl;
	#endif

	return 0;
}

/**
 * Disable the unlimited units cheat.
 **/
DllExport int __stdcall DisableUnlimitedSelectionCheat(const void *reserved) {
	UNREFERENCED_PARAMETER(reserved);

	#if VERBOSE
		cout << "DisableUnlimitedSelectionCheat() has been called" << endl;
	#endif


	if (!isCheatEnabled) {
		#if VERBOSE
			cout << "Cheat is already disabled." << endl;
		#endif
		return 0;
	}
	isCheatEnabled = false;


	#if VERBOSE
		cout << "  [ 0x" << hex << (DWORD)clearSelection << " ] Clearing selection" << endl << endl;
	#endif
	// Note when unloading, we must make sure to clear the selection because 
	// otherwise we may crash the game when more than 100 units are selected.
	clearSelection();


	// Undo patches in reversed order
	for (int i = _countof(patchBundle) - 1; i >= 0; i--) {
		#if VERBOSE
			cout << "  [ 0x" << hex << patchBundle[i].ptr->getAddress() << " ] Restore " << patchBundle[i].name << endl;
		#endif
		patchBundle[i].ptr->unpatch(hProcess);
	}


	#if VERBOSE
		cout << "DisableUnlimitedSelectionCheat() has completed" << endl;
	#endif

	return 0;
}



/////////////////////////////////////////////////////////////////////////////////////////////
//// Hook Procedures
/////////////////////////////////////////////////////////////////////////////////////////////

static void __declspec(naked) __bufferOverflowFix() {
	__asm {
		mov ecx, [__bufferOverflowFixOffset]
		mov ecx, [ecx]
		cmp ecx, 100
		jl ok
			mov ecx, 100
		ok:
		ret
	}
}

static void __declspec(naked) __newBoxSelectLimit() {
	__asm {
		cmp eax, LIMITED_BOXSELECT_LIMIT
		jae lbl
			jmp largerBoxSelectLimitContinue
		lbl:
		jmp largerBoxSelectLimitBreak
	}
}



/*
/////////////////////////////////////////////////////////////////////////////////////////////
//// Patterns documentation and where to patch
/////////////////////////////////////////////////////////////////////////////////////////////

bufferOverflowFixPattern (+11):
S4_Main.exe+260410 - 8B 7D 10              - mov edi,[ebp+10]
S4_Main.exe+260413 - 8B 04 82              - mov eax,[edx+eax*4]
S4_Main.exe+260416 - 03 45 FC              - add eax,[ebp-04]
S4_Main.exe+260419 - 78 46                 - js S4_Main.exe+260461
S4_Main.exe+26041B - 8B 0D 2C75A701        - mov ecx,[S4_Main.exe+105752C]    <===== we patch here
S4_Main.exe+260421 - 8B 55 F8              - mov edx,[ebp-08]
S4_Main.exe+260424 - 8D 04 89              - lea eax,[ecx+ecx*4]
S4_Main.exe+260427 - C1 E0 02              - shl eax,02

unlimitedBoxSelectPatch (+5):
S4_Main.exe+ED23C - D1 F8                 - sar eax,1
S4_Main.exe+ED23E - 83 F8 64              - cmp eax,64                <===== we patch here
S4_Main.exe+ED241 - 0F83 84000000         - jae S4_Main.exe+ED2CB     <===== and here
S4_Main.exe+ED247 - 8B C6                 - mov eax,esi
S4_Main.exe+ED249 - 25 00FFFF00           - and eax,S4_Main.exe+5DFF00
S4_Main.exe+ED24E - 89 45 F8              - mov [ebp-08],eax
S4_Main.exe+ED251 - 0FB7 04 59            - movzx eax,word ptr [ecx+ebx*2]
S4_Main.exe+ED255 - 8B 3C 85 888D8B01     - mov edi,[eax*4+S4_Main.exe+E98D88]

unlimitedUnitsPerRightclickPattern (+10)
S4_Main.exe+EC8C6 - F7 40 14 00400000     - test [eax+14],00004000
S4_Main.exe+EC8CD - 75 DC                 - jne S4_Main.exe+EC8AB
S4_Main.exe+EC8CF - B8 64000000           - mov eax,00000064     <===== we patch here
S4_Main.exe+EC8D4 - BA 02000000           - mov edx,00000002
S4_Main.exe+EC8D9 - 3B F8                 - cmp edi,eax
S4_Main.exe+EC8DB - 0F4F F8               - cmovg edi,eax
S4_Main.exe+EC8DE - 33 C9                 - xor ecx,ecx
S4_Main.exe+EC8E0 - 8D 04 3F              - lea eax,[edi+edi]

unselectAllUnitsPattern (+0)
S4_Main.exe+EE530 - A1 F836AA01           - mov eax,[S4_Main.exe+10836F8]     <===== we invoke here
S4_Main.exe+EE535 - 8B 0D F436AA01        - mov ecx,[S4_Main.exe+10836F4]
S4_Main.exe+EE53B - 2B C1                 - sub eax,ecx
S4_Main.exe+EE53D - D1 F8                 - sar eax,1
S4_Main.exe+EE53F - 74 61                 - je S4_Main.exe+EE5A2
S4_Main.exe+EE541 - 56                    - push esi
S4_Main.exe+EE542 - 33 F6                 - xor esi,esi
S4_Main.exe+EE544 - 85 C0                 - test eax,eax
S4_Main.exe+EE546 - 74 53                 - je S4_Main.exe+EE59B


*/
