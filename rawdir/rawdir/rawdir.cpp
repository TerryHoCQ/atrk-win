#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <locale> 
#include "rawdirImpl.h"
#include <string.h>
#include <map>
#include <vector>
#include <cctype>
#include <shlwapi.h>
#include <inttypes.h>
#include <unordered_map>
#include <iostream>
#include <algorithm>
#pragma comment(lib, "Shlwapi.lib")
#include "fhf.h"

int raw_dir(int argc, TCHAR** argv)
{
	HANDLE hOutput = NULL;

	if (argc < 2) {
		_ftprintf(stderr, _T("  dir <drive-letters> <output>\n"));
		_ftprintf(stderr, _T("      example:\n"));
		_ftprintf(stderr, _T("      dir CDE output.efu\n"));
		return 1;
	}
	if (argc >= 3) {
		hOutput = CreateFile(argv[2], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
		if (hOutput == INVALID_HANDLE_VALUE) {
			_ftprintf(stderr, _T("error: createfile, %d\n"), GetLastError());
			return 1;
		}
	}

	RawListFiles(argv[1], hOutput);

	if (hOutput) {
		CloseHandle(hOutput);
	}
	return 0;
}

int sync_vol(std::wstring volume) {
	HANDLE hVolume = CreateFileW(
		volume.c_str(),         // 卷路径
		GENERIC_WRITE,          // 写权限
		FILE_SHARE_READ | FILE_SHARE_WRITE, // 共享模式
		NULL,                   // 默认安全属性
		OPEN_EXISTING,          // 打开已存在的卷
		FILE_ATTRIBUTE_NORMAL,  // 普通文件
		NULL                    // 无模板文件
	);

	if (hVolume == INVALID_HANDLE_VALUE) {
		std::wcerr << L"  Failed to open volume: " << volume << L". Error: " << GetLastError() << std::endl;
		return -1;
	}

	// 刷新卷的缓存
	if (!FlushFileBuffers(hVolume)) {
		std::wcerr << L"  Failed to flush volume buffers: " << volume << L". Error: " << GetLastError() << std::endl;
	}
	else {
		std::wcout << L"  Volume data flushed to disk: " << volume << std::endl;
	}

	CloseHandle(hVolume);
	return 0;
}

int sync_all_vol() {
	// 获取所有卷的路径
	std::vector<std::wstring> volumes;
	wchar_t volumeName[MAX_PATH];
	HANDLE hFind = FindFirstVolume(volumeName, MAX_PATH);

	if (hFind == INVALID_HANDLE_VALUE) {
		std::cerr << "Failed to find volumes. Error: " << GetLastError() << std::endl;
		return 1;
	}

	do {
		RemoveTrailingSlashes(volumeName);
		volumes.push_back(volumeName);
	} while (FindNextVolume(hFind, volumeName, MAX_PATH));

	FindVolumeClose(hFind);

	// 遍历所有卷并刷新缓存
	for (const auto& volume : volumes) {
		sync_vol(volume);
	}

	return 0;
}

int fhf(int argc, TCHAR** argv)
{
	if (argc < 2) {
		_ftprintf(stderr, _T("  fhf <options> dir_path1 <option_depth> dir_path2 ...\n"));
		_ftprintf(stderr, _T("    options: \n"));
		_ftprintf(stderr, _T("        -depth N     N default=1\n"));
		_ftprintf(stderr, _T("        -sync        flush volume data to disk(seems not working)\n"));
		return 1;
	}

	int ret = 1;
	int n;
	std::vector<fhf_info> totalhf;
	int depth = 1;
	Cfhf F;
	std::vector<TCHAR> drvs;
	bool bsync = false;
	LPCTSTR output_csv = NULL;

	for (n = 1; n < argc; n++) {
		if ((!_tcsicmp(argv[n], _T("-depth")) || !_tcsicmp(argv[n], _T("-d"))) && n + 1 < argc) {
			depth = _ttoi(argv[n + 1]);
			n++;
		}
		else if (!_tcsicmp(argv[n], _T("-sync"))) {
			bsync = true;
		}
		else if (_tcsicmp(argv[n], _T("-ocsv")) == 0 && (n + 1 < argc)) {
			output_csv = argv[n + 1];
			n++;
		}
		else {
			F.add_dir(argv[n], depth);
		}
	}

	F.uniq_drv(drvs);

	if (drvs.size() == 0) {
		return 1;
	}

	for ( TCHAR drv : drvs ) {
		_ftprintf(stdout, _T("Parsing raw %c:\n"), drv);

		TCHAR szDrive[16];
		TCHAR szDevicePath[MAX_PATH];
		uint64_t part_offset;
		uint64_t part_length;

		_stprintf_s(szDrive, _T("\\\\.\\%c:"), drv);
		if (bsync) {
			sync_vol(szDrive);
		}

		if (!SelectLowerDevice(drv, szDevicePath, part_offset, part_length)) {
			_ftprintf(stderr, _T("Failed to SelectLowerDevice %c:\n"), drv);
			continue;
		}

		if (bsync && _tcsicmp(szDrive, szDevicePath) != 0) {
			sync_vol(szDevicePath);
		}

		if (!F.parse_raw(drv, szDevicePath, part_offset, part_length)) {
			_ftprintf(stderr, _T("Failed to Open Device %c:, error=%d\n"), drv, GetLastError());
			continue;
		}

		RawQueryInfo qi;
		RawDirQueryInfo(F.rds[drv].rd, &qi);
		fwprintf(stdout, L"  Parsed %s On %s at %" PRIu64 L"-%" PRIu64 L"\n", qi.drive.c_str(), qi.devicepath.c_str(), qi.offset, qi.length);

		UINT32 numItems = 0;
		RawDirGetNumberOfItems(F.rds[drv].rd, &numItems);

		fwprintf(stdout, L"  Sorting %" PRIu32 " files\n", numItems);
		F.sort_files(drv);

		fwprintf(stdout, L"  Done\n");
	}

	F.scan(totalhf, 1);

	if (totalhf.size() == 0) {
		_ftprintf(stdout, _T("No found hidden files\n"));
	}
	else {
		_ftprintf(stdout, _T("Found suspicious files:\n"));
		for (auto& it : totalhf) {
			fwprintf(stdout, L"%s: %s\n", it.reason.c_str(), it.filename.c_str());
		}
		_ftprintf(stdout, _T("Total Number of Suspicious Files Found: %d\n"), (int)totalhf.size());
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
			ret = 1;
			goto CLEAN;
		}

		_ftprintf(file, _T("reason,path\n"));
		for (auto& it : totalhf) {
			fwprintf(file, L"%s: %s\n", it.reason.c_str(), it.filename.c_str());
		}
		fclose(file);
	}

	ret = 0;
CLEAN:
	F.clean();
	return ret;
}


int _tmain(int argc, TCHAR **argv)
{
	_ftprintf(stderr, _T("atrk-win: rawdir v1.0.0\n"));

	if (argc < 2) {
		_ftprintf(stderr, _T("Usage:\n  %s <cmd>\n"), argv[0]);
		_ftprintf(stderr, _T("cmd:\n"));
		_ftprintf(stderr, _T("  dir\n"));
		_ftprintf(stderr, _T("  fhf\n"));
		return 1;
	}

	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	setlocale(LC_ALL, "chs");

	if (!_tcsicmp(argv[1], _T("dir"))) {
		return raw_dir(argc-1, argv+1);
	}else if (!_tcsicmp(argv[1], _T("fhf"))) {
		return fhf(argc - 1, argv + 1);
	}

	return 1;
}
