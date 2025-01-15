#pragma once
#include <windows.h> 
#include <string.h>
#include <vector>


void RawListFiles(LPCTSTR lpszDriveLetters, HANDLE hOutput);

struct FileItem
{
	std::wstring	FileName;
	UINT        index;
	FILETIME	LastWriteTime = { 0 };
	FILETIME	CreationTime = { 0 };
	FILETIME	LastAccessTime = { 0 };
	ULONGLONG	Size = 0ULL;
	UINT		Attributes = 0;
	bool		IsDirectory = false;
};

struct RawQueryInfo
{
	std::wstring drive;
	std::wstring devicepath;
	std::wstring fstype;
	uint64_t offset, length;
};

bool SelectLowerDevice(TCHAR chDriveLetter, LPTSTR lpszDevicePath, uint64_t &part_offset, uint64_t &part_length);
void* RawDirOpen(TCHAR chDriveLetter, LPCWSTR lpszDevicePath, uint64_t offset, uint64_t length);
bool RawDirQueryInfo(void* ctx, RawQueryInfo* qi);
bool RawDirGetNumberOfItems(void* ctx, unsigned int* num);
bool RawDirGetFileItem(void* ctx, unsigned int index, FileItem &fi);
void RawDirClose(void* ctx);

