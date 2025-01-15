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
#include "fhf.h"
#include <unordered_set>

#pragma comment(lib, "Shlwapi.lib")


void RemoveTrailingSlashes(TCHAR* path) {
	if (path == nullptr) {
		return;
	}

	size_t len = _tcslen(path);
	while (len > 0) {
		TCHAR lastChar = path[len - 1];
		if (lastChar == _T('\\') || lastChar == _T('/')) {
			path[len - 1] = _T('\0');
			len--;
		}
		else {
			break;
		}
	}
}

std::wstring ensure_path_without_backslash(LPCWSTR lpszDir)
{
	std::wstring basedir;
	TCHAR tmp[MAX_PATH];
	_tcscpy_s(tmp, lpszDir);
	RemoveTrailingSlashes(tmp);
	basedir = tmp;
	return basedir;
}

std::wstring toLower(const std::wstring& str) {
	std::wstring result = str;
	std::transform(result.begin(), result.end(), result.begin(),
		[](TCHAR c) { return std::tolower(c); });
	return result;
}

int fhf_m0(std::wstring basedir, std::map<std::wstring, FileInfo>& normallyls, int depth)
{
	if (depth < 1) return 0;

	WIN32_FIND_DATAW findData;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	basedir = ensure_path_without_backslash(basedir.c_str());
	std::wstring searchPath = basedir + L"\\*";

	hFind = FindFirstFileW(searchPath.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return -1;
	}

	do {
		if (!wcscmp(findData.cFileName, L".") || !wcscmp(findData.cFileName, L"..")) continue;

		std::wstring fullPath = basedir + L"\\" + findData.cFileName;

		FileInfo info;
		LARGE_INTEGER lisz;
		info.index = -1;
		info.Attributes = findData.dwFileAttributes;
		info.LastAccessTime = findData.ftLastAccessTime;
		info.CreationTime = findData.ftCreationTime;
		info.LastWriteTime = findData.ftLastWriteTime;
		lisz.LowPart = findData.nFileSizeLow;
		lisz.HighPart = findData.nFileSizeHigh;
		info.Size = lisz.QuadPart;
		info.IsDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		info.FileName = fullPath;

		normallyls[toLower(fullPath)] = info;

		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (depth > 1) {
				fhf_m0(fullPath, normallyls, depth - 1);
			}
		}

	} while (FindNextFileW(hFind, &findData) != 0);

	FindClose(hFind);
	return 0;
}

int fhf_m1(Trie* trie, LPCTSTR lpszDir, int depth, std::vector<fhf_info>& result, int verb)
{
	std::map<std::wstring, FileInfo> files0;
	std::vector<FileInfo> files1;
	std::wstring basedir;
	basedir = ensure_path_without_backslash(lpszDir);
	size_t files1_count = 0;
	int nDiff = 0;

	if (verb)
		_ftprintf(stdout, _T("Method 1: Finding Hidden Files Begins. Target Directory: %s\n"), basedir.c_str());
	files1 = trie->searchByPrefix(basedir, depth);

	if (verb)
		_ftprintf(stdout, _T("Listing Files Using Normal API: FindFirstFile and FindNextFile (Method 0).\n"));
	fhf_m0(basedir, files0, depth);

	//auto& itx = files0.find(L"c:\\windows\\system32\\es.dll");
	//if (itx != files0.end()) {
	//	_ftprintf(stdout, _T("deleted es\n"));
	//	files0.erase(itx);
	//}
	//itx = files0.find(L"c:\\windows\\system32\\drivers\\tcpip.sys");
	//if (itx != files0.end()) {
	//	_ftprintf(stdout, _T("deleted bthpan\n"));
	//	files0.erase(itx);
	//}

	files1_count = files1.size();
	std::vector<FileInfo>::iterator it = files1.begin();
	if (it != files1.end() && it->IsDirectory && _wcsicmp(it->FileName.c_str(), basedir.c_str()) == 0) {
		it++;
		files1_count--;
	}

	if (verb)
	{
		_ftprintf(stdout, _T("Method 0: Number of Files Listed: %d\n"), (int)files0.size());
		_ftprintf(stdout, _T("Method 1: Number of Files Listed: %d\n"), (int)files1_count);
	}

	for (; it != files1.end(); it++)
	{
		FileInfo& fi = *it;
		auto& f0 = files0.find(toLower(fi.FileName));
		if (f0 == files0.end()) {
			fwprintf(stdout, _T("0: \n"));
			fwprintf(stdout, _T("1: %s, Size: %" PRIu64 L"\n\n"), fi.FileName.c_str(), fi.Size);
			nDiff++;
			fhf_info fhfi;
			fhfi.filename = fi.FileName;
			fhfi.reason = L"Hidden";
			result.push_back(fhfi);
		}
		else if (f0->second.Size != fi.Size) {
			fwprintf(stdout, _T("0: %s, Size: %" PRIu64 L"\n"), f0->second.FileName.c_str(), f0->second.Size);
			fwprintf(stdout, _T("1: %s, Size: %" PRIu64 L"\n\n"), fi.FileName.c_str(), fi.Size);
			nDiff++;
			fhf_info fhfi;
			fhfi.filename = fi.FileName;
			fhfi.reason = L"DiffSize";
			result.push_back(fhfi);
		}
	}
	if (!nDiff && verb)
	{
		_ftprintf(stdout, _T("No found hidden files in %s\n\n"), basedir.c_str());
	}

	return nDiff;
}

BOOL __Wow64DisableWow64FsRedirection(PVOID* OldValue)
{
	typedef BOOL WINAPI DEF_Wow64DisableWow64FsRedirection(
		_Out_ PVOID* OldValue
	);

	static DEF_Wow64DisableWow64FsRedirection* api = NULL;
	if (api == NULL) {
		HMODULE hK32 = GetModuleHandleA("kernel32.dll");
		*(void**)&api = GetProcAddress(hK32, "Wow64DisableWow64FsRedirection");
		if (api == NULL)
			return FALSE;
	}

	return api(OldValue);
}


Cfhf::Cfhf()
{
}

Cfhf::~Cfhf()
{
	clean();
}

void Cfhf::add_dir(LPCWSTR lpszDir, int depth)
{
	m_target_dir.push_back(lpszDir);
	m_target_depth.push_back(depth);
}

int Cfhf::uniq_drv(std::vector<TCHAR>& drvs)
{
	int n = 0;
	std::unordered_set<TCHAR> seen;

	for (auto& it : m_target_dir) {
		if (seen.insert(std::toupper(it[0])).second) {
			drvs.push_back(std::toupper(it[0]));
			n++;
		}
	}
	return n;
}

bool Cfhf::parse_raw(TCHAR drv, LPCWSTR lpszDevicePath, uint64_t offset, uint64_t length)
{
	void* rd = NULL;
	for (int t = 0; t < 2; t++) {
		rd = RawDirOpen(drv, lpszDevicePath, offset, length);
		if (rd) {
			break;
		}
		Sleep(2000);
	}
	if (!rd)
		return false;

	rdtr rdr;
	rdr.rd = rd;
	rdr.trie = NULL;
	rds[drv] = rdr;
	return true;
}

bool Cfhf::sort_files(TCHAR drv)
{
	auto & rdr_it = rds.find(drv);
	if (rdr_it == rds.end()) {
		return false;
	}
	rdtr& rdr = rdr_it->second;
	rdr.trie = new Trie();

	UINT32 numItems = 0;
	RawDirGetNumberOfItems(rdr.rd, &numItems);

	for (UINT32 i = 0; i < numItems; i++)
	{
		FileInfo info;
		if (RawDirGetFileItem(rdr.rd, i, info)) {
			rdr.trie->insert(info.FileName, info);
		}
	}
	return true;
}

bool Cfhf::scan(std::vector<fhf_info>& result, int verb)
{
	void* FsRedirectionOldValue = NULL;
	__Wow64DisableWow64FsRedirection(&FsRedirectionOldValue);

	for (size_t i = 0; i < m_target_dir.size(); i++)
	{
		TCHAR drv = std::toupper(m_target_dir[i][0]);
		auto& rdr_it = rds.find(drv);
		if (rdr_it == rds.end()) {
			return false;
		}
		rdtr& rdr = rdr_it->second;
		if (!rdr.trie) {
			//ªπ√ª≈≈–Ú
			return false;
		}

		fhf_m1(rdr.trie, m_target_dir[i].c_str(), m_target_depth[i], result, verb);
	}

	return true;
}

void Cfhf::clean()
{
	for (auto& it : rds) {
		RawDirClose(it.second.rd);
		delete it.second.trie;
	}
	rds.clear();
}
