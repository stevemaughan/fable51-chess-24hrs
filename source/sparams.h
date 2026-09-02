#pragma once
// UCI-settable search parameters (for A/B testing without rebuilds)
struct SParam { const char* name; int* ptr; };
extern int SP_LmrBase, SP_LmrDiv, SP_RfpMargin, SP_RfpImproving, SP_RazorMargin, SP_NmpBase, SP_NmpDiv, SP_NmpEvalDiv,
    SP_ProbcutMargin, SP_LmpBase, SP_FutBase, SP_FutMargin, SP_HistPrune, SP_SeeQuiet, SP_SeeCapt, SP_SingMargin,
    SP_HistDiv, SP_AspDelta, SP_RfpDepth, SP_LmpDepth, SP_FutDepth, SP_HistBonusQ, SP_HistBonusL, SP_HistMax, SP_NodeTm, SP_SingDouble, SP_LmrCaptPct, SP_QsDelta, SP_TmDiv, SP_TmIncPct, SP_TmHardPct, SP_TmHardMul, SP_IirDepth, SP_NmpMinDepth, SP_LmrPv, SP_CheckExt, SP_ProbcutDepth, SP_RazorDepth, SP_LmrImproving, SP_LmrCut, SP_TmMaxFactor, SP_TmStopPct, SP_NoDrawTT, SP_CorrHist, SP_RepTwofold, SP_ClearTT;
extern SParam SParams[];
extern int SParamCount;
void sparams_changed();
