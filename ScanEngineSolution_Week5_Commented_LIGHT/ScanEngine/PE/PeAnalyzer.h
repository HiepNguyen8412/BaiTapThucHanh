#pragma once

#include "PE/PeReader.h"
#include "PE/PeTypes.h"

#include <string>

namespace ScanEngineInternal
{
    class PeAnalyzer
    {
    public:
        static void Analyze(
            const std::wstring& path,
            const PeReader& reader,
            double executableEntropyThreshold,
            PeAnalysis& analysis);

    private:
        static void AnalyzeBasic(const PeReader& reader, double threshold, PeAnalysis& analysis);
        static void AnalyzeImports(const PeReader& reader, PeAnalysis& analysis);
        static void AnalyzeDelayImports(const PeReader& reader, PeAnalysis& analysis);
        static void AnalyzeTls(const PeReader& reader, PeAnalysis& analysis);
        static void AnalyzeExports(const PeReader& reader, PeAnalysis& analysis);
        static void AnalyzeResources(const PeReader& reader, PeAnalysis& analysis);
    };
}
