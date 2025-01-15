#include "pch.h"
#include "misc.h"
#include <winternl.h>
#include <psapi.h>
#include <wtsapi32.h>
#include <algorithm>

#pragma comment(lib, "wtsapi32.lib")

#pragma comment(lib, "Psapi.lib")

#define HANDLE2DWORD(x) ((DWORD)(DWORD_PTR)x)

// 获取当前控制台的文本属性
WORD getConsoleCurrentColor()
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
	GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
	return consoleInfo.wAttributes; // 返回当前的文本属性
}

// 设置控制台的文本属性
void setConsoleColor(WORD color)
{
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(hConsole, color);
}

BOOL IsWow64()
{
	BOOL bIsWow64 = FALSE;
	typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

	static LPFN_ISWOW64PROCESS fnIsWow64Process = NULL;

	if (!fnIsWow64Process)
		fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(
			GetModuleHandle(TEXT("kernel32")), "IsWow64Process");

	if (NULL != fnIsWow64Process)
	{
		if (!fnIsWow64Process(GetCurrentProcess(), &bIsWow64))
		{
			//handle error
		}
	}
	return bIsWow64;
}

BOOL EnableDebugPrivilege()
{
	HANDLE hToken;
	LUID sedebugnameValue;
	TOKEN_PRIVILEGES tkp;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		return FALSE;
	}
	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &sedebugnameValue))
	{
		CloseHandle(hToken);
		return FALSE;
	}
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Luid = sedebugnameValue;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
	{
		CloseHandle(hToken);
		return FALSE;
	}
	return TRUE;
}


BOOL IsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
	OSVERSIONINFOEXW osvi = {};
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	DWORDLONG        const dwlConditionMask = VerSetConditionMask(
		VerSetConditionMask(
			VerSetConditionMask(
				0, VER_MAJORVERSION, VER_GREATER_EQUAL),
			VER_MINORVERSION, VER_GREATER_EQUAL),
		VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

	osvi.dwMajorVersion = wMajorVersion;
	osvi.dwMinorVersion = wMinorVersion;
	osvi.wServicePackMajor = wServicePackMajor;

	return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}

BOOL IsWindowsVistaOrGreater()
{
	return IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_VISTA), LOBYTE(_WIN32_WINNT_VISTA), 0);
}

BOOL IsElevated() {

	if (!IsWindowsVistaOrGreater()) {
		return TRUE;
	}

	BOOL fRet = FALSE;
	HANDLE hToken = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
		TOKEN_ELEVATION Elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &Elevation, sizeof(Elevation), &cbSize)) {
			fRet = !!Elevation.TokenIsElevated;
		}
	}
	if (hToken) {
		CloseHandle(hToken);
	}
	return fRet;
}


typedef struct _SYSTEM_PROCESS_INFO {
	ULONG NextEntryOffset;
	ULONG NumberOfThreads;
	LARGE_INTEGER WorkingSetPrivateSize; // since VISTA
	ULONG HardFaultCount; // since WIN7
	ULONG NumberOfThreadsHighWatermark; // since WIN7
	ULONGLONG CycleTime; // since WIN7
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;
	UNICODE_STRING ImageName;
	LONG BasePriority;
	HANDLE UniqueProcessId;
} SYSTEM_PROCESS_INFO, * PSYSTEM_PROCESS_INFO;


typedef NTSTATUS(NTAPI* PNtQuerySystemInformation)(
	SYSTEM_INFORMATION_CLASS SystemInformationClass,
	PVOID SystemInformation,
	ULONG SystemInformationLength,
	PULONG ReturnLength
	);

PNtQuerySystemInformation GetNtQuerySystemInformation() {
	static PNtQuerySystemInformation p = NULL;
	if (p) return p;
	HMODULE hNtDll = GetModuleHandle(_T("ntdll.dll"));
	if (!hNtDll) {
		return nullptr;
	}
	p = (PNtQuerySystemInformation)GetProcAddress(hNtDll, "NtQuerySystemInformation");
	return p;
}
std::wstring ResolveDevicePath(const std::wstring& devicePath) {
	std::vector<wchar_t> driveLetters(MAX_PATH);

	if (GetLogicalDriveStringsW(MAX_PATH, driveLetters.data()) == 0) {
		return devicePath;
	}

	for (wchar_t* driveLetter = driveLetters.data(); *driveLetter; driveLetter += 4) {
		std::wstring currentDrive(driveLetter);
		WCHAR currentDevicePath[MAX_PATH] = { 0 };

		if (QueryDosDeviceW(currentDrive.substr(0, 2).c_str(), currentDevicePath, MAX_PATH) == 0) {
			continue;
		}
		if (devicePath.find(currentDevicePath) == 0) {
			return currentDrive.substr(0, 2) + devicePath.substr(wcslen(currentDevicePath));
		}
	}

	return devicePath;
}
BOOL GetProcessPathByPid1(DWORD dwPID, LPTSTR lpszBuf, int nBufSize, int verb) {
	// private
	typedef struct _SYSTEM_PROCESS_ID_INFORMATION
	{
		HANDLE ProcessId;
		UNICODE_STRING ImageName;
	} SYSTEM_PROCESS_ID_INFORMATION, * PSYSTEM_PROCESS_ID_INFORMATION;
	const int _SystemProcessIdInformation = 88;

	NTSTATUS status;
	PVOID buffer = nullptr;
	ULONG bufferSize = 0x100;
	SYSTEM_PROCESS_ID_INFORMATION processIdInfo = {};

	buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufferSize);
	if (!buffer) {
		return FALSE;
	}

	processIdInfo.ProcessId = (HANDLE)(ULONG_PTR)dwPID;
	processIdInfo.ImageName.Length = 0;
	processIdInfo.ImageName.MaximumLength = (USHORT)bufferSize;
	processIdInfo.ImageName.Buffer = (PWSTR)buffer;

	status = GetNtQuerySystemInformation()(
		(SYSTEM_INFORMATION_CLASS)_SystemProcessIdInformation,
		&processIdInfo,
		sizeof(SYSTEM_PROCESS_ID_INFORMATION),
		nullptr
		);

	if (status == (NTSTATUS)0xC0000004L) { //STATUS_INFO_LENGTH_MISMATCH
		HeapFree(GetProcessHeap(), 0, buffer);
		bufferSize = processIdInfo.ImageName.MaximumLength;
		buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufferSize);
		if (!buffer) {
			return FALSE;
		}

		processIdInfo.ImageName.Buffer = (PWSTR)buffer;
		status = GetNtQuerySystemInformation()(
			(SYSTEM_INFORMATION_CLASS)_SystemProcessIdInformation,
			&processIdInfo,
			sizeof(SYSTEM_PROCESS_ID_INFORMATION),
			nullptr
			);
	}

	if (!NT_SUCCESS(status)) {
		if (verb)
			printf("GetNtQuerySystemInformation status=0x%x\n", status);
		HeapFree(GetProcessHeap(), 0, buffer);
		return FALSE;
	}


	std::wstring imagePath = std::wstring(processIdInfo.ImageName.Buffer, processIdInfo.ImageName.Length / sizeof(WCHAR));
	HeapFree(GetProcessHeap(), 0, buffer);

	if (imagePath.empty()) {
		return FALSE;
	}

	imagePath = ResolveDevicePath(imagePath);

	if (nBufSize < (int)(imagePath.size() + 1)) {
		return FALSE;
	}

#ifdef UNICODE
	wcscpy_s(lpszBuf, nBufSize, imagePath.c_str());
#else
	WideCharToMultiByte(CP_ACP, 0, imagePath.c_str(), -1, lpszBuf, nBufSize, nullptr, nullptr);
#endif

	return lpszBuf[0] != '\0';
}

BOOL GetProcessPathByPid2(DWORD dwPID, LPTSTR lpszBuf, int nBufSize, int verb)
{
	HANDLE hModuleSnap = INVALID_HANDLE_VALUE;
	MODULEENTRY32W me32;

RETRY:

	hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, dwPID);
	if (hModuleSnap == INVALID_HANDLE_VALUE)
	{
		if (verb)
			printf("CreateToolhelp32Snapshot GetLastError=%d\n", GetLastError());

		if (GetLastError() == ERROR_BAD_LENGTH)
			goto RETRY;
		return(FALSE);
	}

	me32.dwSize = sizeof(MODULEENTRY32);

	if (!Module32FirstW(hModuleSnap, &me32))
	{
		CloseHandle(hModuleSnap);
		return(FALSE);
	}

	wcsncpy_s(lpszBuf, nBufSize, me32.szExePath, nBufSize);
	CloseHandle(hModuleSnap);
	return lpszBuf[0] != '\0';
}

BOOL GetProcessPathByPid3(DWORD dwPID, LPTSTR lpszBuf, int nBufSize, int verb)
{
	DWORD n = 0;
	HANDLE hProc;
	hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
	if (!hProc)
		hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
	if (hProc) {
		n = GetModuleFileNameEx(hProc, NULL, lpszBuf, nBufSize);
		CloseHandle(hProc);
	}
	else {
		if (verb)
			printf("OpenProcess GetLastError=%d\n", GetLastError());

	}
	return n > 0;
}

BOOL GetProcessPathByPid(DWORD dwPID, LPTSTR lpszBuf, int nBufSize)
{
	return GetProcessPathByPid3(dwPID, lpszBuf, nBufSize, 0)
		|| GetProcessPathByPid2(dwPID, lpszBuf, nBufSize, 0)
		|| GetProcessPathByPid1(dwPID, lpszBuf, nBufSize, 0);
}

bool CompareProcItem(const ProcItem& a, const ProcItem& b) {
	return a.dwPID < b.dwPID; // 按 dwPID 从小到大排序
}

void sort_ps(std::vector<ProcItem>& ls)
{
	std::sort(ls.begin(), ls.end(), CompareProcItem);
}

BOOL ps_m1(std::vector<ProcItem>& ls)
{
	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;

	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return(FALSE);
	}

	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);
		return(FALSE);
	}

	do
	{
		if (pe32.th32ProcessID <= 8)
			continue;

		ProcItem pi;
		memset(&pi, 0, sizeof(pi));
		pi.dwPID = pe32.th32ProcessID;
		if (!GetProcessPathByPid(pi.dwPID, pi.szPath, MAX_PATH)) {
#ifdef UNICODE
			wcscpy_s(pi.szPath, MAX_PATH, pe32.szExeFile);
#else
			WideCharToMultiByte(CP_ACP, 0, pe32.szExeFile, -1, pi.szPath, MAX_PATH, nullptr, nullptr);
#endif
		}
		ls.push_back(pi);

	} while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);

	sort_ps(ls);
	return(TRUE);
}

BOOL ps_m2(std::vector<ProcItem>& ls)
{
	const int _SystemExtendedProcessinformation = 57;

	// 获取 NtQuerySystemInformation 函数地址
	PNtQuerySystemInformation NtQuerySystemInformation = GetNtQuerySystemInformation();

	// 第一次调用，获取所需缓冲区大小
	ULONG bufferSize = 0;
	NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)_SystemExtendedProcessinformation,
		NULL, 0, &bufferSize);

RETRY:
	if (!bufferSize) {
		return 1;
	}
	// 分配缓冲区
	PSYSTEM_PROCESS_INFO pProcessInfo = (PSYSTEM_PROCESS_INFO)malloc(bufferSize);
	if (!pProcessInfo) {
		return 0;
	}

	// 第二次调用，获取进程信息
	NTSTATUS status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)_SystemExtendedProcessinformation,
		pProcessInfo, bufferSize, &bufferSize);
	if (status != 0) {
		free(pProcessInfo);
		if (status == (NTSTATUS)0xC0000004L) { //STATUS_INFO_LENGTH_MISMATCH
			goto RETRY;
		}
		return 0;
	}

	// 遍历
	for (PSYSTEM_PROCESS_INFO pCurrentProcess = pProcessInfo
		; pCurrentProcess;
		pCurrentProcess = pCurrentProcess->NextEntryOffset ? (PSYSTEM_PROCESS_INFO)((BYTE*)pCurrentProcess + pCurrentProcess->NextEntryOffset) : NULL
		)
	{
		if (HANDLE2DWORD(pCurrentProcess->UniqueProcessId) <= 8)
			continue;

		ProcItem pi;
		memset(&pi, 0, sizeof(pi));
		pi.dwPID = HANDLE2DWORD(pCurrentProcess->UniqueProcessId);
		if (!GetProcessPathByPid(pi.dwPID, pi.szPath, MAX_PATH)) {
			std::wstring imagePath = std::wstring(pCurrentProcess->ImageName.Buffer, pCurrentProcess->ImageName.Length / sizeof(WCHAR));
#ifdef UNICODE
			wcscpy_s(pi.szPath, MAX_PATH, imagePath.c_str());
#else
			WideCharToMultiByte(CP_ACP, 0, imagePath.c_str(), -1, lpszBuf, nBufSize, nullptr, nullptr);
#endif
		}
		ls.push_back(pi);
	}

	// 释放缓冲区
	free(pProcessInfo);
	sort_ps(ls);
	return 1;
}

BOOL EnumSessions(std::list<DWORD>& ls)
{
	WTS_SESSION_INFO* pSessionInfo = NULL;
	DWORD sessionCount = 0;

	// 枚举所有会话
	BOOL result = WTSEnumerateSessions(
		WTS_CURRENT_SERVER_HANDLE, // 本地服务器
		0,                        // 保留，必须为 0
		1,                        // 版本号，必须为 1
		&pSessionInfo,            // 接收会话信息的指针
		&sessionCount             // 接收会话数量
	);

	if (!result) {
		return 0;
	}

	for (DWORD i = 0; i < sessionCount; i++) {
		ls.push_back(pSessionInfo[i].SessionId);
	}
	// 释放内存
	WTSFreeMemory(pSessionInfo);
	return 1;
}

BOOL ps_m3_sid(DWORD dwSID, std::vector<ProcItem>& ls)
{
	typedef struct _SYSTEM_SESSION_PROCESS_INFORMATION
	{
		ULONG SessionId;
		ULONG SizeOfBuf;
		PVOID Buffer;
	} SYSTEM_SESSION_PROCESS_INFORMATION, * PSYSTEM_SESSION_PROCESS_INFORMATION;

	const int _SystemSessionProcessInformation = 53;

	PNtQuerySystemInformation NtQuerySystemInformation = GetNtQuerySystemInformation();

	SYSTEM_SESSION_PROCESS_INFORMATION SSPI;
	SSPI.SessionId = dwSID;
	SSPI.Buffer = NULL;
	SSPI.SizeOfBuf = 0;

	// 第一次调用，获取所需缓冲区大小
	ULONG bufferSize = 0;
	NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)_SystemSessionProcessInformation,
		&SSPI, sizeof(SSPI), &bufferSize);

RETRY:
	if (!bufferSize) {
		return 1;
	}
	// 分配缓冲区
	PSYSTEM_PROCESS_INFO pProcessInfo = (PSYSTEM_PROCESS_INFO)malloc(bufferSize);
	if (!pProcessInfo) {
		return 0;
	}

	SSPI.Buffer = pProcessInfo;
	SSPI.SizeOfBuf = bufferSize;

	// 第二次调用，获取进程信息
	NTSTATUS status = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)_SystemSessionProcessInformation,
		&SSPI, sizeof(SSPI), &bufferSize);
	if (status != 0) {
		free(pProcessInfo);
		if (status == (NTSTATUS)0xC0000004L) { //STATUS_INFO_LENGTH_MISMATCH
			goto RETRY;
		}
		return 0;
	}

	// 遍历
	for (PSYSTEM_PROCESS_INFO pCurrentProcess = (PSYSTEM_PROCESS_INFO)SSPI.Buffer
		; pCurrentProcess;
		pCurrentProcess = pCurrentProcess->NextEntryOffset ? (PSYSTEM_PROCESS_INFO)((BYTE*)pCurrentProcess + pCurrentProcess->NextEntryOffset) : NULL
		)
	{
		if (HANDLE2DWORD(pCurrentProcess->UniqueProcessId) <= 8)
			continue;
		ProcItem pi;
		memset(&pi, 0, sizeof(pi));
		pi.dwPID = HANDLE2DWORD(pCurrentProcess->UniqueProcessId);
		if (!GetProcessPathByPid(pi.dwPID, pi.szPath, MAX_PATH)) {
			std::wstring imagePath = std::wstring(pCurrentProcess->ImageName.Buffer, pCurrentProcess->ImageName.Length / sizeof(WCHAR));
#ifdef UNICODE
			wcscpy_s(pi.szPath, MAX_PATH, imagePath.c_str());
#else
			WideCharToMultiByte(CP_ACP, 0, imagePath.c_str(), -1, pi.szPath, MAX_PATH, nullptr, nullptr);
#endif
		}
		ls.push_back(pi);
	}

	// 释放缓冲区
	free(pProcessInfo);
	sort_ps(ls);
	return 1;
}


BOOL ps_m3(std::vector<ProcItem>& ls)
{
	std::list<DWORD> sidls;
	EnumSessions(sidls);

	for (auto sid : sidls) {
		ps_m3_sid(sid, ls);
	}

	return 1;
}


BOOL isProcessStillActive1(DWORD pid)
{
	HANDLE hProc;
	hProc = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (hProc) {
		DWORD w = WaitForSingleObject(hProc, 0);
		CloseHandle(hProc);
		if (w == WAIT_TIMEOUT)
		{
			return 1;
		}
	}
	return 0;
}

BOOL isProcessStillActive2(DWORD pid)
{
	static int vista = -1;
	if (vista == -1) {
		vista = IsWindowsVistaOrGreater();
	}
	
	HANDLE hProcess = OpenProcess(vista ? PROCESS_QUERY_LIMITED_INFORMATION : PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (hProcess) {
		DWORD exitCode;
		if (GetExitCodeProcess(hProcess, &exitCode)) {
			if (exitCode == STILL_ACTIVE) {
				CloseHandle(hProcess);
				return 1;
			}
		}
		CloseHandle(hProcess);
	}
	return 0;
}

BOOL ps_m4_test(DWORD dwPID)
{
	if (isProcessStillActive1(dwPID)
		|| isProcessStillActive2(dwPID)) {
		return TRUE;
	}
	return FALSE;
}

BOOL ps_m4(std::vector<ProcItem>& ls, DWORD mpid)
{
	DWORD dwPID;
	for (dwPID = 12; dwPID <= mpid; dwPID += 4) {
		if (ps_m4_test(dwPID)) {
			ProcItem pi;
			memset(&pi, 0, sizeof(pi));
			pi.dwPID = dwPID;
			GetProcessPathByPid(pi.dwPID, pi.szPath, MAX_PATH);
			ls.push_back(pi);
		}
	}
	sort_ps(ls);
	return TRUE;
}
