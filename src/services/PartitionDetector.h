#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>

struct PartitionInfo {
    UINT32      diskNumber;
    UINT32      partitionNumber;
    UINT64      sizeBytes;
    UINT64      offsetBytes;
    std::string driveLetter;
    std::string fileSystemLabel;
    std::string gptType;
    bool        isActive;
};

class PartitionDetector {
private:
    IWbemServices *pSvc;
    IWbemLocator  *pLoc;
    bool           comInitialized;

    bool InitializeWMI();
    void CleanupWMI();

public:
    PartitionDetector();
    ~PartitionDetector();

    std::vector<PartitionInfo> findPartitionsByLabels(const std::vector<std::string> &labels);
    std::vector<PartitionInfo> findPartitionsBySize(UINT64 minSizeBytes, UINT64 maxSizeBytes);
    std::vector<PartitionInfo> getAllPartitions();
    PartitionInfo              getPartitionInfo(UINT32 diskNumber, UINT32 partitionNumber);

private:
    std::string getVolumeLabelByPartitionObjectId(const std::string &partitionObjectId);
};