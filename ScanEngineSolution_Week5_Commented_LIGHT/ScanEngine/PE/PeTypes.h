#pragma once

#include "Api/EngineApi.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ScanEngineInternal
{
    enum class PeParseStatus
    {
        Success,
        NotPe,
        MalformedPe,
        StructCorrupt,
        IoError
    };

    struct PeDataDirectory
    {
        std::uint32_t virtualAddress{};
        std::uint32_t size{};
    };

    struct PeSection
    {
        std::string name;
        std::uint32_t virtualSize{};
        std::uint32_t virtualAddress{};
        std::uint32_t sizeOfRawData{};
        std::uint32_t pointerToRawData{};
        std::uint32_t characteristics{};
        double entropy{};
    };

    struct PeImage
    {
        bool isPe{};
        bool isPe32Plus{};
        std::uint16_t machine{};
        std::uint16_t subsystem{};
        std::uint16_t characteristics{};
        std::uint16_t sectionCount{};
        std::uint32_t timeDateStamp{};
        std::uint32_t entryPointRva{};
        std::uint64_t imageBase{};
        std::uint32_t sectionAlignment{};
        std::uint32_t fileAlignment{};
        std::uint32_t sizeOfHeaders{};
        std::uint32_t numberOfRvaAndSizes{};
        bool isDll{};
        bool isDriver{};
        bool isManaged{};
        bool isSigned{};
        bool hasDebug{};
        bool hasRichHeader{};
        std::uint64_t overlaySize{};
        bool directoryAnomaly{};
        std::array<PeDataDirectory, 16> directories{};
        std::vector<PeSection> sections;
        std::vector<std::uint8_t> bytes;
    };

    struct PeAnalysis
    {
        bool timestampAnomaly{};
        bool entryPointOutsideSections{};
        bool alignmentAnomaly{};
        bool directoryAnomaly{};
        bool strangeSectionName{};
        bool wxSection{};
        bool executableHighEntropy{};
        bool overlayAnomaly{};
        bool largeOverlay{};
        bool sectionCountAnomaly{};

        bool riskyProcessThread{};
        bool riskyPersistence{};
        bool riskyNetwork{};
        bool riskyCrypto{};
        bool tlsCallbacks{};
        bool delayImportRisk{};
        bool exportAnomaly{};

        bool hasVersionInfo{};
        bool hasCompanyName{};
        bool hasIcon{};
        bool hasManifest{};
        bool resourcePayloadAnomaly{};

        EngineSignatureStatus signatureStatus{EngineSignatureStatus::Unsigned};
        std::uint32_t score{};
        std::uint32_t matchedRules{};
    };
}
