#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>
#include <unordered_map>
#include <iomanip>
#include <shared_mutex>

#include "binaryninjaapi.h"
#include "lowlevelilinstruction.h"
#include "mediumlevelilinstruction.h"


using namespace BinaryNinja;
using namespace std;

#if defined(_MSC_VER)
	#define snprintf _snprintf
#endif


std::mutex g_mutex;

static const char* dbKey = "inlineCalls";
static const char* globalTable = "global";
static const char* localTable = "local";
static const char* loggerName = "inliner";


std::string hex_addr(uint64_t addr)
{
	stringstream stream;
	stream << hex << addr;
	return stream.str();
}

Json::Value GetInlinerStateUnprotected(Function* function)
{
	auto db = function->GetView()->GetFile()->GetDatabase();

	if (!db || !db->HasGlobal(dbKey))
	{
		return Json::nullValue;
	}

	return db->ReadGlobal(dbKey);
}

extern "C"
{
	BN_DECLARE_CORE_ABI_VERSION

	void ModifyGlobalData(BinaryView* view, const function<void(Json::Value&)>& func, Ref<Logger> logger)
	{
		std::unique_lock<std::mutex> lock(g_mutex);
		Json::Value val;
		auto db = view->GetFile()->GetDatabase();

		if (!db)
		{
			logger->LogError("You can only mark functions for inlining with a saved analysis database!");
		}

		if (db->HasGlobal(dbKey))
		{
			val = db->ReadGlobal(dbKey);
		}

		func(val);

		db->WriteGlobal(dbKey, val);
	}

	void UpsertLocalCallSiteIntoAnalysisDB(BinaryView* view, Function* func)
	{
		auto logger = LogRegistry::GetLogger(loggerName);

		ModifyGlobalData(
			view,
			[view, func](Json::Value& data) {
				auto func_key = hex_addr(func->GetStart());
				data[localTable][func_key].append(view->GetCurrentOffset());
				func->Reanalyze();
			},
			logger);
	}

	void UpsertFunctionIntoAnalysisDB(BinaryView* view, Function* func)
	{
		auto logger = LogRegistry::GetLogger(loggerName);

		ModifyGlobalData(
			view,
			[view, func, logger](Json::Value& data) {
				int i = 0, j = 0;
				auto func_addr = func->GetStart();
				auto func_key = hex_addr(func_addr);
				data[globalTable][func_key.c_str()] = func_addr;
				logger->LogInfo("Checking for callsites to %s", func_key.c_str());
				for (const auto& callSite : view->GetCodeReferences(func_addr))
				{
					logger->LogInfo("Found callsite %" PRIx64, callSite.addr);
					if (callSite.func)
					{
						logger->LogInfo("Call site reanalyzed at %" PRIx64, callSite.func->GetStart());
						callSite.func->Reanalyze();
						j++;
					}
					i++;
				}
				logger->LogInfo("Found %d callsites, reanalysed %d", i, j);
			},
			logger);
	}

	void RemoveFunctionFromAnalysisDB(BinaryView* view, Function* func)
	{
		auto logger = LogRegistry::GetLogger(loggerName);

		ModifyGlobalData(
			view,
			[view, func, logger](Json::Value& data) {
				int i = 0, j = 0;
				auto func_addr = func->GetStart();
				auto func_key = hex_addr(func_addr);
				data[globalTable].removeMember(func_key);
				logger->LogInfo("Checking for callsites to %s", func_key.c_str());
				for (const auto& callSite : view->GetCodeReferences(func_addr))
				{
					logger->LogInfo("Found callsite %" PRIx64, callSite.addr);
					if (callSite.func)
					{
						logger->LogInfo("Call site reanalyzed at %" PRIx64, callSite.func->GetStart());
						callSite.func->Reanalyze();
						j++;
					}
					i++;
				}
				logger->LogInfo("Found %d callsites, reanalysed %d", i, j);
			},
			logger);
	}

	bool IsFunctionReturn(const LowLevelILInstruction& tinst, Platform* platform, Logger* logger)
	{
		if (tinst.operation == LLIL_RET || tinst.operation == LLIL_TAILCALL)
			return true;

		if (tinst.operation == LLIL_JUMP)
		{
			auto arch = platform->GetArchitecture();
			auto destExpr = tinst.GetDestExpr<LLIL_JUMP>();
			if (destExpr.GetValue().state == ReturnAddressValue)
			{
				logger->LogInfo("Treating jump to pushed lr value at 0x%x as return", tinst.address);
				return true;
			}

			auto operand = destExpr.GetOperands()[0];
			if (operand.GetType() == BinaryNinja::RegisterLowLevelOperand
				&& operand.GetRegister() == arch->GetLinkRegister())
			{
				logger->LogInfo("Treating jump(lr) at 0x%x as return", tinst.address);
				return true;
			}
		}

		return false;
	}

	void FunctionInliner(AnalysisContext* analysisContext)
	{
		std::unique_lock<std::mutex> lock(g_mutex);
		auto logger = LogRegistry::GetLogger(loggerName);

		Ref<Function> function = analysisContext->GetFunction();
		Ref<BinaryView> data = function->GetView();

		auto inliningData = GetInlinerStateUnprotected(function);

		if (inliningData.isNull())
		{
			return;
		}

		auto func_key = hex_addr(function->GetStart());
		auto localInlines = inliningData[localTable][func_key.c_str()];
		auto globalInlines = inliningData[globalTable];
		lock.unlock();

		bool updated = false;
		uint8_t opcode[BN_MAX_INSTRUCTION_LENGTH];
		InstructionInfo iInfo;
		Ref<LowLevelILFunction> llilFunc = analysisContext->GetLowLevelILFunction();

		for (auto& i : llilFunc->GetBasicBlocks())
		{
			Ref<Architecture> arch = i->GetArchitecture();
			for (size_t instrIndex = i->GetStart(); instrIndex < i->GetEnd(); instrIndex++)
			{
				LowLevelILInstruction instr = llilFunc->GetInstruction(instrIndex);
				uint64_t currentAddr = instr.address;
				bool isCallSiteInlined = std::any_of(localInlines.begin(), localInlines.end(),
					[currentAddr](auto value) { return value == currentAddr; });

				uint64_t platformAddr;
				LowLevelILInstruction destExpr;

				if (instr.operation == LLIL_CALL)
				{
					destExpr = instr.GetDestExpr<LLIL_CALL>();
				}
				else if (instr.operation == LLIL_TAILCALL)
				{
					destExpr = instr.GetDestExpr<LLIL_TAILCALL>();
				}
				else
				{
					if (isCallSiteInlined)
						logger->LogWarn(
							"Failed to inline function at: 0x%" PRIx64 ". Mapping to LLIL_CALL Failed!", instr.address);
					continue;
				}

				RegisterValue target = destExpr.GetValue();
				if (target.IsConstant())
					platformAddr = target.value;
				else
				{
					if (isCallSiteInlined)
						logger->LogWarn(
							"Failed to inline function at: 0x%" PRIx64 ". Destination not Constant!", instr.address);
					continue;
				}

				auto calleeAddressKey = hex_addr(platformAddr);
				if (!isCallSiteInlined && !globalInlines[calleeAddressKey.c_str()])
					continue;

				size_t opLen = data->Read(opcode, currentAddr, arch->GetMaxInstructionLength());
				if (!opLen || !arch->GetInstructionInfo(opcode, currentAddr, opLen, iInfo))
				{
					logger->LogInfo("Failed to get instruction info at 0x%x", currentAddr);
					continue;
				}
				Ref<Platform> platform = iInfo.archTransitionByTargetAddr ?
					function->GetPlatform()->GetAssociatedPlatformByAddress(platformAddr) :
                    function->GetPlatform();
				if (platform)
				{
					Ref<Function> targetFunc = data->GetAnalysisFunction(platform, platformAddr);
					auto targetLlil = targetFunc->GetLowLevelIL();
					LowLevelILLabel inlineStartLabel;
					llilFunc->MarkLabel(inlineStartLabel);
					instr.Replace(llilFunc->Goto(inlineStartLabel));

					llilFunc->PrepareToCopyFunction(targetLlil);
					for (auto& ti : targetLlil->GetBasicBlocks())
					{
						ExprId prev;
						llilFunc->PrepareToCopyBlock(ti);
						for (size_t tinstrIndex = ti->GetStart(); tinstrIndex < ti->GetEnd(); tinstrIndex++)
						{
							LowLevelILInstruction tinstr = targetLlil->GetInstruction(tinstrIndex);
							if (IsFunctionReturn(tinstr, platform, logger))
							{
								auto targ = llilFunc->GetInstruction(instrIndex + 1);
								if (targ.operation == LLIL_IF)
								{
									auto condOperands = targ.GetConditionExpr().GetOperands();
									auto hasFlagIL = std::any_of(
										condOperands.begin(), condOperands.end(), [](const LowLevelILOperand& operand) {
											return operand.GetType() == BinaryNinja::FlagLowLevelOperand;
										});
									if (hasFlagIL)
									{
										auto prevInstr = llilFunc->GetInstruction(prev);
										auto condExpr = targ.GetConditionExpr<LLIL_IF>().exprIndex;
										LowLevelILLabel trueTarget;
										trueTarget.operand = targ.GetTrueTarget<LLIL_IF>();
										LowLevelILLabel falseTarget;
										falseTarget.operand = targ.GetFalseTarget<LLIL_IF>();

										for (const auto& item : condOperands)
										{
											if (item.GetType() == BinaryNinja::FlagLowLevelOperand)
											{
												// Equal comparison
												if (LLIL_SUB == prevInstr.operation
													&& ZeroFlagRole == arch->GetFlagRole(item.GetFlag()))
												{
													condExpr = llilFunc->CompareEqual(0,
														prevInstr.GetLeftExpr<LLIL_SUB>().exprIndex,
														prevInstr.GetRightExpr<LLIL_SUB>().exprIndex);
												}
											}
										}

										llilFunc->AddInstruction(llilFunc->If(condExpr, trueTarget, falseTarget));
										targ.Replace(llilFunc->Nop());
									}
								}
								LowLevelILLabel label;
								label.operand = instrIndex + 1;
								llilFunc->AddInstruction(llilFunc->Goto(label));
							}
							else
							{
								prev = llilFunc->AddInstruction(tinstr.CopyTo(llilFunc));
							}
						}
					}
				}
				else
				{
					logger->LogInfo("Failed to get platform info for 0x%x", platformAddr);
				}

				updated = true;
			}

			if (updated)
				llilFunc->Finalize();
		}

		// Updates found, regenerate SSA form
		if (updated)
			llilFunc->GenerateSSAForm();
	}

	bool InlinerIsValid(BinaryView* view, Function* func)
	{
		if (auto workflow = func->GetWorkflow(); workflow)
			return workflow->Contains("extension.functionInliner");
		return !!view->GetFile()->GetDatabase();
	}

	bool CanBeGloballyInlined(BinaryView* view, Function* func)
	{
		std::unique_lock<std::mutex> lock(g_mutex);
		if (!InlinerIsValid(view, func))
			return false;

		auto inliningData = GetInlinerStateUnprotected(func);
		auto func_key = hex_addr(func->GetStart());
		auto globalInlines = inliningData[globalTable];

		return !globalInlines.isMember(func_key);
	}

	bool IsGloballyInlined(BinaryView* view, Function* func)
	{
		std::unique_lock<std::mutex> lock(g_mutex);
		if (!InlinerIsValid(view, func))
			return false;

		auto inliningData = GetInlinerStateUnprotected(func);
		auto func_key = hex_addr(func->GetStart());
		auto globalInlines = inliningData[globalTable];

		return globalInlines.isMember(func_key);
	}

	BINARYNINJAPLUGIN bool CorePluginInit()
	{
		LogRegistry::CreateLogger(loggerName);

		PluginCommand::RegisterForFunction("Optimizer\\Inline Current Function Globally",
			"Inline all calls to the current function.", UpsertFunctionIntoAnalysisDB, CanBeGloballyInlined);

		PluginCommand::RegisterForFunction("Optimizer\\Remove Global Inline of Current Function",
			"Delete all inline calls to the current function.", RemoveFunctionFromAnalysisDB, IsGloballyInlined);

		PluginCommand::RegisterForFunction("Optimizer\\Inline Function at Current Call Site",
			"Inline function call at current call site.", UpsertLocalCallSiteIntoAnalysisDB, InlinerIsValid);

		Ref<Workflow> inlinerWorkflow = Workflow::Instance()->Clone("InlinerWorkflow");
		inlinerWorkflow->RegisterActivity(new Activity("extension.functionInliner", &FunctionInliner));
		inlinerWorkflow->Insert("core.function.translateTailCalls", "extension.functionInliner");
		Workflow::RegisterWorkflow(inlinerWorkflow,
			R"#({
			"title" : "Function Inliner",
			"description" : "This analysis stands in as an example to demonstrate Binary Ninja's extensible analysis APIs. ***Note** this feature is under active development and subject to change 
without notice.",
			"capabilities" : []
			})#");

		return true;
	}
}
