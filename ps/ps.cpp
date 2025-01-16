#include "pch.h"
#include <vector>
#include <string>
#include <locale.h>
#include <windows.h>
#include <time.h>
#include "misc.h"

DWORD max_pid = 300000;

// 自定义比较函数，用于二分查找
bool CompareProcItem(const ProcItem& item, DWORD dwPID) {
	return item.dwPID < dwPID;
}

// 在排序好的 procList 中查找指定 dwPID 的项
std::vector<ProcItem>::iterator FindProcItemByPID(std::vector<ProcItem>& procList, DWORD dwPID) {
	auto it = std::lower_bound(procList.begin(), procList.end(), dwPID, CompareProcItem);

	if (it != procList.end() && it->dwPID == dwPID) {
		return it; // 找到目标项
	}
	else {
		return procList.end(); // 未找到
	}
}

bool TestPIDInList(std::vector<ProcItem>& procList, DWORD targetPID) {
	
	return FindProcItemByPID(procList, targetPID) != procList.end();
}

void psResultAdd(std::vector<ProcItem>& procList, ProcItem& pi)
{
	if (!TestPIDInList(procList, pi.dwPID)) {
		procList.push_back(pi);
		sort_ps(procList);
	}
}

int FindHiddenProcess(std::vector<ProcItem>& out)
{
	int nfound = 0;
	std::vector<ProcItem> psls1;
	std::vector<ProcItem> psls2, psls2e;
	std::vector<ProcItem> psls3, psls3e;
	std::vector<ProcItem> psls4;
	std::vector<ProcItem> psls5, psls5e;

	_ftprintf(stdout, _T("Method 5: Retrieving process list using SystemExtendedHandleInformation...\n"));
	ps_m5(psls5);
	_ftprintf(stdout, _T("    Total processes: %d\n"), (int)psls5.size());

	_ftprintf(stdout, _T("Method 4: Bruteforce searching for PIDs using OpenProcess, from 12 to %d...\n"), max_pid);
	ps_m4(psls4, max_pid);  // OpenProcess
	_ftprintf(stdout, _T("    Total processes: %d\n"), (int)psls4.size());

	_ftprintf(stdout, _T("Method 3: Retrieving process IDs for all sessions using SystemSessionProcessInformation...\n"));
	ps_m3(psls3);  // SystemSessionProcessInformation
	_ftprintf(stdout, _T("    Total processes: %d\n"), (int)psls3.size());

	_ftprintf(stdout, _T("Method 2: Retrieving process list using SystemExtendedProcessInformation...\n"));
	ps_m2(psls2);  // SystemExtendedProcessInformation
	_ftprintf(stdout, _T("    Total processes: %d\n"), (int)psls2.size());

	_ftprintf(stdout, _T("Method 1: Retrieving process list using CreateToolhelp32Snapshot...\n"));
	ps_m1(psls1);  // CreateToolhelp32Snapshot
	_ftprintf(stdout, _T("    Total processes: %d\n"), (int)psls1.size());

	ps_m5(psls5e);
	ps_m3(psls3e);
	ps_m2(psls2e);

	_ftprintf(stdout, _T("Results comparing to method 1's...\n"));

	for (auto& it : psls2) {
		//方法2获得的进程列表，如果pid不在方法1的列表中，且方法2再次确认该pid仍存在
		if (!TestPIDInList(psls1, it.dwPID) && TestPIDInList(psls2e, it.dwPID)) {
			psResultAdd(out, it);
			nfound += 1;
			_ftprintf(stdout, _T("Method 2: Found %d\n"), it.dwPID);
		}
	}
	for (auto& it : psls3) {
		if (!TestPIDInList(psls1, it.dwPID) && TestPIDInList(psls3e, it.dwPID)) {
			psResultAdd(out, it);
			nfound += 1;
			_ftprintf(stdout, _T("Method 3: Found %d\n"), it.dwPID);
		}
	}
	for (auto& it : psls4) {
		if (!TestPIDInList(psls1, it.dwPID) && ps_m4_test(it.dwPID)) {
			psResultAdd(out, it);
			nfound += 1;
			_ftprintf(stdout, _T("Method 4: Found %d\n"), it.dwPID);
		}
	}
	for (auto& it : psls5) {
		if (!TestPIDInList(psls1, it.dwPID) && TestPIDInList(psls5e, it.dwPID)) {
			psResultAdd(out, it);
			nfound += 1;
			_ftprintf(stdout, _T("Method 5: Found %d\n"), it.dwPID);
		}
	}
	return nfound;
}


int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	_ftprintf(stderr, _T("atrk-win: ps v1.1.0\n"));

	int nRetCode = 0;
	LPCTSTR output_csv = NULL;
	setlocale(LC_ALL, "chs");

	EnableDebugPrivilege();

	for (int n = 1; n < argc; n++) {
		if ((!_tcsicmp(argv[n], _T("-help")) || !_tcsicmp(argv[n], _T("-h")))) {
			_ftprintf(stderr, _T("Usage: %s -maxpid 300000\n"), argv[0]);
			return 0;
		}
		else if (!_tcsicmp(argv[n], _T("-maxpid")) && n + 1 < argc) {
			max_pid = _ttoi(argv[n + 1]);
			n++;
		}
		else if (_tcsicmp(argv[n], _T("-ocsv")) == 0 && (n + 1 < argc)) {
			output_csv = argv[n + 1];
			n++;
		}
	}


	if (!IsElevated()) {
		_ftprintf(stderr, _T("NOT ELEVATED! Please run as administrator.\n"));
	}
	std::vector<ProcItem> pshidden;

	FindHiddenProcess(pshidden);

	if (pshidden.size()) {
		_ftprintf(stdout, _T("Found suspicious processes:\n"));
		for (auto& it : pshidden) {
			printf("%d: %S\n", it.dwPID, it.szPath);
		}
		_ftprintf(stdout, _T("Total Number of Suspicious Processes Found: %d\n"), (int)pshidden.size());

	}
	else {
		fprintf(stdout, "No found hidden processes.\n");
	}

	if (output_csv) {
		FILE* file = nullptr;
#ifdef _UNICODE
		if (_tfopen_s(&file, output_csv, _T("w, ccs=UTF-8")) != 0)
#else
		if (_tfopen_s(&file, output_csv, _T("w")) != 0)
#endif
		{
			_ftprintf(stderr, _T("Failed to open file: %s\n"), output_csv);
			return 1;
		}

		_ftprintf(file, _T("pid,path\n"));
		for (auto& it : pshidden) {
#ifdef _UNICODE
			_ftprintf(file, _T("%d,%s\n"), it.dwPID, it.szPath);
#else
			_ftprintf(file, _T("%d,%S\n"), it.dwPID, it.szPath);
#endif
		}
		fclose(file);
	}

	return nRetCode;
}
