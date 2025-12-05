#pragma once

#define XWF_ITEM_INFO_ORIG_ID 1
#define XWF_ITEM_INFO_ATTR 2
#define XWF_ITEM_INFO_FLAGS 3
#define XWF_ITEM_INFO_DELETION 4
#define XWF_ITEM_INFO_CLASSIFICATION 5
#define XWF_ITEM_INFO_LINKCOUNT 6
#define XWF_ITEM_INFO_COLORANALYSIS 7
#define XWF_ITEM_INFO_PIXELINDEX 8
#define XWF_ITEM_INFO_FILECOUNT 11
#define XWF_ITEM_INFO_EMBEDDEDOFFSET 16
#define XWF_ITEM_INFO_CREATIONTIME 32
#define XWF_ITEM_INFO_MODIFICATIONTIME 33
#define XWF_ITEM_INFO_LASTACCESSTIME 34
#define XWF_ITEM_INFO_ENTRYMODIFICATIONTIME 35
#define XWF_ITEM_INFO_DELETIONTIME 36
#define XWF_ITEM_INFO_INTERNALCREATIONTIME 37

#define XT_PREPARE_CALLPI 0x01
#define XT_PREPARE_CALLPILATE 0x02
#define XT_PREPARE_EXPECTMOREITEMS 0x04
#define XT_PREPARE_DONTOMIT 0x08
#define XT_PREPARE_TARGETDIRS 0x10
#define XT_PREPARE_TARGETZEROBYTEFILES 0x20

#define XT_INIT_NOT_THREAD_SAFE 1

#define XT_INIT_THREAD_SAFE 2

#define XWF_HASHTYPE_MD5	7
#define XWF_HASHTYPE_SHA1	8
#define XWF_HASHTYPE_SHA256 9

#define WINDOWS_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL

class DHFS_4_1_ReaderInterface {
public:
	virtual std::unique_ptr<BYTE[]> readSectors(uint64_t offset, uint64_t size) = 0;
};

class DHFS4_1_ItemReader : public DHFS_4_1_ReaderInterface 
{
private:
	HANDLE hItem;

public:
	std::unique_ptr<BYTE[]> readSectors(uint64_t offset, uint64_t size);

	void setHandle(HANDLE hItem) 
	{
		this->hItem = hItem;
	}

	HANDLE getHandle()
	{
		return this->hItem;
	}
};

class DHFS4_1_DiskIOReader : public DHFS_4_1_ReaderInterface 
{
private:
	LONG nDrive;

public:
	std::unique_ptr<BYTE[]> readSectors(uint64_t offset, uint64_t size);

	void setNDrive(LONG nDrive)
	{
		this->nDrive = nDrive;
	}

	LONG getNDrive()
	{
		return this->nDrive;
	}
};

enum class DHF4_1_DescriptorStatus {
	free = 0,
	used = 1,
	unused = 2,
	dirty = 3,
	carved = 4,
	fragCarved = 5
};


struct DHFS4_1_Bootsector {
	uint32_t beginTime;
	uint32_t endTime;
	uint32_t sectorSize;
	uint32_t clusterSize;
	uint32_t descriptorTableOffset;
	uint32_t descriptorTableItemcount;
	uint32_t dataAreaOffset;
	uint32_t logsOffset;
};

struct DHFS4_1_VideoFragment {
	uint64_t id;
	uint32_t prevFragmentId;
	uint32_t nextFragmentId;
	uint16_t fragmentId;
	uint32_t beginDate;
	uint32_t endDate;
	uint32_t fragmentSize;
	uint32_t mainDescriptorId;
	
	DHF4_1_DescriptorStatus status;
	uint32_t offset;

	// For carved fragments
	uint32_t dueBytes;
	uint64_t offsetInStream;
};

struct DHFS4_1_Videoframe {
	uint32_t mainDescriptorId;
	uint32_t videoOffset;
	uint16_t camera;
	uint32_t beginDate;
	uint32_t length;
	uint32_t bytesDue;
	DHF4_1_DescriptorStatus status;
};

struct DHFS4_1_Descriptor {
	uint64_t id;
	uint8_t camera;
	uint16_t fragmentCount;
	uint32_t lastFragmentSize;
	uint32_t beginDate;
	uint32_t endDate;
	std::vector<DHFS4_1_VideoFragment> videoFragments;
	DHF4_1_DescriptorStatus status;
};

struct DHFS4_1_Partition {
	uint32_t id;
	uint32_t bootSectorOffset;
	uint64_t partitionOffset;
	uint32_t length;
	std::set<uint16_t> cameras;
	DHFS4_1_Bootsector bootsector;
	std::unordered_map<uint32_t, DHFS4_1_Descriptor> mainDescriptors;
	std::vector<DHFS4_1_Descriptor> carvedDescriptors;
	std::vector<uint32_t> allocatedDescriptors;
	std::vector<uint32_t> freeDescriptors;
	std::vector<std::pair<uint64_t, uint64_t>> lastFragmentDescriptors;
	uint64_t rootId;
	uint64_t carvedRootId;
};

struct DHFS4_1_Time {
	uint32_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
};

void readPartitionTable(DHFS_4_1_ReaderInterface& reader, std::vector<DHFS4_1_Partition>& partitionTable);

void readBootSector(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition);

BOOL readDescriptorTable(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition, uint64_t descriptorId, DHFS4_1_Descriptor& descriptor);

DWORD createVSItems(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition, DHFS4_1_Descriptor descriptor);

DWORD createVSCarvedItems(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition, DHFS4_1_Descriptor descriptor, uint64_t index);

DWORD createVSLogfile(DHFS4_1_Partition partition);

void carveFreeDescriptor(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition);

void carveSlackSpace(DHFS_4_1_ReaderInterface& reader, DHFS4_1_Partition& partition);

BOOL validateDHFSTime(uint32_t dhfsTimestamp);

INT64 dhfstimeToFiletime(uint32_t dhfsTimestamp);

std::wstring dhfstimeToWString(uint32_t dhfsTimestamp);

uint64_t getVideoOffset(uint64_t partitionOffset, uint64_t dataAreaOffset, uint64_t clusterSize, uint64_t descriptorId);
