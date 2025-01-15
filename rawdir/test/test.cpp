#include <stdio.h>
#include <tchar.h>
#include <assert.h>
#include <iostream>
#include <atltime.h>
#include <locale> 
#include <codecvt>

//  Wrapper
#include "../7zpp/7zpp.h"

#define ASSERT_EQ(x, y) assert(!!x == !!y)
#define EXPECT_EQ(x, y) assert(!!x == !!y)


class CompProgressCallback : public SevenZip::ProgressCallback
{
public:
	unsigned __int64 m_totalBytes;
	/*
	Called at beginning
	*/
	virtual void OnStartWithTotal(const SevenZip::TString& archivePath, unsigned __int64 totalBytes)
	{
		m_totalBytes = totalBytes;
	}

	/*
	Called Whenever progress has updated with a bytes complete
	*/
	virtual void OnProgress(const SevenZip::TString& archivePath, unsigned __int64 bytesCompleted)
	{
		if (m_totalBytes)
		{
			double p = (double)bytesCompleted * 100 / m_totalBytes;
			printf("%.2f\n", (float)p);
		}
	}


	/*
	Called When progress has reached 100%
	*/
	virtual void OnDone(const SevenZip::TString& archivePath)
	{
		printf("OnDone\n");
	}

	/*
	Called When single file progress has reached 100%, returns the filepath that completed
	*/
	virtual void OnFileDone(const SevenZip::TString& archivePath, const SevenZip::TString& filePath, unsigned __int64 bytesCompleted)
	{
		_tprintf(_T("OnFileDone: %s\n"), filePath.c_str());
	}

	/*
	Called to determine if it's time to abort the zip operation. Return true to abort the current operation.
	*/
	virtual bool OnCheckBreak()
	{
		return false;
	}

	virtual bool OnOpenFile(const SevenZip::TString& filePath, HRESULT hr)
	{
		_tprintf(_T("OnOpenFile(%s): %s\n"), SUCCEEDED(hr) ? _T("ok") : _T("err"), filePath.c_str());

		return true;
	}

};
//
// Test compression
//
void CompressFiles_Test1()
{
	SevenZip::SevenZipLibrary lib;

	SevenZip::TString myArchive(_T("test1"));

	SevenZip::SevenZipCompressor compressor(lib, myArchive);
	compressor.SetCompressionFormat(SevenZip::CompressionFormat::SevenZip);
	compressor.SetPassword(_T("test"), true);
	bool addResult;
	addResult = compressor.AddFile(_T("TestFiles\\ReadMe.md"));
	EXPECT_EQ(addResult, true);

	addResult = compressor.AddAllFiles(_T("TestFiles\\dir\\"));
	EXPECT_EQ(addResult, true);

	std::string str = "Just a string in a memory";
	addResult = compressor.AddMemory(_T("memory.txt"), (void*)str.c_str(), str.size());
	EXPECT_EQ(addResult, true);

	addResult = compressor.AddMemory(_T("memory\\中文.txt"), (void*)str.c_str(), str.size());
	EXPECT_EQ(addResult, true);

	CompProgressCallback progressCB;

	bool compressResult = compressor.DoCompress(&progressCB);
	EXPECT_EQ(compressResult, true);
}

//
// Lister callback
//
class ListCallBackOutput : SevenZip::ListCallback
{
public:
	~ListCallBackOutput() = default;

	virtual void OnFileFound(const SevenZip::intl::FileInfo& info)
	{
		m_files.push_back(info);
	}

	virtual void OnListingDone(const SevenZip::TString& path)
	{
	}

	const std::vector<SevenZip::FileInfo>& GetList() const { return m_files; }

protected:
	std::vector<SevenZip::FileInfo> m_files;
};

void ExtractFiles_Test1()
{
	SevenZip::SevenZipLibrary lib;
	SevenZip::CompressionFormatEnum myCompressionFormat = SevenZip::CompressionFormat::SevenZip;

	SevenZip::TString myArchive(myCompressionFormat == SevenZip::CompressionFormat::Zip ? _T("test1.zip") : _T("test1.7z"));
	SevenZip::TString myDest;
	TCHAR szCurrDir[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, szCurrDir);
	myDest = szCurrDir;
	myDest += _T("\\tmp");
	CreateDirectory(myDest.c_str(), NULL);

	//
	// Lister
	//
	ListCallBackOutput myListCallBack;

	SevenZip::SevenZipLister lister(lib, myArchive);

	lister.SetCompressionFormat(myCompressionFormat);
	bool result = lister.ListArchive(_T("test"), (SevenZip::ListCallback*)&myListCallBack);
	EXPECT_EQ(true, result);
	EXPECT_EQ(false, myListCallBack.GetList().empty());

	//
	// Extract
	//
	SevenZip::SevenZipExtractor extractor(lib, myArchive);
	extractor.SetCompressionFormat(myCompressionFormat);
	extractor.SetPassword(_T("test"));
	//UINT indces[2] = { 1, 4 };
	//result = extractor.ExtractFilesFromArchive(indces, 2, myDest);
	//ASSERT_EQ(true, result);

	result = extractor.ExtractArchive(myDest);
	ASSERT_EQ(true, result);

}


int main()
{
	CompressFiles_Test1();
	ExtractFiles_Test1();
}
