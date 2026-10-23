#pragma once

// Minimal ZIP reader for stored and deflated entries (no zip64, no encryption).
// It works from the central directory, so archives with data descriptors (streamed zips) are fine.
// Header-only and free of other SuperBLT dependencies, as it is shared with the DLL updater.

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace raidhook
{
	namespace zip
	{
		struct EntryInfo
		{
			std::string name;
			uint16_t method = 0;
			uint32_t crc32 = 0;
			uint32_t compressedSize = 0;
			uint32_t uncompressedSize = 0;
			uint32_t localHeaderOffset = 0;

			bool IsDirectory() const
			{
				return !name.empty() && (name.back() == '/' || name.back() == '\\');
			}
		};

		// Rejects empty names, absolute paths, drive letters and any ".." component (zip slip)
		inline bool IsSafeEntryPath(std::string path)
		{
			std::replace(path.begin(), path.end(), '\\', '/');
			if (path.empty() || path[0] == '/' || path.find(':') != std::string::npos)
				return false;

			size_t start = 0;
			while (start <= path.size())
			{
				size_t end = path.find('/', start);
				if (end == std::string::npos)
					end = path.size();
				if (path.compare(start, end - start, "..") == 0)
					return false;
				start = end + 1;
			}
			return true;
		}

		class Archive
		{
		  public:
			explicit Archive(const std::string& path) : path(path) {}

			// Reads and validates the central directory
			bool Open(std::string& error)
			{
				stream.open(path, std::ios::binary);
				if (!stream)
				{
					error = "cannot open file";
					return false;
				}

				stream.seekg(0, std::ios::end);
				fileSize = static_cast<uint64_t>(stream.tellg());
				if (fileSize < EocdSize)
				{
					error = "not a ZIP archive";
					return false;
				}

				// The end of central directory record is within the last 64 KiB (maximum comment length)
				const size_t tailSize = static_cast<size_t>(std::min<uint64_t>(fileSize, EocdSize + 0xFFFF));
				std::string tail;
				if (!ReadAt(fileSize - tailSize, tailSize, tail))
				{
					error = "cannot read the end of the file";
					return false;
				}

				size_t eocd = std::string::npos;
				for (size_t i = tailSize - EocdSize + 1; i-- > 0;)
				{
					if (Le32(tail, i) == EocdSignature)
					{
						eocd = i;
						break;
					}
				}
				if (eocd == std::string::npos)
				{
					error = "not a ZIP archive (no central directory)";
					return false;
				}

				const uint32_t entryCount = Le16(tail, eocd + 10);
				const uint32_t directorySize = Le32(tail, eocd + 12);
				const uint32_t directoryOffset = Le32(tail, eocd + 16);
				if (entryCount == 0xFFFF || directorySize == 0xFFFFFFFFu || directoryOffset == 0xFFFFFFFFu)
				{
					error = "zip64 archives are not supported";
					return false;
				}
				if (static_cast<uint64_t>(directoryOffset) + directorySize > fileSize)
				{
					error = "invalid central directory";
					return false;
				}

				std::string directory;
				if (!ReadAt(directoryOffset, directorySize, directory))
				{
					error = "cannot read the central directory";
					return false;
				}

				size_t pos = 0;
				for (uint32_t i = 0; i < entryCount; i++)
				{
					if (pos + CentralHeaderSize > directory.size() || Le32(directory, pos) != CentralSignature)
					{
						error = "invalid central directory entry";
						return false;
					}

					const uint16_t flags = Le16(directory, pos + 8);
					const size_t nameLength = Le16(directory, pos + 28);
					const size_t extraLength = Le16(directory, pos + 30);
					const size_t commentLength = Le16(directory, pos + 32);
					if (pos + CentralHeaderSize + nameLength + extraLength + commentLength > directory.size())
					{
						error = "invalid central directory entry";
						return false;
					}

					EntryInfo entry;
					entry.name = directory.substr(pos + CentralHeaderSize, nameLength);
					entry.method = Le16(directory, pos + 10);
					entry.crc32 = Le32(directory, pos + 16);
					entry.compressedSize = Le32(directory, pos + 20);
					entry.uncompressedSize = Le32(directory, pos + 24);
					entry.localHeaderOffset = Le32(directory, pos + 42);
					pos += CentralHeaderSize + nameLength + extraLength + commentLength;

					if (flags & 1)
					{
						error = "encrypted entries are not supported: " + entry.name;
						return false;
					}
					if (entry.method != 0 && entry.method != 8)
					{
						error = "unsupported compression method in entry: " + entry.name;
						return false;
					}
					if (entry.compressedSize == 0xFFFFFFFFu || entry.uncompressedSize == 0xFFFFFFFFu ||
					    entry.localHeaderOffset == 0xFFFFFFFFu)
					{
						error = "zip64 entries are not supported: " + entry.name;
						return false;
					}
					if (entry.uncompressedSize > MaxEntrySize)
					{
						error = "entry is too large: " + entry.name;
						return false;
					}

					entries.push_back(std::move(entry));
				}

				return true;
			}

			const std::vector<EntryInfo>& Entries() const { return entries; }

			// Reads and decompresses one entry, and verifies its size and CRC
			bool ReadEntry(const EntryInfo& entry, std::string& out, std::string& error)
			{
				std::string local;
				if (!ReadAt(entry.localHeaderOffset, LocalHeaderSize, local) || Le32(local, 0) != LocalSignature)
				{
					error = "invalid local header: " + entry.name;
					return false;
				}

				// The name and extra field lengths of the local header may differ from the central directory
				const uint64_t dataOffset = static_cast<uint64_t>(entry.localHeaderOffset) + LocalHeaderSize +
				                            Le16(local, 26) + Le16(local, 28);
				if (dataOffset + entry.compressedSize > fileSize)
				{
					error = "truncated entry: " + entry.name;
					return false;
				}

				std::string compressed;
				if (!ReadAt(dataOffset, entry.compressedSize, compressed))
				{
					error = "cannot read entry: " + entry.name;
					return false;
				}

				if (entry.method == 0)
				{
					if (entry.compressedSize != entry.uncompressedSize)
					{
						error = "size mismatch in entry: " + entry.name;
						return false;
					}
					out = std::move(compressed);
				}
				else if (entry.uncompressedSize == 0)
				{
					out.clear();
				}
				else if (!Inflate(compressed, entry.uncompressedSize, out))
				{
					error = "failed to inflate entry: " + entry.name;
					return false;
				}

				const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(out.data()), static_cast<uInt>(out.size()));
				if (static_cast<uint32_t>(crc) != entry.crc32)
				{
					error = "CRC mismatch in entry: " + entry.name;
					return false;
				}
				return true;
			}

		  private:
			static constexpr uint32_t EocdSignature = 0x06054b50;
			static constexpr uint32_t CentralSignature = 0x02014b50;
			static constexpr uint32_t LocalSignature = 0x04034b50;
			static constexpr size_t EocdSize = 22;
			static constexpr size_t CentralHeaderSize = 46;
			static constexpr size_t LocalHeaderSize = 30;
			static constexpr uint32_t MaxEntrySize = 1u << 30;

			static uint16_t Le16(const std::string& b, size_t o)
			{
				return static_cast<uint16_t>(static_cast<unsigned char>(b[o]) | (static_cast<unsigned char>(b[o + 1]) << 8));
			}

			static uint32_t Le32(const std::string& b, size_t o)
			{
				return static_cast<uint32_t>(Le16(b, o)) | (static_cast<uint32_t>(Le16(b, o + 2)) << 16);
			}

			bool ReadAt(uint64_t offset, size_t length, std::string& out)
			{
				out.assign(length, '\0');
				stream.clear();
				stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
				if (length > 0)
					stream.read(out.data(), static_cast<std::streamsize>(length));
				return static_cast<bool>(stream);
			}

			static bool Inflate(const std::string& in, size_t outSize, std::string& out)
			{
				z_stream z{};
				if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
					return false;

				out.assign(outSize, '\0');
				z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
				z.avail_in = static_cast<uInt>(in.size());
				z.next_out = reinterpret_cast<Bytef*>(out.data());
				z.avail_out = static_cast<uInt>(outSize);

				const int status = inflate(&z, Z_FINISH);
				const bool complete = status == Z_STREAM_END && z.avail_out == 0;
				inflateEnd(&z);
				return complete;
			}

			std::string path;
			std::ifstream stream;
			uint64_t fileSize = 0;
			std::vector<EntryInfo> entries;
		};
	}
}
