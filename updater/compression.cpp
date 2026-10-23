#include <zlib.h>
#include "../src/util/zip_reader.h"

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>


#include <algorithm>
#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <string>
#include <vector>
#include <sstream>

namespace raidhook
{
	namespace
	{
		bool CreateDirectorySingle(const std::string& path)
		{
			return CreateDirectory(path.c_str(), NULL);
		}

		void SplitString(const std::string &s, char delim, std::vector<std::string> &elems)
		{
			std::istringstream ss(s);
			std::string item;
			while (std::getline(ss, item, delim))
			{
				if (!item.empty())
				{
					elems.push_back(item);
				}
			}
		}

		std::vector<std::string> SplitString(const std::string &s, char delim)
		{
			std::vector<std::string> elems;
			SplitString(s, delim, elems);
			return elems;
		}

		bool CreateDirectoryPath(const std::string& path)
		{
			std::string newPath = "";
			std::vector<std::string> paths = SplitString(path, '/');
			for (const auto& i : paths)
			{
				newPath = newPath + i + "/";
				CreateDirectorySingle(newPath);
			}
			return true;
		}

		bool DirectoryExists(const std::string& dir)
		{
			std::string clean = dir;

			if (clean[clean.length() - 1] == '/')
			{
				clean.erase(clean.end() - 1);
			}

			DWORD ftyp = GetFileAttributes(clean.c_str());
			if (ftyp == INVALID_FILE_ATTRIBUTES) return false;
			if (ftyp & FILE_ATTRIBUTE_DIRECTORY) return true;
			return false;
		}

		void EnsurePathWritable(const std::string& path)
		{
			int finalSlash = path.find_last_of('/');
			std::string finalPath = path.substr(0, finalSlash);
			if (DirectoryExists(finalPath)) return;
			CreateDirectoryPath(finalPath);
		}

		bool WriteFile(const std::string& extractPath, const std::string& name, const std::string& data)
		{
			const std::string finalWritePath = extractPath + "/" + name;
			EnsurePathWritable(finalWritePath);

			std::ofstream outFile(finalWritePath.c_str(), std::ios::binary);
			if (!outFile)
				return false;

			outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
			outFile.close();
			return static_cast<bool>(outFile);
		}
	}

	bool ExtractZIPArchive(const std::string& path, const std::string& extractPath)
	{
		zip::Archive archive(path);
		std::string error;
		if (!archive.Open(error))
			return false;

		// Check every entry before writing anything
		for (const zip::EntryInfo& entry : archive.Entries())
		{
			if (!zip::IsSafeEntryPath(entry.name))
				return false;
		}

		bool result = true;
		for (const zip::EntryInfo& entry : archive.Entries())
		{
			// Directories are created automatically when a file is written inside them
			if (entry.IsDirectory())
				continue;

			std::string data;
			if (!archive.ReadEntry(entry, data, error))
				return false;
			result &= WriteFile(extractPath, entry.name, data);
		}
		return result;
	}
}
