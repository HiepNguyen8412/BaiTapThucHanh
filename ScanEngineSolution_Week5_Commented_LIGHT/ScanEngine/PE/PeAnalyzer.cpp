#include "PE/PeAnalyzer.h"
#include "PE/Authenticode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <set>
#include <vector>

namespace
{
    constexpr std::size_t DIR_EXPORT = 0;
    constexpr std::size_t DIR_IMPORT = 1;
    constexpr std::size_t DIR_RESOURCE = 2;
    constexpr std::size_t DIR_TLS = 9;
    constexpr std::size_t DIR_DELAY_IMPORT = 13;
    constexpr std::uint32_t MEM_EXECUTE = 0x20000000u;
    constexpr std::uint32_t MEM_WRITE = 0x80000000u;

    template <typename T>
    bool ReadPod(const ScanEngineInternal::PeImage& image, std::size_t offset, T& value)
    {
        if (offset > image.bytes.size() || sizeof(T) > image.bytes.size() - offset) return false;
        std::memcpy(&value, image.bytes.data() + offset, sizeof(T));
        return true;
    }

    std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool IsPowerOfTwo(std::uint32_t value)
    {
        return value != 0 && (value & (value - 1)) == 0;
    }

    bool StrangeName(const std::string& name)
    {
        if (name.empty()) return true;
        for (unsigned char ch : name)
        {
            if (ch < 0x20 || ch > 0x7E) return true;
        }
        return false;
    }

    bool StartsWith(const std::string& value, const char* prefix)
    {
        const std::string p = Lower(prefix);
        const std::string v = Lower(value);
        return v.rfind(p, 0) == 0;
    }

    // Danh dau cac API import nhay cam (process/memory/network/crypto...) de rule evaluator cham diem.
    void MarkRiskyImport(const std::string& rawName, ScanEngineInternal::PeAnalysis& analysis)
    {
        const std::string name = Lower(rawName);
        static const std::array<const char*, 6> processNames{
            "createremotethread", "createremotethreadex", "openprocess",
            "writeprocessmemory", "virtualallocex", "queueuserapc"};
        for (const char* item : processNames)
        {
            if (name == item) { analysis.riskyProcessThread = true; break; }
        }
        if (StartsWith(name, "regsetvalue") || StartsWith(name, "createservice"))
            analysis.riskyPersistence = true;
        if (StartsWith(name, "winhttp") || StartsWith(name, "wsa") || StartsWith(name, "internet"))
            analysis.riskyNetwork = true;
        if (StartsWith(name, "crypt") || StartsWith(name, "bcrypt"))
            analysis.riskyCrypto = true;
    }

    void ParseThunkTable(
        const ScanEngineInternal::PeReader& reader,
        std::uint32_t thunkRva,
        ScanEngineInternal::PeAnalysis& analysis,
        bool* sawRisk = nullptr)
    {
        const auto& image = reader.Image();
        const std::size_t thunkSize = image.isPe32Plus ? 8 : 4;
        const std::uint64_t ordinalMask = image.isPe32Plus ? 0x8000000000000000ull : 0x80000000ull;
        for (std::size_t index = 0; index < 4096; ++index)
        {
            const std::uint64_t entryRva64 = static_cast<std::uint64_t>(thunkRva) + index * thunkSize;
            if (entryRva64 > UINT32_MAX) break;
            std::size_t offset = 0;
            if (!reader.RvaToFileOffset(static_cast<std::uint32_t>(entryRva64), thunkSize, offset)) break;
            std::uint64_t thunk = 0;
            if (image.isPe32Plus)
            {
                if (!ReadPod(image, offset, thunk)) break;
            }
            else
            {
                std::uint32_t thunk32 = 0;
                if (!ReadPod(image, offset, thunk32)) break;
                thunk = thunk32;
            }
            if (thunk == 0) break;
            if ((thunk & ordinalMask) != 0) continue;

            std::string functionName;
            const std::uint64_t nameRva64 = thunk & 0x7FFFFFFFFFFFFFFFull;
            if (nameRva64 > UINT32_MAX - 2u) continue;
            const auto nameRva = static_cast<std::uint32_t>(nameRva64);
            std::size_t nameOffset = 0;
            if (!reader.RvaToFileOffset(nameRva, 3, nameOffset)) continue;
            // IMAGE_IMPORT_BY_NAME starts with a 2-byte hint.
            const std::uint32_t stringRva = nameRva + 2;
            if (!reader.ReadCStringRva(stringRva, functionName, 512)) continue;
            const bool before = analysis.riskyProcessThread || analysis.riskyPersistence ||
                analysis.riskyNetwork || analysis.riskyCrypto;
            MarkRiskyImport(functionName, analysis);
            const bool after = analysis.riskyProcessThread || analysis.riskyPersistence ||
                analysis.riskyNetwork || analysis.riskyCrypto;
            if (sawRisk != nullptr && !before && after) *sawRisk = true;
            if (sawRisk != nullptr && after) *sawRisk = true;
        }
    }

    // Tinh entropy rieng cho mot vung byte, thuong dung de phat hien section bi pack/nen.
    double ByteEntropy(const std::uint8_t* data, std::size_t size)
    {
        if (size == 0) return 0.0;
        std::array<std::uint64_t, 256> counts{};
        for (std::size_t i = 0; i < size; ++i) ++counts[data[i]];
        double result = 0.0;
        const double total = static_cast<double>(size);
        for (auto count : counts)
        {
            if (count == 0) continue;
            const double p = static_cast<double>(count) / total;
            result -= p * std::log2(p);
        }
        return result;
    }

    bool ContainsUtf16Ascii(const std::uint8_t* data, std::size_t size, const char* text)
    {
        std::vector<std::uint8_t> pattern;
        for (const char* p = text; *p != '\0'; ++p)
        {
            pattern.push_back(static_cast<std::uint8_t>(*p));
            pattern.push_back(0);
        }
        return pattern.size() <= size &&
            std::search(data, data + size, pattern.begin(), pattern.end()) != data + size;
    }
}

namespace ScanEngineInternal
{
    // Phan tich dac diem co ban: section entropy/name/permission, overlay va cac dau hieu bat thuong.
    void PeAnalyzer::AnalyzeBasic(const PeReader& reader, double threshold, PeAnalysis& analysis)
    {
        const auto& image = reader.Image();
        const std::time_t now = std::time(nullptr);
        const std::time_t oldest = 946684800; // 2000-01-01, demo threshold.
        analysis.timestampAnomaly = image.timeDateStamp == 0 ||
            static_cast<std::time_t>(image.timeDateStamp) < oldest ||
            static_cast<std::time_t>(image.timeDateStamp) > now + 24 * 60 * 60;

        bool entryInSection = image.entryPointRva == 0;
        std::set<std::string> names;
        for (const auto& section : image.sections)
        {
            const std::uint64_t begin = section.virtualAddress;
            const std::uint64_t span = (std::max)(
                static_cast<std::uint64_t>(section.virtualSize),
                static_cast<std::uint64_t>(section.sizeOfRawData));
            const std::uint64_t end = begin + span;
            if (image.entryPointRva >= begin && image.entryPointRva < end) entryInSection = true;

            const std::string lowered = Lower(section.name);
            if (StrangeName(section.name) || !names.insert(lowered).second) analysis.strangeSectionName = true;
            const bool executable = (section.characteristics & MEM_EXECUTE) != 0;
            const bool writable = (section.characteristics & MEM_WRITE) != 0;
            if (executable && writable) analysis.wxSection = true;
            if (executable && section.entropy > threshold) analysis.executableHighEntropy = true;
        }
        analysis.entryPointOutsideSections = !entryInSection;

        analysis.alignmentAnomaly =
            !IsPowerOfTwo(image.fileAlignment) ||
            !IsPowerOfTwo(image.sectionAlignment) ||
            image.fileAlignment < 512 || image.fileAlignment > 65536 ||
            image.sectionAlignment < image.fileAlignment;
        analysis.directoryAnomaly = image.directoryAnomaly;
        analysis.overlayAnomaly = image.overlaySize > 64ull * 1024ull;
        analysis.largeOverlay = image.overlaySize > 1024ull * 1024ull;
        analysis.sectionCountAnomaly = image.sectionCount > 12;
    }

    // Duyet Import Directory de dem DLL/API va tim risky imports.
    void PeAnalyzer::AnalyzeImports(const PeReader& reader, PeAnalysis& analysis)
    {
        const auto& image = reader.Image();
        const auto& directory = image.directories[DIR_IMPORT];
        if (directory.virtualAddress == 0 || directory.size == 0) return;

        for (std::size_t index = 0; index < 2048; ++index)
        {
            const std::uint64_t descriptorRva64 = static_cast<std::uint64_t>(directory.virtualAddress) + index * 20u;
            if (descriptorRva64 > UINT32_MAX) break;
            std::size_t offset = 0;
            if (!reader.RvaToFileOffset(static_cast<std::uint32_t>(descriptorRva64), 20, offset)) break;

            std::uint32_t originalThunk = 0, nameRva = 0, firstThunk = 0;
            ReadPod(image, offset + 0, originalThunk);
            ReadPod(image, offset + 12, nameRva);
            ReadPod(image, offset + 16, firstThunk);
            if (originalThunk == 0 && nameRva == 0 && firstThunk == 0) break;
            const std::uint32_t thunk = originalThunk != 0 ? originalThunk : firstThunk;
            if (thunk != 0) ParseThunkTable(reader, thunk, analysis);
        }
    }

    // Duyet Delay Import Directory; day la import chi resolve khi can va de bi bo qua neu chi doc Import thuong.
    void PeAnalyzer::AnalyzeDelayImports(const PeReader& reader, PeAnalysis& analysis)
    {
        const auto& image = reader.Image();
        const auto& directory = image.directories[DIR_DELAY_IMPORT];
        if (directory.virtualAddress == 0 || directory.size == 0) return;

        for (std::size_t index = 0; index < 512; ++index)
        {
            const std::uint64_t descriptorRva64 = static_cast<std::uint64_t>(directory.virtualAddress) + index * 32u;
            if (descriptorRva64 > UINT32_MAX) break;
            std::size_t offset = 0;
            if (!reader.RvaToFileOffset(static_cast<std::uint32_t>(descriptorRva64), 32, offset)) break;

            std::uint32_t attrs = 0, nameValue = 0, intValue = 0;
            ReadPod(image, offset + 0, attrs);
            ReadPod(image, offset + 4, nameValue);
            ReadPod(image, offset + 16, intValue); // pINT
            if (attrs == 0 && nameValue == 0 && intValue == 0) break;

            auto ToRva = [&](std::uint32_t value) -> std::uint32_t
            {
                if ((attrs & 1u) != 0) return value; // dlattrRva
                if (value < image.imageBase) return 0;
                const std::uint64_t rva = static_cast<std::uint64_t>(value) - image.imageBase;
                return rva <= UINT32_MAX ? static_cast<std::uint32_t>(rva) : 0;
            };
            const std::uint32_t thunkRva = ToRva(intValue);
            if (thunkRva == 0) continue;
            bool sawRisk = false;
            ParseThunkTable(reader, thunkRva, analysis, &sawRisk);
            if (sawRisk) analysis.delayImportRisk = true;
        }
    }

    // Doc TLS Directory/callback. TLS callback co the chay truoc entry point nen la tin hieu can quan sat.
    void PeAnalyzer::AnalyzeTls(const PeReader& reader, PeAnalysis& analysis)
    {
        const auto& image = reader.Image();
        const auto& directory = image.directories[DIR_TLS];
        if (directory.virtualAddress == 0 || directory.size == 0) return;
        std::size_t offset = 0;
        const std::size_t tlsSize = image.isPe32Plus ? 40u : 24u;
        if (!reader.RvaToFileOffset(directory.virtualAddress, tlsSize, offset)) return;

        std::uint64_t callbacksVa = 0;
        if (image.isPe32Plus)
        {
            ReadPod(image, offset + 24, callbacksVa);
        }
        else
        {
            std::uint32_t callbacks32 = 0;
            ReadPod(image, offset + 12, callbacks32);
            callbacksVa = callbacks32;
        }
        if (callbacksVa == 0 || callbacksVa < image.imageBase) return;
        const std::uint64_t callbacksRva64 = callbacksVa - image.imageBase;
        if (callbacksRva64 > UINT32_MAX) return;
        std::size_t callbackOffset = 0;
        const std::size_t pointerSize = image.isPe32Plus ? 8u : 4u;
        if (!reader.RvaToFileOffset(static_cast<std::uint32_t>(callbacksRva64), pointerSize, callbackOffset)) return;
        std::uint64_t first = 0;
        if (image.isPe32Plus) ReadPod(image, callbackOffset, first);
        else
        {
            std::uint32_t first32 = 0;
            ReadPod(image, callbackOffset, first32);
            first = first32;
        }
        analysis.tlsCallbacks = first != 0;
    }

    // Doc Export Directory va cac dau hieu export forwarding/bat thuong.
    void PeAnalyzer::AnalyzeExports(const PeReader& reader, PeAnalysis& analysis)
    {
        const auto& image = reader.Image();
        if (!image.isDll) return;
        const auto& directory = image.directories[DIR_EXPORT];
        if (directory.virtualAddress == 0 || directory.size == 0) return;
        std::size_t offset = 0;
        if (!reader.RvaToFileOffset(directory.virtualAddress, 40, offset)) return;

        std::uint32_t numberOfNames = 0, namesRva = 0;
        ReadPod(image, offset + 24, numberOfNames);
        ReadPod(image, offset + 32, namesRva);
        if (numberOfNames > 512) analysis.exportAnomaly = true;
        const std::uint32_t checkCount = (std::min)(numberOfNames, 512u);
        for (std::uint32_t i = 0; i < checkCount; ++i)
        {
            const std::uint64_t entryRva64 = static_cast<std::uint64_t>(namesRva) + i * 4u;
            if (entryRva64 > UINT32_MAX) break;
            std::size_t namePtrOffset = 0;
            if (!reader.RvaToFileOffset(static_cast<std::uint32_t>(entryRva64), 4, namePtrOffset)) break;
            std::uint32_t nameRva = 0;
            ReadPod(image, namePtrOffset, nameRva);
            std::string name;
            if (!reader.ReadCStringRva(nameRva, name, 512) || StrangeName(name))
            {
                analysis.exportAnomaly = true;
                break;
            }
        }
    }

    // Duyet resource metadata de tim resource lon/bat thuong va cac dau hieu lien quan.
    void PeAnalyzer::AnalyzeResources(const PeReader& reader, PeAnalysis& analysis)
    {
        const auto& image = reader.Image();
        const auto& directory = image.directories[DIR_RESOURCE];
        if (directory.virtualAddress == 0 || directory.size == 0) return;
        std::size_t root = 0;
        if (!reader.RvaToFileOffset(directory.virtualAddress, 16, root)) return;

        std::uint16_t named = 0, ids = 0;
        ReadPod(image, root + 12, named);
        ReadPod(image, root + 14, ids);
        const std::uint32_t count = static_cast<std::uint32_t>(named) + ids;
        const std::uint32_t safeCount = (std::min)(count, 4096u);
        bool hasRcData = false;
        for (std::uint32_t i = 0; i < safeCount; ++i)
        {
            const std::size_t entry = root + 16u + static_cast<std::size_t>(i) * 8u;
            std::uint32_t nameOrId = 0;
            if (!ReadPod(image, entry, nameOrId)) break;
            if ((nameOrId & 0x80000000u) != 0) continue;
            const std::uint16_t id = static_cast<std::uint16_t>(nameOrId & 0xFFFFu);
            if (id == 3 || id == 14) analysis.hasIcon = true; // ICON/GROUP_ICON
            if (id == 10) hasRcData = true;
            if (id == 16) analysis.hasVersionInfo = true;
            if (id == 24) analysis.hasManifest = true;
        }

        // Resource directory bytes are bounded by the PE directory size. This is a demo
        // heuristic, not a full resource tree extraction.
        const std::size_t sampleSize = (std::min<std::size_t>)(directory.size, image.bytes.size() - root);
        if (sampleSize != 0)
        {
            analysis.hasCompanyName = ContainsUtf16Ascii(image.bytes.data() + root, sampleSize, "CompanyName");
            const double entropy = ByteEntropy(image.bytes.data() + root, sampleSize);
            if ((hasRcData && sampleSize > 512u * 1024u) || entropy > 7.4)
                analysis.resourcePayloadAnomaly = true;
        }
    }

    // Ham dieu phoi cac bo phan AnalyzeBasic/Imports/DelayImports/TLS/Exports/Resources/Signature.
    void PeAnalyzer::Analyze(
        const std::wstring& path,
        const PeReader& reader,
        double executableEntropyThreshold,
        PeAnalysis& analysis)
    {
        analysis = {};
        AnalyzeBasic(reader, executableEntropyThreshold, analysis);
        AnalyzeImports(reader, analysis);
        AnalyzeDelayImports(reader, analysis);
        AnalyzeTls(reader, analysis);
        AnalyzeExports(reader, analysis);
        AnalyzeResources(reader, analysis);
        analysis.signatureStatus = VerifyAuthenticode(path, reader.Image().isSigned);
    }
}
