#pragma once

#include "PE/PeReader.h"
#include "PE/PeTypes.h"

namespace ScanEngineInternal
{
    class PeRuleEvaluator
    {
    public:
        static void Evaluate(
            const PeReader& reader,
            PeAnalysis& analysis,
            bool unsignedAddsOnePoint = false);
        static void FillPeInfo(
            const PeReader& reader,
            const PeAnalysis& analysis,
            EnginePeInfoV1& info);
    };
}
