#pragma once

#include <vector>
#include <string.h>
#include "rawdirImpl.h"

struct fhf_info {
	std::wstring filename;
	std::wstring reason;
};


struct FileInfo : public FileItem {
};

struct TrieNode {
	std::unordered_map<std::wstring, TrieNode*> children;
	std::vector<FileInfo> files;
};

std::wstring toLower(const std::wstring& str);
std::wstring ensure_path_without_backslash(LPCWSTR lpszDir);
void RemoveTrailingSlashes(TCHAR* path);

class Trie {
public:
	Trie() {
		root = new TrieNode();
	}

	~Trie() {
		clear(root);
	}

	void insert(const std::wstring& path, const FileInfo& fileInfo) {
		TrieNode* node = root;
		auto parts = splitPath(path);
		for (const auto& part : parts) {
			if (!node->children.count(part)) {
				node->children[part] = new TrieNode();
			}
			node = node->children[part];
		}
		node->files.push_back(fileInfo);
	}

	std::vector<FileInfo> searchByPrefix(const std::wstring& prefix, int depth) {
		TrieNode* node = root;
		auto parts = splitPath(prefix);
		for (const auto& part : parts) {
			if (!node->children.count(part)) {
				return {};
			}
			node = node->children[part];
		}
		return collectFiles(node, depth);
	}

private:
	TrieNode* root;

	void clear(TrieNode* node) {
		for (auto& child : node->children) {
			clear(child.second);
		}
		delete node;
	}

	std::vector<std::wstring> splitPath(const std::wstring& path) {
		std::vector<std::wstring> parts;
		size_t pos = 0, found;
		while ((found = path.find('\\', pos)) != std::wstring::npos) {
			parts.push_back(toLower(path.substr(pos, found - pos)));
			pos = found + 1;
		}
		parts.push_back(toLower(path.substr(pos)));
		return parts;
	}

	std::vector<FileInfo> collectFiles(TrieNode* node, int depth, int currentDepth = 0) {
		if (currentDepth > depth) {
			return {};
		}

		std::vector<FileInfo> result = node->files;
		if (currentDepth < depth) {
			for (auto& child : node->children) {
				auto childFiles = collectFiles(child.second, depth, currentDepth + 1);
				result.insert(result.end(), childFiles.begin(), childFiles.end());
			}
		}
		return result;
	}
};



class Cfhf
{
public:
	std::vector<std::wstring> m_target_dir;
	std::vector<int> m_target_depth;

	struct rdtr {
		void* rd;
		Trie* trie;
	};
	std::map<TCHAR, rdtr> rds;

	Cfhf();
	~Cfhf();

	void add_dir(LPCWSTR lpszDir, int depth);
	int uniq_drv(std::vector<TCHAR> &drvs);
	bool parse_raw(TCHAR drv, LPCWSTR lpszDevicePath, uint64_t offset, uint64_t length);
	bool sort_files(TCHAR drv);
	bool scan(std::vector<fhf_info> &result, int verb);
	void clean();
};


