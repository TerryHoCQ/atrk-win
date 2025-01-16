#pragma once

#include <vector>
#include <string>
#include <tlhelp32.h>

struct ProcItem {
	DWORD dwPID;
	WCHAR szPath[MAX_PATH];
};

WORD getConsoleCurrentColor();
void setConsoleColor(WORD color);
BOOL IsWow64();
BOOL EnableDebugPrivilege();
BOOL IsWindowsVistaOrGreater();
BOOL IsElevated();
BOOL GetProcessPathByPid(DWORD dwPID, LPTSTR lpszBuf, int nBufSize);
BOOL GetProcessPathByPid1(DWORD dwPID, LPTSTR lpszBuf, int nBufSize, int verb);
BOOL GetProcessPathByPid2(DWORD dwPID, LPTSTR lpszBuf, int nBufSize, int verb);
BOOL GetProcessPathByPid3(DWORD dwPID, LPTSTR lpszBuf, int nBufSize, int verb);

void sort_ps(std::vector<ProcItem>& ls);

BOOL ps_m1(std::vector<ProcItem>& ls); //CreateToolhelp32Snapshot
BOOL ps_m2(std::vector<ProcItem>& ls); //SystemExtendedProcessinformation
BOOL ps_m3(std::vector<ProcItem>& ls); //SystemSessionProcessInformation
BOOL ps_m4(std::vector<ProcItem>& ls, DWORD mpid); // ±©Á¦OpenProcess
BOOL ps_m4_test(DWORD dwPID);
BOOL ps_m5(std::vector<ProcItem>& ls); //SystemExtendedHandleInformation
