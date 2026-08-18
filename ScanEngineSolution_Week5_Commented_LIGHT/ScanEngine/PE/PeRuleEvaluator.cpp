 #include "PE/PeRuleEvaluator.h"

namespace ScanEngineInternal
{
    // Tong hop cac feature do PeAnalyzer thu duoc thanh risk score + bitmask matchedRules.
    void PeRuleEvaluator::Evaluate(
        const PeReader& reader,
        PeAnalysis& a,
        bool unsignedAddsOnePoint)
    {
        std::uint32_t score = 0;
        std::uint32_t rules = ENGINE_RULE_NONE;
        const auto& image = reader.Image();

        // A) Header & metadata.
        if (a.timestampAnomaly) { score += 1; rules |= ENGINE_RULE_PE_TIMESTAMP; }
        if (a.entryPointOutsideSections) { score += 2; rules |= ENGINE_RULE_PE_ENTRYPOINT; }
        if (a.alignmentAnomaly) { score += 1; rules |= ENGINE_RULE_PE_ALIGNMENT; }
        if (a.directoryAnomaly) { score += 2; rules |= ENGINE_RULE_PE_DIRECTORY; }

        // B) Sections.
        if (a.strangeSectionName) { score += 1; rules |= ENGINE_RULE_PE_SECTION_NAME; }
        if (a.wxSection) { score += 2; rules |= ENGINE_RULE_PE_WX_SECTION; }
        if (a.executableHighEntropy) { score += 1; rules |= ENGINE_RULE_PE_EXEC_ENTROPY; }
        if (a.overlayAnomaly)
        {
            score += a.largeOverlay ? 2u : 1u;
            rules |= ENGINE_RULE_PE_OVERLAY;
        }
        if (a.sectionCountAnomaly) { score += 1; rules |= ENGINE_RULE_PE_SECTION_COUNT; }

        // C) Imports / exports / TLS / delay-load. Each risky API group is capped at +1.
        if (a.riskyProcessThread) { score += 1; rules |= ENGINE_RULE_PE_RISK_PROCESS; }
        if (a.riskyPersistence) { score += 1; rules |= ENGINE_RULE_PE_RISK_PERSISTENCE; }
        if (a.riskyNetwork) { score += 1; rules |= ENGINE_RULE_PE_RISK_NETWORK; }
        if (a.riskyCrypto) { score += 1; rules |= ENGINE_RULE_PE_RISK_CRYPTO; }
        if (a.tlsCallbacks) { score += 2; rules |= ENGINE_RULE_PE_TLS_CALLBACK; }
        if (a.delayImportRisk) { score += 1; rules |= ENGINE_RULE_PE_DELAY_RISK; }
        if (a.exportAnomaly) { score += 1; rules |= ENGINE_RULE_PE_EXPORT_ANOMALY; }

        // D) Resources/version info. Demo heuristic for normal desktop EXE.
        const bool normalExe = !image.isDll && !image.isDriver &&
            (image.subsystem == 2 || image.subsystem == 3); // GUI/CUI
        if (normalExe && (!a.hasVersionInfo || !a.hasCompanyName))
        {
            score += 1;
            rules |= ENGINE_RULE_PE_VERSION_INFO;
        }
        if (a.resourcePayloadAnomaly)
        {
            score += 1;
            rules |= ENGINE_RULE_PE_RESOURCE;
        }

        // E) Authenticode.
        if (a.signatureStatus == EngineSignatureStatus::SignedInvalid)
        {
            score += 2;
            rules |= ENGINE_RULE_PE_SIGNATURE_INVALID;
        }
        else if (a.signatureStatus == EngineSignatureStatus::Unsigned && unsignedAddsOnePoint)
        {
            score += 1;
            rules |= ENGINE_RULE_PE_UNSIGNED;
        }

        a.score = score;
        a.matchedRules = rules;
    }

    // Copy ket qua phan tich PE noi bo sang struct API EnginePeInfoV1 de tra ra ngoai DLL.
    void PeRuleEvaluator::FillPeInfo(
        const PeReader& reader,
        const PeAnalysis& analysis,
        EnginePeInfoV1& info)
    {
        const auto& image = reader.Image();
        info = {};
        info.structSize = sizeof(info);
        info.apiVersion = ENGINE_API_VERSION_1;
        info.isPe = image.isPe ? TRUE : FALSE;
        info.isPe32Plus = image.isPe32Plus ? TRUE : FALSE;
        info.machine = image.machine;
        info.subsystem = image.subsystem;
        info.isDll = image.isDll ? TRUE : FALSE;
        info.isDriver = image.isDriver ? TRUE : FALSE;
        info.isManaged = image.isManaged ? TRUE : FALSE;
        info.isSigned = image.isSigned ? TRUE : FALSE;
        info.signatureStatus = static_cast<std::uint32_t>(analysis.signatureStatus);
        info.hasDebug = image.hasDebug ? TRUE : FALSE;
        info.hasRichHeader = image.hasRichHeader ? TRUE : FALSE;
        info.entryPointRva = image.entryPointRva;
        info.imageBase = image.imageBase;
        info.sectionCount = image.sectionCount;
        info.overlaySize = image.overlaySize;
        info.riskScore = analysis.score;
        info.matchedRules = analysis.matchedRules;
    }
}
