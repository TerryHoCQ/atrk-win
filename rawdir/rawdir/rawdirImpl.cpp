#include "rawdirImpl.h"
#include <stdio.h>
#include <tchar.h>
#include <assert.h>
#include <iostream>
#include <atltime.h>
#include <locale> 
#include <codecvt>
#include <winioctl.h>
#include "Disks.h"
#include "json.hpp"

//  Wrapper
#include "../7zpp/7zpp.h"
#include "../7zpp/FileSys.h"
#include "../7zpp/ArchiveOpenCallback.h"
#include "../7zpp/ArchiveExtractCallback.h"
#include "../7zpp/InStreamWrapper.h"
#include "../7zpp/PropVariant.h"
#include "../7zpp/UsefulFunctions.h"
#include <InitGuid.h>

using namespace SevenZip;
using namespace SevenZip::intl;
using json = nlohmann::ordered_json;


DEFINE_GUID(CLSID_CFormatFat,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0xDA, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatNtfs,
	0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0xD9, 0x00, 0x00);

enum {
	FileSysType_Unk,
	FileSysType_NTFS,
	FileSysType_FAT,
};

int GetFileSystemType(TCHAR driveLetter) {
	TCHAR rootPath[7];
	TCHAR fileSystemName[MAX_PATH + 1];
	DWORD serialNumber, maxComponentLength, fileSystemFlags;

	// 构造根路径
	_stprintf_s(rootPath, _T("%c:\\"), driveLetter);

	// 获取文件系统信息
	if (GetVolumeInformation(
		rootPath,
		NULL, 0,
		&serialNumber,
		&maxComponentLength,
		&fileSystemFlags,
		fileSystemName,
		sizeof(fileSystemName))) {

		if (_tcscmp(fileSystemName, _T("NTFS")) == 0) {
			return FileSysType_NTFS;
		}
		else if (_tcscmp(fileSystemName, _T("FAT32")) == 0 || _tcscmp(fileSystemName, _T("FAT16")) == 0 || _tcscmp(fileSystemName, _T("FAT")) == 0) {
			return FileSysType_FAT;
		}
	}
	return FileSysType_Unk;
}

class CDevInStream : public IInStream, public IStreamGetSize
{
private:

	long				m_refCount;
	CComPtr< IStream >	m_baseStream;
	uint64_t       m_offset, m_size;

public:
	CDevInStream::CDevInStream(const CComPtr< IStream >& baseStream, uint64_t offset, uint64_t length)
		: m_refCount(0)
		, m_baseStream(baseStream)
		, m_offset(offset)
		, m_size(length)
	{

	}

	CDevInStream::~CDevInStream()
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppvObject)
	{
		if (iid == __uuidof(IUnknown))
		{
			*ppvObject = reinterpret_cast<IUnknown*>(this);
			AddRef();
			return S_OK;
		}

		if (iid == ::IID_ISequentialInStream)
		{
			*ppvObject = static_cast<ISequentialInStream*>(this);
			AddRef();
			return S_OK;
		}

		if (iid == ::IID_IInStream)
		{
			*ppvObject = static_cast<IInStream*>(this);
			AddRef();
			return S_OK;
		}

		if (iid == ::IID_IStreamGetSize)
		{
			*ppvObject = static_cast<IStreamGetSize*>(this);
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef()
	{
		return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
	}

	ULONG STDMETHODCALLTYPE Release()
	{
		ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
		if (res == 0)
		{
			delete this;
		}
		return res;
	}

	STDMETHODIMP Read(void* data, UInt32 size, UInt32* processedSize)
	{
		ULONG read = 0;
		HRESULT hr = m_baseStream->Read(data, size, &read);
		if (processedSize != NULL)
		{
			*processedSize = read;
		}
		// Transform S_FALSE to S_OK
		return SUCCEEDED(hr) ? S_OK : hr;
	}

	STDMETHODIMP Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
	{
		LARGE_INTEGER move;
		ULARGE_INTEGER newPos;

		move.QuadPart = m_offset + offset;

		if (offset == 0 && seekOrigin == STREAM_SEEK_END) {
			*newPosition = m_size;
			return S_OK;
		}

		HRESULT hr = m_baseStream->Seek(move, seekOrigin, &newPos);
		if (newPosition != NULL)
		{
			*newPosition = newPos.QuadPart - m_offset;
		}
		return hr;
	}

	STDMETHODIMP GetSize(UInt64* size)
	{
		*size = m_size;
		return S_OK;
	}

};

class CRawDir
{

protected:
	CComPtr< IInArchive > m_archive;
	CComPtr< ArchiveOpenCallback > m_openCallback;

public:
	SevenZipLibrary m_library;
	TString m_Drive;
	TString m_DevPath;
	uint64_t m_offset;
	uint64_t m_length;
	TString m_fstype;

	CRawDir::CRawDir(const TString& Drive)
		: m_Drive(Drive.substr(0, 2))
		, m_offset(0)
		, m_length(0)
	{
	}

	CRawDir::~CRawDir()
	{
	}

	bool List(TString devicePath, uint64_t offset, uint64_t length, ListCallback* callback /*= nullptr*/)
	{
		if (!Open(devicePath, offset, length)) {
			return false;
		}

		// List command
		UInt32 numItems = 0;
		m_archive->GetNumberOfItems(&numItems);
		for (UInt32 i = 0; i < numItems; i++)
		{
			FileInfo info;
			CPropVariant prop;

			// Get is directory property
			prop.Clear();
			m_archive->GetProperty(i, kpidIsDir, &prop);
			info.IsDirectory = prop.boolVal != VARIANT_FALSE;

			prop.Clear();
			if (info.IsDirectory) {
				info.Size = 0;
			}
			else {
				m_archive->GetProperty(i, kpidSize, &prop);
				if (prop.vt == VT_UI8)
					info.Size = prop.uhVal.QuadPart;
				else
					info.Size = prop.uintVal;
			}
			// Get name of file
			prop.Clear();
			m_archive->GetProperty(i, kpidPath, &prop);

			//valid string?
			if (prop.vt == VT_BSTR)
			{
				info.FileName = m_Drive + _T("\\") + BstrToTString(prop.bstrVal);
			}

			if (info.FileName.size() >= 5 && memcmp(&info.FileName[1], _T(":\\<"), 3*sizeof(TCHAR)) == 0) {
				continue;
			}

			// Get packed size of file
			prop.Clear();
			m_archive->GetProperty(i, kpidPackSize, &prop);
			if (prop.vt == VT_UI8)
				info.PackedSize = prop.uhVal.QuadPart;
			else
				info.PackedSize = prop.uintVal;

			// Get create time
			prop.Clear();
			m_archive->GetProperty(i, kpidCTime, &prop);
			info.CreationTime = prop.filetime;

			// Get last access time
			prop.Clear();
			m_archive->GetProperty(i, kpidATime, &prop);
			info.LastAccessTime = prop.filetime;

			// Get modify time
			prop.Clear();
			m_archive->GetProperty(i, kpidMTime, &prop);
			info.LastWriteTime = prop.filetime;

			// Get Change time
			prop.Clear();
			m_archive->GetProperty(i, kpidChangeTime, &prop);
			if (prop.vt == VT_FILETIME)
				info.ChangeTime = prop.filetime;
			else
				info.ChangeTime.dwHighDateTime = info.ChangeTime.dwLowDateTime = 0;

			// Get attributes
			prop.Clear();
			m_archive->GetProperty(i, kpidAttrib, &prop);
			info.Attributes = prop.uintVal;

			// pass back the found value and call the callback function if set
			if (callback)
			{
				callback->OnFileFound(info);
			}
		}

		if (callback)
		{
			callback->OnListingDone(m_Drive);
		}

		Close();

		return true;
	}

	bool Open(TString devicePath, uint64_t offset, uint64_t length)
	{
		Close();
		m_DevPath = devicePath;
		m_offset = offset;
		m_length = length;
		CComPtr< IStream > fileStream = FileSys::OpenFileToRead(devicePath);
		if (fileStream == NULL) {
			return false;
		}

		UInt64 newPosition;
		CComPtr< CDevInStream > inFile = new CDevInStream(fileStream, offset, length);
		if (FAILED(inFile->Seek(STREAM_SEEK_SET, 0, &newPosition))) {
			return false;
		}

		switch (GetFileSystemType(m_Drive[0])) {
		case FileSysType_NTFS:
			m_fstype = _T("NTFS");
			m_library.CreateObject(CLSID_CFormatNtfs, ::IID_IInArchive, reinterpret_cast<void**>(&m_archive));
			break;
		case FileSysType_FAT:
			m_fstype = _T("FAT");
			m_library.CreateObject(CLSID_CFormatFat, ::IID_IInArchive, reinterpret_cast<void**>(&m_archive));
			break;
		}
		if (!m_archive) {
			return false;
		}

		CComPtr < ISetProperties > spSetProp;
		m_archive->QueryInterface(::IID_ISetProperties, (void**)&spSetProp);
		if (spSetProp) {
			const wchar_t *names[1] = {L"ls"};
			PROPVARIANT props[1];
			props[0].vt = VT_BOOL;
			props[0].boolVal = VARIANT_FALSE;
			spSetProp->SetProperties(names, props, _countof(props));
		}

		m_openCallback = new ArchiveOpenCallback(_T(""));
		HRESULT hr = m_archive->Open(inFile, 0, m_openCallback);
		if (hr != S_OK)
		{
			return false;
		}

		return true;
	}
	bool GetNumberOfItems(UInt32 *num)
	{
		*num = 0;
		if (!m_archive) {
			return false;
		}
		if (FAILED(m_archive->GetNumberOfItems(num))) {
			return false;
		}
		return true;
	}
	bool GetFileItem(UInt32 i, FileItem &info)
	{
		if (!m_archive) {
			return false;
		}

		// Get uncompressed size of file
		CPropVariant prop;

		info.index = i;

		// Get is directory property
		prop.Clear();
		m_archive->GetProperty(i, kpidIsDir, &prop);
		info.IsDirectory = prop.boolVal != VARIANT_FALSE;

		prop.Clear();
		if (info.IsDirectory) {
			info.Size = 0;
		}
		else {
			m_archive->GetProperty(i, kpidSize, &prop);
			if (prop.vt == VT_UI8)
				info.Size = prop.uhVal.QuadPart;
			else
				info.Size = prop.uintVal;
		}
		// Get name of file
		prop.Clear();
		m_archive->GetProperty(i, kpidPath, &prop);

		//valid string?
		if (prop.vt == VT_BSTR)
		{
			info.FileName = m_Drive + _T("\\") + BstrToTString(prop.bstrVal);
		}

		if (info.FileName.size() >= 5 && memcmp(&info.FileName[1], L":\\<", 3*sizeof(WCHAR)) == 0) {
			return false;
		}

		// Get create time
		prop.Clear();
		m_archive->GetProperty(i, kpidCTime, &prop);
		info.CreationTime = prop.filetime;

		// Get last access time
		prop.Clear();
		m_archive->GetProperty(i, kpidATime, &prop);
		info.LastAccessTime = prop.filetime;

		// Get modify time
		prop.Clear();
		m_archive->GetProperty(i, kpidMTime, &prop);
		info.LastWriteTime = prop.filetime;

		// Get attributes
		prop.Clear();
		m_archive->GetProperty(i, kpidAttrib, &prop);
		info.Attributes = prop.uintVal;

		return true;
	}

	void Close()
	{
		if (m_openCallback) {
			m_openCallback = NULL;
		}
		if (m_archive) {
			m_archive->Close();
			m_archive = NULL;
		}
	}

};

ULONGLONG ft2ull(const FILETIME& ft)
{
	ULARGE_INTEGER uli;
	uli.HighPart = ft.dwHighDateTime;
	uli.LowPart = ft.dwLowDateTime;
	return uli.QuadPart;
}

TString tostring(ULONGLONG n)
{
#ifdef _UNICODE
	return std::to_wstring(n);
#else
	return std::to_string(n);
#endif
}

std::string wstring_to_utf8(const std::wstring& wstr) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.to_bytes(wstr);
}


static bool WriteFileAll(HANDLE hFile, const void* data, DWORD len)
{
	static DWORD maxchunksize = 1024 * 1024;
	DWORD cbW;
	DWORD bs;
	while (len > 0)
	{
		if (len >= maxchunksize)
			bs = maxchunksize;
		else
			bs = len;
		if (!WriteFile(hFile, data, bs, &cbW, NULL)) {
			if (cbW == 0 && GetLastError() == ERROR_NOT_ENOUGH_MEMORY && maxchunksize >= 1024) {
				maxchunksize /= 2;
				continue;
			}
			return false;
		}

		data = (char*)data + cbW;
		len -= cbW;
	}
	return true;
}

class RawListCallBackOutputEFU : ListCallback
{
public:
	TString mOutput;
	HANDLE m_hOutputFile;

	RawListCallBackOutputEFU(HANDLE hOutputFile) {
		m_hOutputFile = hOutputFile;
		mOutput.reserve(1024 * 1024 * 2 + 4096);
		mOutput = _T("Filename,Size,Date Modified,Date Created,Date Accessed,Date Changed,Attributes\n");
	}

	~RawListCallBackOutputEFU() = default;

	virtual void OnFileFound(const intl::FileInfo& info)
	{
		mOutput += _T("\"");
		mOutput += info.FileName;
		mOutput += _T("\",");
		if (!info.IsDirectory)
			mOutput += tostring(info.Size);
		mOutput += _T(",");
		mOutput += tostring(ft2ull(info.LastWriteTime));
		mOutput += _T(",");
		mOutput += tostring(ft2ull(info.CreationTime));
		mOutput += _T(",");
		mOutput += tostring(ft2ull(info.LastAccessTime));
		mOutput += _T(",");
		mOutput += tostring(ft2ull(info.ChangeTime));
		mOutput += _T(",");
		mOutput += tostring(info.Attributes);
		mOutput += _T("\n");
		FlushIfNeeded(false);
	}

	virtual void OnListingDone(const TString& path)
	{
		FlushIfNeeded(true);
	}

	void FlushIfNeeded(bool force)
	{
		if (force || mOutput.size() >= 1024 * 1024 * 2) {
#ifdef _UNICODE
			std::string data = wstring_to_utf8(mOutput);
			WriteFileAll(m_hOutputFile, &data[0], (DWORD)data.size());
#else
			WriteFileAll(m_hOutputFile, &mOutput[0], (DWORD)mOutput.size());
#endif
			mOutput.clear();
		}
	}

};

BOOL queryVolumeInfo(json &jdisks, TCHAR MountLetter, LPTSTR lpszPhysicalDrive, uint64_t & offset, uint64_t & length)
{
	int i, j;

	for (i = 0; i < (int)jdisks.size(); i++)
	{
		json disk = jdisks[i];
		json property = disk["property"];
		int index = disk["index"];
		json partitions = disk["partitions"];

		for (j = 0; j < (int)partitions.size(); j++)
		{
			json part = partitions[j];
			std::string mount = part["mount"].get<std::string>();
			if (mount.size() && std::toupper((char)MountLetter) == std::toupper(mount[0])) {
				length = part["length"];
				offset = part["offset"];

				int bitlocker = part["bitlocker"];
				if (bitlocker == 0 || bitlocker == -1) {
					wsprintf(lpszPhysicalDrive, L"\\\\.\\PhysicalDrive%d", index);
					return TRUE;
				}
				else {
					break;
				}
			}

		}
	}
	return FALSE;
}

void RawListFiles(LPCTSTR lpszDriveLetters, HANDLE hOutput)
{
	std::string text;
	json jdisks;
	text = enumPartitions();
	jdisks = json::parse(text);

	if (hOutput == NULL) {
		hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	}
	RawListCallBackOutputEFU myListCallBack(hOutput);

	DWORD tick = GetTickCount();
	TCHAR szDrive[5];
	TCHAR chDriveLetter;
	TCHAR szDevicePath[MAX_PATH];
	uint64_t part_offset, part_length;

	while (chDriveLetter = *(lpszDriveLetters++)) {
		if (!isalpha(chDriveLetter)) {
			continue;
		}
		chDriveLetter = (TCHAR)std::toupper(chDriveLetter);
		_stprintf_s(szDrive, _T("%c:"), chDriveLetter);
		CRawDir lister(szDrive);
		part_offset = part_length = 0;
		if (queryVolumeInfo(jdisks, chDriveLetter, szDevicePath, part_offset, part_length)) {
			_ftprintf(stderr, _T(" - %s Mounted on %s, @%s-%s\n"), 
				szDrive, szDevicePath, tostring(part_offset).c_str(), tostring(part_length).c_str());
		}
		else if (part_length){
			_stprintf_s(szDevicePath, _T("\\\\.\\%c:"), chDriveLetter);
			part_offset = 0;
		}
		else {
			_ftprintf(stderr, _T("Failed to query volume(%s) info\n"), szDrive);
			continue;
		}

		_ftprintf(stderr, _T(" - Listing %s on %s... \n"), szDrive, szDevicePath);
		if (!lister.List(szDevicePath, part_offset, part_length,(ListCallback*)&myListCallBack)) {
			_ftprintf(stderr, _T("Failed to list %s\n"), szDrive);
		}
	}

	_ftprintf(stderr, _T("took %d seconds\n"), (GetTickCount() - tick) / 1000);
}


bool SelectLowerDevice(TCHAR chDriveLetter, LPTSTR lpszDevicePath, uint64_t &part_offset, uint64_t &part_length)
{
	std::string text;
	json jdisks;
	text = enumPartitions();
	jdisks = json::parse(text);

	part_offset = part_length = 0;
	if (queryVolumeInfo(jdisks, chDriveLetter, lpszDevicePath, part_offset, part_length)) {
		return true;
	}
	else if (part_length) {
		_stprintf_s(lpszDevicePath, MAX_PATH, _T("\\\\.\\%c:"), chDriveLetter);
		part_offset = 0;
		return true;
	}
	return false;
}

void* RawDirOpen(TCHAR chDriveLetter, LPCWSTR lpszDevicePath, uint64_t offset, uint64_t length)
{
	DWORD tick = GetTickCount();
	TCHAR szDrive[5];

	chDriveLetter = (TCHAR)std::toupper(chDriveLetter);
	_stprintf_s(szDrive, _T("%c:"), chDriveLetter);
	CRawDir * lister = new CRawDir(szDrive);

	if (!lister->Open(lpszDevicePath, offset, length)) {
		DWORD err = GetLastError();
		delete lister;
		SetLastError(err);
		return NULL;
	}
	return lister;
}

bool RawDirQueryInfo(void* ctx, RawQueryInfo* qi)
{
	CRawDir* lister = (CRawDir*)ctx;

	qi->drive = lister->m_Drive;
	qi->devicepath = lister->m_DevPath;
	qi->offset = lister->m_offset;
	qi->length = lister->m_length;
	qi->fstype = lister->m_fstype;
	return true;
}

bool RawDirGetNumberOfItems(void* ctx, unsigned int* num)
{
	CRawDir* lister = (CRawDir*)ctx;

	return lister->GetNumberOfItems(num);
}

bool RawDirGetFileItem(void* ctx, unsigned int index, FileItem& fi)
{
	CRawDir* lister = (CRawDir*)ctx;

	return lister->GetFileItem(index, fi);
}

void RawDirClose(void* ctx)
{
	CRawDir* lister = (CRawDir*)ctx;
	lister->Close();
	delete lister;
}
