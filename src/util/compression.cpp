#include "util.h"
#include "zip_reader.h"

#include <fstream>

namespace raidhook
{
	namespace
	{
		bool WriteFile(const std::string& extractPath, const std::string& name, const std::string& data)
		{
			const std::string finalWritePath = extractPath + "/" + name;
			Util::EnsurePathWritable(finalWritePath);

			std::ofstream outFile(finalWritePath.c_str(), std::ios::binary);
			if (!outFile)
			{
				RAIDHOOK_LOG_WARN(std::string("Failed to extract file: ") + finalWritePath);
				return false;
			}

			outFile.write(data.data(), static_cast<std::streamsize>(data.size()));
			outFile.close();
			if (!outFile)
			{
				RAIDHOOK_LOG_WARN(std::string("Failed to write file: ") + finalWritePath);
				return false;
			}
			return true;
		}
	}

	bool ExtractZIPArchive(const std::string& path, const std::string& extractPath)
	{
		zip::Archive archive(path);
		std::string error;
		if (!archive.Open(error))
		{
			RAIDHOOK_LOG_WARN(std::string("Cannot extract ") + path + ": " + error);
			return false;
		}

		// Check every entry before writing anything, so a bad archive doesn't leave a half-extracted mod
		for (const zip::EntryInfo& entry : archive.Entries())
		{
			if (!zip::IsSafeEntryPath(entry.name))
			{
				RAIDHOOK_LOG_WARN(std::string("Refusing to extract unsafe path: ") + entry.name);
				return false;
			}
		}

		RAIDHOOK_LOG_LOG(std::string("Extracting ") + path + std::string(" to ") + extractPath);
		bool result = true;
		for (const zip::EntryInfo& entry : archive.Entries())
		{
			// Directories are created automatically when a file is written inside them
			if (entry.IsDirectory())
				continue;

			std::string data;
			if (!archive.ReadEntry(entry, data, error))
			{
				RAIDHOOK_LOG_WARN(std::string("Cannot extract ") + path + ": " + error);
				return false;
			}
			result &= WriteFile(extractPath, entry.name, data);
		}
		return result;
	}
}
