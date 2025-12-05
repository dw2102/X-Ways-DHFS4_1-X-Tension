#include "pch.h"
#include "dhfs4_1.h"

static uint64_t currentPosition = 0;

// Global variables for the I/O Disk case, because the DLL stays active until the disk is closed
std::vector<DHFS4_1_Partition> partitionTable;
DHFS4_1_DiskIOReader reader;
uint64_t currentNItemID = -1;
BOOL moreFragments = false;
uint64_t moreFragmentsOffset = 0;

#pragma pack(2)
struct DriveInfo {
	DWORD nSize;
	LONG nDrive;
	LONG nParentDrive;
	DWORD nBytesPerSector;
	INT64 nSectorCount;
	INT64 nParentSectorCount;
	INT64 nStartSectorOnParent;
	LPVOID lpPrivate;
};

std::unique_ptr<BYTE[]> DHFS4_1_ItemReader::readSectors(uint64_t offset,uint64_t size)
{
	std::unique_ptr<BYTE[]> buffer(new BYTE[size * 512]);
	ZeroMemory(buffer.get(), size);
	XWF_Read(this->hItem, offset, buffer.get(), size * 512);
	currentPosition += size * 512;
	return buffer;
}

std::unique_ptr<BYTE[]> DHFS4_1_DiskIOReader::readSectors(uint64_t offset, uint64_t size)
{
	std::unique_ptr<BYTE[]> buffer(new BYTE[size * 512]);
	ZeroMemory(buffer.get(), size);
	XWF_SectorIO(this->nDrive, offset / 512, size, buffer.get(), 0);
	currentPosition += size * 512;
	return buffer;
}

LONG __stdcall XT_Init(CallerInfo info, DWORD nFlags, HANDLE hMainWnd, struct LicenseInfo* pLicInfo)
{
	XT_RetrieveFunctionPointers();

	currentPosition = 0;

	if (nFlags == XT_INIT_QUICKCHECK || nFlags == XT_INIT_ABOUTONLY) {

		return XT_INIT_THREAD_SAFE;

	}
	return XT_INIT_THREAD_SAFE;
}

LONG __stdcall XT_Done(void* lpReserved)
{
	XWF_OutputMessage(L"DHFS4.1 X-Tension done.", 0);
	return 0;
}

LONG __stdcall XT_About(HANDLE hParentWnd, void* lpReserved)
{
	XWF_OutputMessage(L"X-Ways X-Tension to extract video data from DHFS.41 filesystem. NO WARRANTY. SOFTWARE IS PROVIDED \' AS IS\'", 0);
	XWF_OutputMessage(L"Usage: Use this X-Tension to build the file tree for a DHFS 4.1 drive. Later open the same drive in Disk I/O mode with this X-Tension to access all files.", 0);
	XWF_OutputMessage(L"Author: Dane Wullen", 0);
	return 0;
}

LONG __stdcall XT_Prepare(HANDLE hVolume, HANDLE hEvidence, DWORD nOpType, void* lpReserved)
{
	return XT_PREPARE_CALLPI;
}

LONG __stdcall XT_Finalize(HANDLE hVolume, HANDLE hEvidence, DWORD nOpType, void* lpReserved)
{
	XWF_OutputMessage(L"Finished.", 0);
	return 0;
}

DWORD XT_SectorIOInit(struct DriveInfo* pDInfo)
{
	XWF_OutputMessage(L"Starting DHFS4.1 Disk I/O X-Tension", 0);

	std::unique_ptr<BYTE[]> buffer(new BYTE[512]);
	ZeroMemory(buffer.get(), 512);

	uint32_t dhfsId;

	XWF_SectorIO(pDInfo->nDrive, 0, 1, buffer.get(), 0x0);

	memcpy(&dhfsId, buffer.get(), 4);

	if (dhfsId == 0x53464844)
	{
		currentPosition = 0;

		reader.setNDrive(pDInfo->nDrive);

		readPartitionTable(reader, partitionTable);
		for (DHFS4_1_Partition& partition : partitionTable)
		{
			readBootSector(reader, partition);

			std::wstring progressDescription = std::format(L"Read descriptor table of partition {}", partition.id);

			XWF_ShowProgress((wchar_t*)progressDescription.c_str(), (0x04 | 0x08));
			XWF_SetProgressPercentage(0);

			for (int i = 0; i < partition.bootsector.descriptorTableItemcount; i++)
			{
				XWF_ShouldStop();
				DHFS4_1_Descriptor descriptor;
				// Only needed to get the free descriptors;
				readDescriptorTable(reader, partition, i, descriptor);
				XWF_SetProgressPercentage(DWORD((100. / partition.bootsector.descriptorTableItemcount) * i));
			}
			XWF_HideProgress();

			carveFreeDescriptor(reader, partition);
			carveSlackSpace(reader, partition);
		}
		return 0x11;
	}
	return 0;
}

DWORD XT_SectorIO(LPVOID lpPrivate, LONG nDrive, INT64 nSector, DWORD nCount, LPVOID lpBuffer, DWORD nFlags)
{
	return XWF_SectorIO(nDrive, nSector, nCount, lpBuffer, &nFlags);
}

DWORD XT_SectorIODone(LPVOID lpPrivate, LPVOID lpReserved)
{
	return 0;
}

uint64_t getVideoOffset(uint64_t partitionOffset, uint64_t dataAreaOffset, uint64_t clusterSize, uint64_t descriptorId)
{
	uint32_t videoOffset = 0;
	uint64_t oldPosition = currentPosition;

	currentPosition = (partitionOffset + dataAreaOffset + clusterSize * descriptorId) * 512ULL;
	std::unique_ptr<BYTE[]> buffer = reader.readSectors(currentPosition, 1);
	uint64_t internalOffset = 64;
	memcpy(&videoOffset, buffer.get() + internalOffset, 4);

	currentPosition = oldPosition;

	return videoOffset;
}

INT64 XT_FileIO(LPVOID lpPrivate, LONG nDrive, HANDLE hVolume, HANDLE hItem, LONG nItemID, INT64 nOffset, LPVOID lpBuffer, INT64 nNumberOfBytes, DWORD nFlags)
{
	XWF_ShouldStop();
	LPWSTR lpMetaData = XWF_GetExtractedMetadata(nItemID);

	if (lpMetaData == nullptr)
	{
		// Skip all data without added meta data
		return -1;
	}

	std::wstring metaData(lpMetaData);
	uint8_t pos = metaData.find(L":");
	
	
	if (nOffset >= 0 && pos != std::wstring::npos)
	{
		std::vector<std::wstring> parts;

		size_t start = 0, end;
		while ((end = metaData.find(L":", start)) != std::wstring::npos)
		{
			parts.push_back(metaData.substr(start, end - start));
			start = end + 1;
		}
		parts.push_back(metaData.substr(start));

		uint32_t metaDataPartition = static_cast<uint32_t>(std::stoul(parts[0]));

		DHFS4_1_Partition partition = partitionTable[metaDataPartition];
		uint64_t clusterSize = partition.bootsector.clusterSize;
		uint64_t dataAreaOffset = partition.bootsector.dataAreaOffset;
		uint64_t partitionOffset = partition.partitionOffset;

		std::unique_ptr<BYTE[]> byteBuffer(new BYTE[nNumberOfBytes]);
		uint64_t bufferOffset = 0;
		uint64_t fragmentID = 0;
		uint64_t fragmentOffset = 0;
		uint64_t maxRead = nNumberOfBytes;

		if (currentNItemID != nItemID)
		{
			currentNItemID = nItemID;
			moreFragmentsOffset = 0;
		}

		if (parts[1] == L"Logfile")
		{
			uint32_t logFileSize = 0;

			currentPosition = (partition.partitionOffset + partition.bootsector.logsOffset) * 512ULL;

			std::unique_ptr<BYTE[]> buffer = reader.readSectors(currentPosition, 1);
			memcpy(&logFileSize, buffer.get(), 4);

			while (maxRead > 0)
			{
				uint64_t offset = nOffset + bufferOffset + moreFragmentsOffset;
				uint64_t sectorSize = (logFileSize + 511) / 512;
				currentPosition = (partition.partitionOffset + partition.bootsector.logsOffset + 2) * 512ULL;
				std::unique_ptr<BYTE[]> logBuffer = reader.readSectors((currentPosition), sectorSize);
				memcpy(byteBuffer.get() + bufferOffset, logBuffer.get() + offset, maxRead);
				bufferOffset += maxRead;
				maxRead -= maxRead;
			}
		}
		else if (parts[1] == L"Carved")
		{
			uint32_t index = static_cast<uint32_t>(std::stoul(parts[2]));

			DHFS4_1_Descriptor descriptor = partition.carvedDescriptors[index];

			struct BinarySearchElement {
				uint32_t length;
				uint64_t startInStream;
			};

			while (maxRead > 0)
			{
				XWF_ShouldStop();
				uint64_t offset = nOffset + bufferOffset + moreFragmentsOffset;

				uint64_t slidingWindow = 0;
				uint64_t fragmentIndex = 0;

				size_t low = 0, high = descriptor.videoFragments.size();

				// More efficient method of traversing the fragment list, instead O(n) its O(log n)...
				while (low < high)
				{
					size_t mid = (low + high) / 2;
					const auto& element = descriptor.videoFragments[mid];
					if (offset < element.offsetInStream)
					{
						high = mid;
					}
					else if (offset >= element.offsetInStream + element.fragmentSize)
					{
						low = mid + 1;
					}
					else
					{
						fragmentIndex = mid;
						// If the index is 0, the start is 0. So this prevents a division by 0
						fragmentOffset = mid > 0 ? offset % element.offsetInStream : offset % element.fragmentSize;
						break;
					}
				}
				currentPosition = (partitionOffset + dataAreaOffset + descriptor.videoFragments[fragmentIndex].mainDescriptorId * clusterSize) * 512;

				std::unique_ptr<BYTE[]> videoBuffer = reader.readSectors((currentPosition), clusterSize);
				memcpy(byteBuffer.get() + bufferOffset, videoBuffer.get() + descriptor.videoFragments[fragmentIndex].offset + fragmentOffset, min(descriptor.videoFragments[fragmentIndex].fragmentSize - fragmentOffset, maxRead));
				bufferOffset += min(descriptor.videoFragments[fragmentIndex].fragmentSize - fragmentOffset, maxRead);
				maxRead -= min(descriptor.videoFragments[fragmentIndex].fragmentSize - fragmentOffset, maxRead);
			}
		}
		else
		{
			uint32_t descriptorId = static_cast<uint32_t>(std::stoul(parts[1]));

			DHFS4_1_Descriptor descriptor;

			readDescriptorTable(reader, partition, descriptorId, descriptor);

			std::vector<DHFS4_1_VideoFragment> videoFragments = descriptor.videoFragments;
			uint32_t videoOffset = 0;

			if (descriptor.status != DHF4_1_DescriptorStatus::carved)
			{
				videoOffset = getVideoOffset(partitionOffset, dataAreaOffset, clusterSize, descriptorId);
			}

			while (maxRead > 0)
			{
				XWF_ShouldStop();
				uint64_t offset = nOffset + videoOffset + bufferOffset + moreFragmentsOffset;

				uint64_t fragmentID = offset / (clusterSize * 512);
				uint64_t fragmentOffset = offset % (clusterSize * 512);

				currentPosition = (partitionOffset + dataAreaOffset + clusterSize * videoFragments[fragmentID].id) * 512;

				if (fragmentID == videoFragments.size() - 1)
				{
					clusterSize = descriptor.lastFragmentSize;
				}

				std::unique_ptr<BYTE[]> videoBuffer = reader.readSectors((currentPosition), clusterSize);
				memcpy(byteBuffer.get() + bufferOffset, videoBuffer.get() + fragmentOffset, min((clusterSize * 512 - fragmentOffset), maxRead));
				bufferOffset += min((clusterSize * 512 - fragmentOffset), maxRead);
				maxRead -= min((clusterSize * 512 - fragmentOffset), maxRead);
			}
		}

		memcpy(lpBuffer, byteBuffer.get(), min(nNumberOfBytes, bufferOffset));

		moreFragmentsOffset += min(nNumberOfBytes, bufferOffset);

		// End of file reached
		// No more following fragments
		if (moreFragmentsOffset >= XWF_GetItemSize(nItemID))
		{
			moreFragmentsOffset = 0;
		}

		// No more following fragments
		// 8388608 = 8MB, the maximum chunk size X-Ways can read per call of XT_FileIO
		// If nNumberOfBytes is less then 8 MB no more fragments are following
		if (nNumberOfBytes < 8388608)
		{
			moreFragmentsOffset = 0;
		}

		// If returned exactly 8 MB and the file size is bigger then
		// the bytes still open to read, more fragments will follow
		return min(bufferOffset, nNumberOfBytes);
	}
}

LONG XT_ProcessItemEx(LONG nItemID, HANDLE hItem, PVOID lpReserved)
{
	XWF_OutputMessage(L"Starting DHFS4.1 Filetree X-Tension", 0);
	XWF_ShouldStop();

	std::vector<DHFS4_1_Partition> partitionTable;
	DHFS4_1_ItemReader reader;	
	reader.setHandle(hItem);

	uint32_t dhfsId;

	currentPosition = 0;
	std::unique_ptr<BYTE[]> buffer = reader.readSectors(currentPosition, 1);

	memcpy(&dhfsId, buffer.get(), 4);

	if (dhfsId == 0x53464844)
	{
		currentPosition = 0;

		readPartitionTable(reader, partitionTable);

		for (DHFS4_1_Partition& partition : partitionTable)
		{
			XWF_ShouldStop();

			readBootSector(reader, partition);

			std::wstring progressDescription = std::format(L"Read descriptortable of partition {}", partition.id);

			XWF_ShowProgress((wchar_t*)progressDescription.c_str(), (0x04 | 0x08));
			XWF_SetProgressPercentage(0);

			std::wstring folderName = std::format(L"Partition {}", partition.id);
			int rootId = XWF_CreateItem(const_cast<LPWSTR>(folderName.c_str()), 0x00000001);
			XWF_SetItemInformation(rootId, XWF_ITEM_INFO_FLAGS, 0x00000001);
			XWF_SetItemParent(rootId, 0);
			partition.rootId = rootId;
			int fileCounter = 0;

			for (int i = 0; i < partition.bootsector.descriptorTableItemcount; i++)
			{
				XWF_ShouldStop();

				DHFS4_1_Descriptor descriptor;
				BOOL success = readDescriptorTable(reader, partition, i, descriptor);

				if (success)
				{
					createVSItems(reader, partition, descriptor);
					fileCounter++;
				}

				XWF_SetProgressPercentage(DWORD((100. / partition.bootsector.descriptorTableItemcount) * i));
			}

			XWF_HideProgress();

			carveFreeDescriptor(reader, partition);
			carveSlackSpace(reader, partition);

			std::wstring carvedFolderName = L"Carved";
			int carvedRootId = XWF_CreateItem(const_cast<LPWSTR>(carvedFolderName.c_str()), 0x00000001);
			XWF_SetItemInformation(carvedRootId, XWF_ITEM_INFO_FLAGS, 0x00000001);
			XWF_SetItemParent(carvedRootId, partition.rootId);
			partition.carvedRootId = carvedRootId;
			int carvedFileCounter = 0;

			for (int i = 0; i < partition.carvedDescriptors.size(); i++)
			{
				createVSCarvedItems(reader, partition, partition.carvedDescriptors[i], i);
				carvedFileCounter++;
			}

			uint32_t logFileSize = 0;
			const std::wstring logType = std::wstring(L"txt\0");

			currentPosition = (partition.partitionOffset + partition.bootsector.logsOffset) * 512ULL;

			std::unique_ptr<BYTE[]> logBuffer = reader.readSectors(currentPosition, 1);
			memcpy(&logFileSize, logBuffer.get(), 4);

			std::wstring logFileName = std::format(L"Part_{}_Logfile.txt", partition.id);
			int logFiledId = XWF_CreateItem(const_cast<LPWSTR>(logFileName.c_str()), 0x00000001);

			if (logFiledId != -1)
			{
				XWF_SetItemInformation(logFiledId, XWF_ITEM_INFO_CREATIONTIME, dhfstimeToFiletime(partition.bootsector.beginTime));
				XWF_SetItemInformation(logFiledId, XWF_ITEM_INFO_MODIFICATIONTIME, dhfstimeToFiletime(partition.bootsector.endTime));
				XWF_SetItemSize(logFiledId, logFileSize);

				XWF_SetItemType(logFiledId, const_cast <LPWSTR>(logType.c_str()), 3);
				std::wstring metaData = std::to_wstring(partition.id) + L":" + L"Logfile";
				XWF_AddExtractedMetadata(logFiledId, &metaData[0], 0x01);
				XWF_SetItemParent(logFiledId, partition.rootId);
				XWF_SetItemOfs(logFiledId, (-1) * ((partition.partitionOffset + partition.bootsector.logsOffset + 2) * 512ULL), ((partition.partitionOffset + partition.bootsector.logsOffset + 2)));
			}

			XWF_SetItemInformation(carvedRootId, XWF_ITEM_INFO_FILECOUNT, carvedFileCounter);
			XWF_SetItemInformation(rootId, XWF_ITEM_INFO_FILECOUNT, fileCounter + 1);
			XWF_SetItemInformation(0, XWF_ITEM_INFO_FLAGS, 0x00000002);
		}
	}
	else
	{
		XWF_OutputMessage(L"Wrong Dahua signature!", 0);
	}

	return 0;
}

DWORD createVSItems(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition, DHFS4_1_Descriptor descriptor)
{
	const std::wstring itemType = std::wstring(L"dav\0");

	uint32_t descriptorId = descriptor.id;
	uint32_t videoOffset = 0;

	if (descriptor.status != DHF4_1_DescriptorStatus::carved)
	{
		// Calculating "real" offset by parsing the header of the DHII structure of the .DAV videofiles
		// Skip 64 bytes and read the next 4 byte to get the offset of the first videoframe
		// Without the calculation the video is still playable but not controlable by the timeline of the videoplayer
		uint64_t oldPosition = currentPosition;

		currentPosition = (partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) * 512ULL;
		std::unique_ptr<BYTE[]> buffer = reader.readSectors(currentPosition, 1);
		uint64_t internalOffset = 64;
		memcpy(&videoOffset, buffer.get() + internalOffset, 4);
		currentPosition = oldPosition;
	}
	
	std::vector<DHFS4_1_VideoFragment> videoFragments = descriptor.videoFragments;

	uint64_t totalFragmentSize = 0;
	for (const DHFS4_1_VideoFragment& videoFragment : videoFragments)
	{
		totalFragmentSize += videoFragment.fragmentSize;
	}

	uint64_t fileSize = (totalFragmentSize) * 512ULL - videoOffset;

	std::wstring fileName = std::format(L"Ch_{}_{}-{}.dav", descriptor.camera, dhfstimeToWString(descriptor.beginDate), dhfstimeToWString(descriptor.endDate));
	int childId = XWF_CreateItem(const_cast<LPWSTR>(fileName.c_str()), 0x00000001);
	XWF_SetItemInformation(childId, XWF_ITEM_INFO_CREATIONTIME, dhfstimeToFiletime(descriptor.beginDate));
	XWF_SetItemInformation(childId, XWF_ITEM_INFO_MODIFICATIONTIME, dhfstimeToFiletime(descriptor.endDate));
	XWF_SetItemSize(childId, fileSize);
	XWF_SetItemType(childId, const_cast <LPWSTR>(itemType.c_str()), 3);
	std::wstring metaData = std::to_wstring(partition.id) + L":" + std::to_wstring(descriptorId);
	XWF_AddExtractedMetadata(childId, &metaData[0], 0x01);
	XWF_SetItemParent(childId, partition.rootId);

	XWF_SetItemOfs(childId, (-1) * ((partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) * 512ULL + videoOffset), ((partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) + videoOffset / 512));

	return 0;
}

DWORD createVSCarvedItems(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition, DHFS4_1_Descriptor descriptor, uint64_t index)
{
	const std::wstring itemType = std::wstring(L"dav\0");

	uint32_t descriptorId = descriptor.id;

	std::vector<DHFS4_1_VideoFragment> videoFragments = descriptor.videoFragments;

	uint64_t totalFragmentSize = 0;

	if (videoFragments.size() == 0)
	{
		return 0;
	}

	for (const DHFS4_1_VideoFragment& videoFragment : videoFragments)
	{
		totalFragmentSize += videoFragment.fragmentSize;
	}

	uint64_t fileSize = totalFragmentSize;

	std::wstring fileName = std::format(L"Ch_{}_{}-{}.dav", descriptor.camera, dhfstimeToWString(descriptor.beginDate), dhfstimeToWString(descriptor.endDate));
	int childId = XWF_CreateItem(const_cast<LPWSTR>(fileName.c_str()), 0x00000001);
	XWF_SetItemInformation(childId, XWF_ITEM_INFO_CREATIONTIME, dhfstimeToFiletime(descriptor.beginDate));
	XWF_SetItemInformation(childId, XWF_ITEM_INFO_MODIFICATIONTIME, dhfstimeToFiletime(descriptor.endDate));
	XWF_SetItemSize(childId, fileSize);
	XWF_SetItemType(childId, const_cast <LPWSTR>(itemType.c_str()), 3);
	std::wstring metaData = std::to_wstring(partition.id) + L":" + L"Carved:" + std::to_wstring(index);
	XWF_AddExtractedMetadata(childId, &metaData[0], 0x01);
	XWF_SetItemParent(childId, partition.carvedRootId);

	XWF_SetItemOfs(childId, (-1) * ((partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) * 512ULL + videoFragments[0].offset), ((partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) + videoFragments[0].offset / 512));

	return 0;
}

DWORD createVSLogfile(DHFS4_1_Partition partition)
{
	uint32_t logFileSize = 0;
	const std::wstring logType = std::wstring(L"txt\0");

	currentPosition = (partition.partitionOffset + partition.bootsector.logsOffset) * 512ULL;

	std::unique_ptr<BYTE[]> logBuffer = reader.readSectors(currentPosition, 1);
	memcpy(&logFileSize, logBuffer.get(), 4);

	std::wstring logFileName = std::format(L"Part_{}_Logfile.txt", partition.id);
	int logFiledId = XWF_CreateItem(const_cast<LPWSTR>(logFileName.c_str()), 0x00000001);

	if (logFiledId != -1)
	{
		XWF_SetItemInformation(logFiledId, XWF_ITEM_INFO_CREATIONTIME, dhfstimeToFiletime(partition.bootsector.beginTime));
		XWF_SetItemInformation(logFiledId, XWF_ITEM_INFO_MODIFICATIONTIME, dhfstimeToFiletime(partition.bootsector.endTime));
		XWF_SetItemSize(logFiledId, logFileSize);

		XWF_SetItemType(logFiledId, const_cast <LPWSTR>(logType.c_str()), 3);
		std::wstring metaData = std::to_wstring(partition.id) + L":" + L"Logfile";
		XWF_AddExtractedMetadata(logFiledId, &metaData[0], 0x01);
		XWF_SetItemParent(logFiledId, partition.rootId);
		XWF_SetItemOfs(logFiledId, (-1) * ((partition.partitionOffset + partition.bootsector.logsOffset + 2) * 512ULL), ((partition.partitionOffset + partition.bootsector.logsOffset + 2)));
		return 0;
	}
	return -1;
}

void readPartitionTable(DHFS_4_1_ReaderInterface& reader, std::vector<DHFS4_1_Partition>& partitionTable)
{
	XWF_ShouldStop();

	// Skip 30 sectors
	currentPosition = 30 * 512ULL;
	uint64_t internalOffset = 0;

	std::unique_ptr<BYTE[]> buffer = reader.readSectors(currentPosition, 1);
	
	internalOffset += 64;

	uint32_t endSignatur = 0;
	int i = 0;

	for (int i = 0; endSignatur != 0x55AA55AA; i++)
	{
		DHFS4_1_Partition partition;

		memcpy(&partition.id, &i, 8);

		internalOffset += 8;

		memcpy(&partition.bootSectorOffset, buffer.get() + internalOffset, 4);
		internalOffset += 4;

		internalOffset += 24;

		memcpy(&partition.partitionOffset, buffer.get() + internalOffset, 8);
		internalOffset += 8;

		memcpy(&partition.length, buffer.get() + internalOffset, 4);
		internalOffset += 4;

		internalOffset += 4;

		memcpy(&endSignatur, buffer.get() + internalOffset, 4);
		internalOffset += 4;

		internalOffset += 8;

		partitionTable.push_back(partition);
	}
}

void readBootSector(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition)
{
	DHFS4_1_Bootsector bootSector;
	uint64_t internalOffset = 0;

	XWF_ShouldStop();

	currentPosition = partition.bootSectorOffset * 512ULL + partition.partitionOffset * 512ULL;
	std::unique_ptr<BYTE[]> buffer = reader.readSectors(currentPosition, 1);

	internalOffset += 16;

	memcpy(&bootSector.beginTime, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&bootSector.endTime, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	internalOffset += 20;

	memcpy(&bootSector.sectorSize, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&bootSector.clusterSize, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	internalOffset += 16;

	memcpy(&bootSector.descriptorTableOffset, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&bootSector.dataAreaOffset, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&bootSector.descriptorTableItemcount, buffer.get() + internalOffset, 4);
	internalOffset += 4;

	internalOffset += 168;

	memcpy(&bootSector.logsOffset, buffer.get() + internalOffset, 4);

	partition.bootsector = bootSector;
}

BOOL readDescriptorTable(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition, uint64_t descriptorId, DHFS4_1_Descriptor& descriptor)
{
	XWF_ShouldStop();

	std::vector<DHFS4_1_VideoFragment> videoFragments;

	uint64_t sector = (descriptorId * 32ULL) / 512;

	currentPosition = partition.partitionOffset * 512ULL + partition.bootsector.descriptorTableOffset * 512ULL + sector * 512ULL;

	uint64_t internalOffset = (descriptorId * 32) % 512ULL;

	std::unique_ptr<BYTE[]> sectorBuffer = reader.readSectors((currentPosition), 1);

	uint8_t id;
	uint8_t camera;
	uint16_t fragmentCount;
	uint32_t begin;
	uint32_t end;
	uint32_t nextDescriptorId;
	uint16_t lastFragmentSize;
	uint32_t prevDescriptorId;
	uint32_t mainDescriptorId; // Main-Descriptor position in descriptor table

	XWF_ShouldStop();

	memcpy(&id, sectorBuffer.get() + internalOffset, 1);
	internalOffset += 1;

	memcpy(&camera, sectorBuffer.get() + internalOffset, 1);
	internalOffset += 1;

	memcpy(&fragmentCount, sectorBuffer.get() + internalOffset, 2);
	internalOffset += 2;

	memcpy(&begin, sectorBuffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&end, sectorBuffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&nextDescriptorId, sectorBuffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&lastFragmentSize, sectorBuffer.get() + internalOffset, 2);
	internalOffset += 2;

	// Skip 2 unknown bytes
	internalOffset += 2;

	memcpy(&prevDescriptorId, sectorBuffer.get() + internalOffset, 4);
	internalOffset += 4;

	memcpy(&mainDescriptorId, sectorBuffer.get() + internalOffset, 4);
	internalOffset += 4;

	internalOffset += 4;

	if (id == 0xFE)
	{
		partition.freeDescriptors.push_back(descriptorId);
	}
	else if (id == 0x02)
	{
		partition.allocatedDescriptors.push_back(descriptorId);
	}
	else if (id == 0x01)
	{
		if (begin < end)
		{
			partition.allocatedDescriptors.push_back(descriptorId);
			partition.cameras.insert(camera);

			descriptor.beginDate = begin;
			descriptor.endDate = end;
			descriptor.fragmentCount = fragmentCount;
			descriptor.lastFragmentSize = lastFragmentSize;
			descriptor.id = descriptorId;
			descriptor.camera = (camera & 0x0F) + 1;
			descriptor.status = DHF4_1_DescriptorStatus::used;

			DHFS4_1_VideoFragment videoFragment;
			videoFragment.beginDate = begin;
			videoFragment.endDate = end;
			videoFragment.fragmentId = fragmentCount; // When id == 0x02 fragmentCount is the fragment id
			videoFragment.id = descriptorId;
			videoFragment.fragmentSize = partition.bootsector.clusterSize;
			videoFragment.nextFragmentId = nextDescriptorId;
			videoFragment.prevFragmentId = 0;
			videoFragment.mainDescriptorId = descriptorId;
			videoFragments.push_back(videoFragment);

			uint32_t nextFragmentId = videoFragment.nextFragmentId;

			while (nextFragmentId != 0)
			{
				uint8_t id;
				uint8_t camera;
				uint16_t fragmentCount;
				uint32_t begin;
				uint32_t end;
				uint32_t nextDescriptorId;
				uint32_t lastFragmentSize;
				uint32_t prevDescriptorId;
				uint32_t mainDescriptorId;

				uint64_t sector = (nextFragmentId * 32ULL) / 512;

				currentPosition = partition.partitionOffset * 512ULL + partition.bootsector.descriptorTableOffset * 512ULL + sector * 512ULL;

				uint64_t internalOffset = (nextFragmentId * 32) % 512ULL;

				std::unique_ptr<BYTE[]> sectorBuffer = reader.readSectors(currentPosition, 1);

				memcpy(&id, sectorBuffer.get() + internalOffset, 1);
				internalOffset += 1;

				memcpy(&camera, sectorBuffer.get() + internalOffset, 1);
				internalOffset += 1;

				memcpy(&fragmentCount, sectorBuffer.get() + internalOffset, 2);
				internalOffset += 2;

				memcpy(&begin, sectorBuffer.get() + internalOffset, 4);
				internalOffset += 4;

				memcpy(&end, sectorBuffer.get() + internalOffset, 4);
				internalOffset += 4;

				memcpy(&nextDescriptorId, sectorBuffer.get() + internalOffset, 4);
				internalOffset += 4;

				memcpy(&lastFragmentSize, sectorBuffer.get() + internalOffset, 4);
				internalOffset += 4;

				memcpy(&prevDescriptorId, sectorBuffer.get() + internalOffset, 4);
				internalOffset += 4;

				memcpy(&mainDescriptorId, sectorBuffer.get() + internalOffset, 4);
				internalOffset += 4;

				internalOffset += 4;

				DHFS4_1_VideoFragment videoFragment;
				videoFragment.beginDate = begin;
				videoFragment.endDate = end;
				videoFragment.fragmentId = fragmentCount; // When id == 0x02 fragmentCount is the fragment id
				videoFragment.id = nextFragmentId;
				videoFragment.nextFragmentId = nextDescriptorId;
				videoFragment.prevFragmentId = prevDescriptorId;
				videoFragment.mainDescriptorId = mainDescriptorId;

				if (videoFragment.nextFragmentId == 0)
				{
					videoFragment.fragmentSize = descriptor.lastFragmentSize;

					std::pair<uint64_t, uint64_t> lastFragment;
					partition.lastFragmentDescriptors.push_back({ nextFragmentId, descriptor.lastFragmentSize });

				}
				else
				{
					videoFragment.fragmentSize = partition.bootsector.clusterSize;
				}

				videoFragments.push_back(videoFragment);

				nextFragmentId = videoFragment.nextFragmentId;
			}

			descriptor.videoFragments = videoFragments;
			return true;
		}
	}
	return false;
}

void carveFreeDescriptor(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition)
{
	XWF_ShouldStop();

	std::wstring progressDescription = std::format(L"Carve descriptor table of partition {}", partition.id);

	BYTE dhavSignaturBegin[4] = { 0x44, 0x48, 0x41, 0x56 };
	BYTE dhavSignaturEnd[4] = { 0x64, 0x68, 0x61, 0x76 };
	int i = 0;

	uint32_t bitmask = ~((1U << 12) - 1);

	std::unordered_map<uint8_t, std::unordered_map<uint32_t, std::vector<DHFS4_1_Videoframe>>> carvedFramgentsMap; // [camera][hourly timestamp][list of fragments]
	std::vector<DHFS4_1_Videoframe> carvedVideoFrames;
	
	XWF_ShowProgress((wchar_t*)progressDescription.c_str(), (0x04 | 0x08));
	XWF_SetProgressPercentage(0);

	for (uint32_t& descriptorId : partition.freeDescriptors)
	{
		XWF_ShouldStop();
		currentPosition = (partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) * 512ULL;
		std::unique_ptr<BYTE[]> descriptorBuffer = reader.readSectors(currentPosition, partition.bootsector.clusterSize);

		for (uint64_t j = 0; j <= (4096 * 512) - 4;)
		{
			if (std::memcmp(descriptorBuffer.get() + j, dhavSignaturBegin, 4) == 0)
			{
				uint16_t camera = 0;
				uint32_t length = 0;
				uint32_t dhfsTimestamp = 0;

				memcpy(&camera, descriptorBuffer.get() + j + 6, 2);
				memcpy(&length, descriptorBuffer.get() + j + 12, 4);
				memcpy(&dhfsTimestamp, descriptorBuffer.get() + j + 16, 4);

				// probably no real DHAV frame, so skip this
				if (length == 0 || dhfsTimestamp == 0)
				{
					j++;
					continue;
				}

				if (!validateDHFSTime(dhfsTimestamp))
				{
					j++;
					continue;
				}

				DHFS4_1_Videoframe carvedVideoFrame;
				carvedVideoFrame.beginDate = dhfsTimestamp;
				carvedVideoFrame.length = length;
				carvedVideoFrame.mainDescriptorId = descriptorId;
				carvedVideoFrame.status = DHF4_1_DescriptorStatus::carved;
				carvedVideoFrame.camera = camera;
				carvedVideoFrame.bytesDue = 0;
				carvedVideoFrame.videoOffset = j;

				// Videoframe is within cluster, so no internal fragmentation
				if (length + j < 4096 * 512)
				{
					
					uint32_t length = 0;
					uint32_t endSig = 0;

					memcpy(&length, descriptorBuffer.get() + j + carvedVideoFrame.length - 4, 4);

					// Matching footer in fragment
					if (length == carvedVideoFrame.length && std::memcmp(descriptorBuffer.get() + j + carvedVideoFrame.length - 8, dhavSignaturEnd, 4) == 0)
					{
						j = j + length;
					}
					// No matching footer, so probably no real frame
					else
					{
						j++;
						continue;
					}

					// If the matching footer isn't necessary comment above code out
					// j = j + length
				}
				else
				{
					carvedVideoFrame.status = DHF4_1_DescriptorStatus::fragCarved;
					carvedVideoFrame.bytesDue = (-1) * ((4096 * 512 - j) - (length));
					carvedVideoFrames.push_back(carvedVideoFrame);
					break;
				}
				carvedVideoFrames.push_back(carvedVideoFrame);
			}
			else if (std::memcmp(descriptorBuffer.get() + j, dhavSignaturEnd, 4) == 0)
			{
				uint32_t length = 0;
				memcpy(&length, descriptorBuffer.get() + j + 4, 4);

				// again, probably no real dhav footer
				if (length == 0)
				{
					j++;
					continue;
				}

				// 4 bytes dhav, 4 bytes length
				j += 8;

				for (int i = carvedVideoFrames.size()-1; i >= 0; i--)
				{
					// lookout for a DHAV head which
					// 1. got the same length as the footer
					// 2. is fragmented due to the cluster size
					// 3. the pending bytes are the same as the offset to the dhav ending
					if (carvedVideoFrames[i].length == length &&
						carvedVideoFrames[i].status == DHF4_1_DescriptorStatus::fragCarved &&
						carvedVideoFrames[i].bytesDue == j)
					{

						DHFS4_1_Videoframe carvedVideoTail;
						carvedVideoTail.beginDate = carvedVideoFrames[i].beginDate;
						carvedVideoTail.length = j;
						carvedVideoTail.mainDescriptorId = descriptorId;
						carvedVideoTail.status = DHF4_1_DescriptorStatus::carved;
						carvedVideoTail.camera = (carvedVideoFrames[i].camera & 0x0F) + 1;
						carvedVideoTail.bytesDue = 0;
						carvedVideoTail.videoOffset = 0;

						carvedVideoFrames[i].status = DHF4_1_DescriptorStatus::carved;

						carvedVideoFrames.push_back(carvedVideoTail);
					}
				}				
			}
			else
			{
				j++;
			}
		}
		i++;
		XWF_SetProgressPercentage(DWORD((100. / partition.freeDescriptors.size()) * i));
	}
		
	// Sort all carved fragments and map them to the cameras and duration
	for (DHFS4_1_Videoframe & carvedVideoFrame : carvedVideoFrames)
	{
		carvedFramgentsMap[carvedVideoFrame.camera][(carvedVideoFrame.beginDate & bitmask)].push_back(carvedVideoFrame);
	}

	// Create new descriptors which can be used for the VS items
	std::vector<DHFS4_1_Descriptor> videoDescriptors;
	int index = 0;
	for (auto& channelMap : carvedFramgentsMap)
	{
		for (auto& timeMap : channelMap.second)
		{
			// the duration is not exactly 1 hour, but now the fragments can assign to a time range...
			DHFS4_1_Descriptor descriptor;
			descriptor.beginDate = timeMap.first;
			descriptor.endDate = (timeMap.first + 4096) & bitmask;
			descriptor.camera = channelMap.first;
			descriptor.id = index;

			uint64_t offsetInStream = 0;

			for (int i = 0; i < timeMap.second.size(); i++)
			{
				DHFS4_1_VideoFragment videoFragment;
				videoFragment.beginDate = timeMap.second[i].beginDate;
				videoFragment.endDate = (timeMap.second[i].beginDate + 4096) & bitmask;
				videoFragment.offset = timeMap.second[i].videoOffset;
				videoFragment.mainDescriptorId = timeMap.second[i].mainDescriptorId;
				videoFragment.id = timeMap.second[i].mainDescriptorId;
				videoFragment.endDate = (timeMap.second[i].beginDate + 4096) & bitmask;
				videoFragment.nextFragmentId = i < timeMap.second.size() - 1 ? timeMap.second[i + 1].mainDescriptorId : 0;
				videoFragment.prevFragmentId = i > 0 ? timeMap.second[i - 1].mainDescriptorId : 0;
				videoFragment.fragmentSize = timeMap.second[i].length - timeMap.second[i].bytesDue; // to get the size in this fragment, not the total size
				videoFragment.offsetInStream = offsetInStream;

				offsetInStream += videoFragment.fragmentSize;

				descriptor.videoFragments.push_back(videoFragment);
			}

			partition.carvedDescriptors.push_back(descriptor);
			index++;
		}
	}

	XWF_HideProgress();
}

void carveSlackSpace(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition)
{
	XWF_ShouldStop();

	std::wstring progressDescription = std::format(L"Carve slack space of partition {}", partition.id);

	BYTE dhavSignaturBegin[4] = { 0x44, 0x48, 0x41, 0x56 };
	BYTE dhavSignaturEnd[4] = { 0x64, 0x68, 0x61, 0x76 };
	int i = 0;

	uint32_t bitmask = ~((1U << 12) - 1);

	std::unordered_map<uint8_t, std::unordered_map<uint32_t, std::vector<DHFS4_1_Videoframe>>> carvedFramgentsMap; // [camera][hourly timestamp][list of fragments]
	std::vector<DHFS4_1_Videoframe> carvedVideoFrames;

	XWF_ShowProgress((wchar_t*)progressDescription.c_str(), (0x04 | 0x08));
	XWF_SetProgressPercentage(0);
	i = 0;

	for (const auto& lastFragment : partition.lastFragmentDescriptors)
	{
		XWF_ShouldStop();
		uint64_t descriptorId = lastFragment.first;
		uint64_t size = lastFragment.second;

		currentPosition = (partition.partitionOffset + partition.bootsector.dataAreaOffset + partition.bootsector.clusterSize * descriptorId) * 512ULL;
		std::unique_ptr<BYTE[]> descriptorBuffer = reader.readSectors(currentPosition, partition.bootsector.clusterSize);

		for (uint64_t j = (size * 512); j <= (4096 * 512) - 4;)
		{
			XWF_ShouldStop();
			if (std::memcmp(descriptorBuffer.get() + j, dhavSignaturBegin, 4) == 0)
			{
				uint16_t camera = 0;
				uint32_t length = 0;
				uint32_t dhfsTimestamp = 0;

				memcpy(&camera, descriptorBuffer.get() + j + 6, 2);
				memcpy(&length, descriptorBuffer.get() + j + 12, 4);
				memcpy(&dhfsTimestamp, descriptorBuffer.get() + j + 16, 4);

				// probably no real DHAV frame, so skip this
				if (length == 0 || dhfsTimestamp == 0)
				{
					j++;
					continue;
				}

				if (!validateDHFSTime(dhfsTimestamp))
				{
					j++;
					continue;
				}

				DHFS4_1_Videoframe carvedVideoFrame;
				carvedVideoFrame.beginDate = dhfsTimestamp;
				carvedVideoFrame.length = length;
				carvedVideoFrame.mainDescriptorId = descriptorId;
				carvedVideoFrame.status = DHF4_1_DescriptorStatus::carved;
				carvedVideoFrame.camera = camera;
				carvedVideoFrame.bytesDue = 0;
				carvedVideoFrame.videoOffset = j;

				// Videoframe is within cluster, so no internal fragmentation
				if (length + j < (4096 * 512))
				{
					uint32_t length = 0;
					uint32_t endSig = 0;

					memcpy(&length, descriptorBuffer.get() + j + carvedVideoFrame.length - 4, 4);

					// Matching footer in fragment
					if (length == carvedVideoFrame.length && std::memcmp(descriptorBuffer.get() + j + carvedVideoFrame.length - 8, dhavSignaturEnd, 4) == 0)
					{
						j = j + length;
					}
					// No matching footer, so probably no real frame
					else
					{
						j++;
						continue;
					}

					// If the matching footer isn't necessary comment above code out
					// j = j + length
				}
				else
				{
					carvedVideoFrame.status = DHF4_1_DescriptorStatus::fragCarved;
					carvedVideoFrame.bytesDue = (-1) * ((4096 * 512 - j) - (length));
					carvedVideoFrames.push_back(carvedVideoFrame);
					break;
				}
				carvedVideoFrames.push_back(carvedVideoFrame);
			}
			else if (std::memcmp(descriptorBuffer.get() + j, dhavSignaturEnd, 4) == 0)
			{
				uint32_t length = 0;
				memcpy(&length, descriptorBuffer.get() + j + 4, 4);

				// again, probably no real dhav footer
				if (length == 0)
				{
					j++;
					continue;
				}

				// 4 bytes dhav, 4 bytes length
				j += 8;

				for (int i = carvedVideoFrames.size() - 1; i >= 0; i--)
				{
					// lookout for a DHAV head which
					// 1. got the same length as the footer
					// 2. is fragmented due to the cluster size
					// 3. the pending bytes are the same as the offset to the dhav ending
					if (carvedVideoFrames[i].length == length &&
						carvedVideoFrames[i].status == DHF4_1_DescriptorStatus::fragCarved &&
						carvedVideoFrames[i].bytesDue == j)
					{

						DHFS4_1_Videoframe carvedVideoTail;
						carvedVideoTail.beginDate = carvedVideoFrames[i].beginDate;
						carvedVideoTail.length = j;
						carvedVideoTail.mainDescriptorId = descriptorId;
						carvedVideoTail.status = DHF4_1_DescriptorStatus::carved;
						carvedVideoTail.camera = (carvedVideoFrames[i].camera & 0x0F) + 1;
						carvedVideoTail.bytesDue = 0;
						carvedVideoTail.videoOffset = 0;

						carvedVideoFrames[i].status = DHF4_1_DescriptorStatus::carved;
						carvedVideoFrames.push_back(carvedVideoTail);
					}
				}
			}
			else
			{
				j++;
			}
		}
		i++;
		XWF_SetProgressPercentage(DWORD((100. / partition.lastFragmentDescriptors.size()) * i));
	}

	// Sort all carved fragments and map them to the cameras and duration
	for (DHFS4_1_Videoframe& carvedVideoFrame : carvedVideoFrames)
	{
		carvedFramgentsMap[carvedVideoFrame.camera][(carvedVideoFrame.beginDate & bitmask)].push_back(carvedVideoFrame);
	}

	// Create new descriptors which can be used for the VS items
	std::vector<DHFS4_1_Descriptor> videoDescriptors;
	int index = 0;
	for (auto& channelMap : carvedFramgentsMap)
	{
		for (auto& timeMap : channelMap.second)
		{
			// the duration is not exactly 1 hour, but now the fragments can assign to a time range...
			DHFS4_1_Descriptor descriptor;
			descriptor.beginDate = timeMap.first;
			descriptor.endDate = (timeMap.first + 4096) & bitmask;
			descriptor.camera = channelMap.first;
			descriptor.id = index;

			uint64_t offsetInStream = 0;

			for (int i = 0; i < timeMap.second.size(); i++)
			{
				DHFS4_1_VideoFragment videoFragment;
				videoFragment.beginDate = timeMap.second[i].beginDate;
				videoFragment.endDate = (timeMap.second[i].beginDate + 4096) & bitmask;
				videoFragment.offset = timeMap.second[i].videoOffset;
				videoFragment.mainDescriptorId = timeMap.second[i].mainDescriptorId;
				videoFragment.id = timeMap.second[i].mainDescriptorId;
				videoFragment.endDate = (timeMap.second[i].beginDate + 4096) & bitmask;
				videoFragment.nextFragmentId = i < timeMap.second.size() - 1 ? timeMap.second[i + 1].mainDescriptorId : 0;
				videoFragment.prevFragmentId = i > 0 ? timeMap.second[i - 1].mainDescriptorId : 0;
				videoFragment.fragmentSize = timeMap.second[i].length - timeMap.second[i].bytesDue; // to get the size in this fragment, not the total size
				videoFragment.offsetInStream = offsetInStream;

				offsetInStream += videoFragment.fragmentSize;

				descriptor.videoFragments.push_back(videoFragment);
			}

			partition.carvedDescriptors.push_back(descriptor);
			index++;
		}
	}

	XWF_HideProgress();
}


DHFS4_1_Time convertDfhstime(uint32_t dhfsTimestamp)
{
	DHFS4_1_Time dhfsTime;
	dhfsTime.year = (dhfsTimestamp >> (31 + 1 - 6)) & ((1u << 6) - 1); // 6 bits year
	dhfsTime.month = (dhfsTimestamp >> (25 + 1 - 4)) & ((1u << 4) - 1); // 4 bits month
	dhfsTime.day = (dhfsTimestamp >> (21 + 1 - 5)) & ((1u << 5) - 1); // 5 bits day
	dhfsTime.hour = (dhfsTimestamp >> (16 + 1 - 5)) & ((1u << 5) - 1); // 5 bits hour
	dhfsTime.minute = (dhfsTimestamp >> (11 + 1 - 6)) & ((1u << 6) - 1); // 6 bits minute
	dhfsTime.second = (dhfsTimestamp >> (5 + 1 - 6)) & ((1u << 6) - 1); // 6 bits seconds

	return dhfsTime;
}

INT64 dhfstimeToFiletime(uint32_t dhfsTimestamp)
{
	DHFS4_1_Time dhfsTime = convertDfhstime(dhfsTimestamp);

	std::tm t = {};
	t.tm_year = dhfsTime.year - 1900 + 2000;
	t.tm_mon = dhfsTime.month - 1;
	t.tm_mday = dhfsTime.day;
	t.tm_hour = dhfsTime.hour;
	t.tm_min = dhfsTime.minute;
	t.tm_sec = dhfsTime.second;

	uint64_t unixtime = static_cast<INT64>(timegm(&t));
	
	const uint64_t EPOCH_DIFFERENCE = 116444736000000000ULL;
	uint64_t fileTimeValue = unixtime * 10000000ULL + EPOCH_DIFFERENCE;

	FILETIME ft;
	ULARGE_INTEGER ull;
	ft.dwLowDateTime = static_cast<DWORD>(fileTimeValue & 0xFFFFFFFF);
	ft.dwHighDateTime = static_cast<DWORD>(fileTimeValue >> 32);

	ull.LowPart = ft.dwLowDateTime;
	ull.HighPart = ft.dwHighDateTime;

	INT64 ret = static_cast<INT64>(ull.QuadPart);

	return ret;
}

BOOL validateDHFSTime(uint32_t dhfsTimestamp)
{
	DHFS4_1_Time dhfsTime = convertDfhstime(dhfsTimestamp);

	// filter dates that make no sense
	if (dhfsTime.year < 1 ||
		dhfsTime.month < 1 ||
		dhfsTime.month > 12 ||
		dhfsTime.day < 1 ||
		dhfsTime.day > 31 ||
		dhfsTime.hour < 0 ||
		dhfsTime.hour > 23 ||
		dhfsTime.minute < 0 ||
		dhfsTime.minute > 59 ||
		dhfsTime.second < 0 ||
		dhfsTime.second > 59)
	{
		return false;
	}


	std::tm t = {};
	t.tm_year = dhfsTime.year - 1900 + 2000;
	t.tm_mon = dhfsTime.month - 1;
	t.tm_mday = dhfsTime.day;
	t.tm_hour = dhfsTime.hour;
	t.tm_min = dhfsTime.minute;
	t.tm_sec = dhfsTime.second;

	std::time_t time = std::mktime(&t);
	if (time == -1)
	{
		return false;
	}
	
	std::tm norm = {};
	if (localtime_s(&norm, &time) != 0)
	{
		return false;
	}

	return (norm.tm_year == t.tm_year &&
		norm.tm_mon == t.tm_mon &&
		norm.tm_mday == t.tm_mday &&
		norm.tm_hour == t.tm_hour &&
		norm.tm_min == t.tm_min &&
		norm.tm_sec == t.tm_sec);
}

std::wstring dhfstimeToWString(uint32_t dhfsTimestamp)
{
	DHFS4_1_Time dhfsTime = convertDfhstime(dhfsTimestamp);

	return std::format(L"{:02d}.{:02d}.{}_{:02d}:{:02d}:{:02d}", dhfsTime.day, dhfsTime.month, (dhfsTime.year + 2000), dhfsTime.hour, dhfsTime.minute, dhfsTime.second);
}
