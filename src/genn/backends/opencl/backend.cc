#include "backend.h"

// Standard C++ includes
#include <algorithm>

// PLOG includes
#include <plog/Log.h>

// GeNN includes
#include "gennUtils.h"
#include "modelSpecInternal.h"

// GeNN code generator includes
#include "code_generator/codeStream.h"
#include "code_generator/substitutions.h"
#include "code_generator/codeGenUtils.h"

// OpenCL backend includes
#include "utils.h"

//--------------------------------------------------------------------------
// Anonymous namespace
//--------------------------------------------------------------------------
namespace {

//! TO BE IMPLEMENTED - Use OpenCL functions
const std::vector<CodeGenerator::FunctionTemplate> openclFunctions = {
	{"gennrand_uniform", 0, "uniform_double($(rng))", "uniform($(rng))"},
	{"gennrand_normal", 0, "normal_double($(rng))", "normal($(rng))"},
	{"gennrand_exponential", 0, "exponentialDistDouble($(rng))", "exponentialDistFloat($(rng))"},
	{"gennrand_log_normal", 2, "log_normal_double($(rng), $(0), $(1))", "log_normal_float($(rng), $(0), $(1))"},
	{"gennrand_gamma", 1, "gammaDistDouble($(rng), $(0))", "gammaDistFloat($(rng), $(0))"}
};

//--------------------------------------------------------------------------
// Timer
//--------------------------------------------------------------------------
class Timer {
public:
	// TO BE REVIEWED
	Timer(CodeGenerator::CodeStream& codeStream, const std::string& name, bool timingEnabled, bool synchroniseOnStop = false)
	:	m_CodeStream(codeStream), m_Name(name), m_TimingEnabled(timingEnabled), m_SynchroniseOnStop(synchroniseOnStop) {
		
	}
private:
	//--------------------------------------------------------------------------
	// Members
	//--------------------------------------------------------------------------
	CodeGenerator::CodeStream& m_CodeStream;
	const std::string m_Name;
	const bool m_TimingEnabled;
	const bool m_SynchroniseOnStop;
};


void gennExtraGlobalParamPass(CodeGenerator::CodeStream& os, const std::map<std::string, std::string>::value_type& p)
{
	if (Utils::isTypePointer(p.second)) {
		os << "d_" << p.first << ", ";
	}
	else {
		os << p.first << ", ";
	}
}
//-----------------------------------------------------------------------
bool isSparseInitRequired(const SynapseGroupInternal& sg)
{
	return ((sg.getMatrixType() & SynapseMatrixConnectivity::SPARSE)
		&& (sg.isWUVarInitRequired() || !sg.getWUModel()->getLearnPostCode().empty() || !sg.getWUModel()->getSynapseDynamicsCode().empty()));
}
//-----------------------------------------------------------------------
void updateExtraGlobalParams(const std::string& varSuffix, const std::string& codeSuffix, const Snippet::Base::EGPVec& extraGlobalParameters,
	std::map<std::string, std::string>& kernelParameters, const std::vector<std::string>& codeStrings)
{
	// Loop through list of global parameters
	for (const auto& p : extraGlobalParameters) {
		// If this parameter is used in any codestrings, add it to list of kernel parameters
		if (std::any_of(codeStrings.cbegin(), codeStrings.cend(),
			[p, codeSuffix](const std::string& c) { return c.find("$(" + p.name + codeSuffix + ")") != std::string::npos; }))
		{
			kernelParameters.emplace(p.name + varSuffix, p.type);
		}
	}
}
}

namespace CodeGenerator
{
namespace OpenCL
{
const char* Backend::KernelNames[KernelMax] = {
	"updateNeuronsKernel",
	"updatePresynapticKernel",
	"updatePostsynapticKernel",
	"updateSynapseDynamicsKernel",
	"initializeKernel",
	"initializeSparseKernel",
	"preNeuronResetKernel",
	"preSynapseResetKernel" };
const char* Backend::ProgramNames[ProgramMax] = {
	"initProgram",
	"updateNeuronsProgram" };
//--------------------------------------------------------------------------
Backend::Backend(const KernelWorkGroupSize& kernelWorkGroupSizes, const Preferences& preferences,
	int localHostID, const std::string& scalarType, int device)
	: BackendBase(localHostID, scalarType), m_KernelWorkGroupSizes(kernelWorkGroupSizes), m_Preferences(preferences), m_ChosenDeviceID(device)
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::Backend");
}
//--------------------------------------------------------------------------
void Backend::genNeuronUpdate(CodeStream& os, const ModelSpecInternal& model, NeuronGroupSimHandler simHandler, NeuronGroupHandler wuVarUpdateHandler) const
{
	// Generate reset kernel to be run before the neuron kernel

	//! KernelPreNeuronReset START
	size_t idPreNeuronReset = 0;
	
	// Vector to collect the parameters for the kernel
	std::map<std::string, std::string> preNeuronResetKernelParams;

	// Creating the kernel body separately to collect all arguments and put them into the main kernel
	std::stringstream preNeuronResetKernelBodyStream;
	CodeStream preNeuronResetKernelBody(preNeuronResetKernelBodyStream);

	preNeuronResetKernelBody << "size_t groupId = get_group_id(0);" << std::endl;
	preNeuronResetKernelBody << "size_t localId = get_local_id(0);" << std::endl;
	preNeuronResetKernelBody << "unsigned int id = " << m_KernelWorkGroupSizes[KernelPreNeuronReset] << " * groupId + localId;" << std::endl;

	// Loop through remote neuron groups
	for (const auto& n : model.getRemoteNeuronGroups()) {
		if (n.second.hasOutputToHost(getLocalHostID()) && n.second.isDelayRequired()) {
			if (idPreNeuronReset > 0) {
				preNeuronResetKernelBody << "else ";
			}
			preNeuronResetKernelBody << "if(id == " << (idPreNeuronReset++) << ")";
			{
				CodeStream::Scope b(preNeuronResetKernelBody);
				preNeuronResetKernelBody << "d_spkQuePtr" << n.first << " = (d_spkQuePtr" << n.first << " + 1) % " << n.second.getNumDelaySlots() << ";" << std::endl;
			}
			preNeuronResetKernelParams.insert({ "d_spkQuePtr" + n.first, "__global unsigned int*" });
		}
	}

	// Loop through local neuron groups
	for (const auto& n : model.getLocalNeuronGroups()) {
		if (idPreNeuronReset > 0) {
			preNeuronResetKernelBody << "else ";
		}
		if (n.second.isSpikeEventRequired()) {
			preNeuronResetKernelParams.insert({ "d_glbSpkCntEvnt" + n.first, "__global unsigned int*" });
		}
		preNeuronResetKernelBody << "if(id == " << (idPreNeuronReset++) << ")";
		{
			CodeStream::Scope b(preNeuronResetKernelBody);

			if (n.second.isDelayRequired()) { // with delay
				preNeuronResetKernelBody << "d_spkQuePtr" << n.first << " = (d_spkQuePtr" << n.first << " + 1) % " << n.second.getNumDelaySlots() << ";" << std::endl;

				if (n.second.isSpikeEventRequired()) {
					preNeuronResetKernelBody << "d_glbSpkCntEvnt" << n.first << "[d_spkQuePtr" << n.first << "] = 0;" << std::endl;
				}
				if (n.second.isTrueSpikeRequired()) {
					preNeuronResetKernelBody << "d_glbSpkCnt" << n.first << "[d_spkQuePtr" << n.first << "] = 0;" << std::endl;
				}
				else {
					preNeuronResetKernelBody << "d_glbSpkCnt" << n.first << "[0] = 0;" << std::endl;
				}
				preNeuronResetKernelParams.insert({ "d_spkQuePtr" + n.first, "__global unsigned int*" });
			}
			else { // no delay
				if (n.second.isSpikeEventRequired()) {
					preNeuronResetKernelBody << "d_glbSpkCntEvnt" << n.first << "[0] = 0;" << std::endl;
				}
				preNeuronResetKernelBody << "d_glbSpkCnt" << n.first << "[0] = 0;" << std::endl;
			}
			preNeuronResetKernelParams.insert({ "d_glbSpkCnt" + n.first, "__global unsigned int*" });
		}
	}
	//! KernelPreNeuronReset END

	//! KernelNeuronUpdate START
	std::stringstream updateNeuronsKernelBodyStream;
	CodeStream updateNeuronsKernelBody(updateNeuronsKernelBodyStream);

	std::map<std::string, std::string> updateNeuronsKernelParams;

    // Add extra global parameters references by neuron models to map of kernel parameters
	std::map<std::string, std::string> neuronKernelParameters;
	for (const auto& n : model.getLocalNeuronGroups()) {
		const auto* nm = n.second.getNeuronModel();
		updateExtraGlobalParams(n.first, "", nm->getExtraGlobalParams(), neuronKernelParameters,
			{ nm->getSimCode(), nm->getThresholdConditionCode(), nm->getResetCode() });
	}

	// Add extra global parameters references by current source models to map of kernel parameters
	for (const auto& c : model.getLocalCurrentSources()) {
		const auto* csm = c.second.getCurrentSourceModel();
		updateExtraGlobalParams(c.first, "", csm->getExtraGlobalParams(), neuronKernelParameters,
			{ csm->getInjectionCode() });
	}

	// Add extra global parameters referenced by postsynaptic models and
	// event thresholds of weight update models to map of kernel parameters
	for (const auto& s : model.getLocalSynapseGroups()) {
		const auto* psm = s.second.getPSModel();
		updateExtraGlobalParams(s.first, "", psm->getExtraGlobalParams(), neuronKernelParameters,
			{ psm->getDecayCode(), psm->getApplyInputCode() });

		const auto* wum = s.second.getWUModel();
		updateExtraGlobalParams(s.first, "", wum->getExtraGlobalParams(), neuronKernelParameters,
			{ wum->getEventThresholdConditionCode() });
	}

	size_t idStart = 0;

	//! KernelNeuronUpdate BODY START
	updateNeuronsKernelBody << "size_t groupId = get_group_id(0);" << std::endl;
	updateNeuronsKernelBody << "size_t localId = get_local_id(0);" << std::endl;
	updateNeuronsKernelBody << "const unsigned int id = " << m_KernelWorkGroupSizes[KernelNeuronUpdate] << " * groupId + localId; " << std::endl;

	Substitutions kernelSubs(openclFunctions, model.getPrecision());
	kernelSubs.addVarSubstitution("t", "t");

	// If any neuron groups emit spike events
	if (std::any_of(model.getLocalNeuronGroups().cbegin(), model.getLocalNeuronGroups().cend(),
		[](const ModelSpec::NeuronGroupValueType& n) { return n.second.isSpikeEventRequired(); }))
	{
		updateNeuronsKernelBody << "volatile __local unsigned int shSpkEvnt[" << m_KernelWorkGroupSizes[KernelNeuronUpdate] << "];" << std::endl;
		updateNeuronsKernelBody << "volatile __local unsigned int shPosSpkEvnt;" << std::endl;
		updateNeuronsKernelBody << "volatile __local unsigned int shSpkEvntCount;" << std::endl;
		updateNeuronsKernelBody << std::endl;
		updateNeuronsKernelBody << "if (localId == 1);";
		{
			CodeStream::Scope b(updateNeuronsKernelBody);
			updateNeuronsKernelBody << "shSpkEvntCount = 0;" << std::endl;
		}
		updateNeuronsKernelBody << std::endl;
	}

	// If any neuron groups emit true spikes
	if (std::any_of(model.getLocalNeuronGroups().cbegin(), model.getLocalNeuronGroups().cend(),
		[](const ModelSpec::NeuronGroupValueType& n) { return !n.second.getNeuronModel()->getThresholdConditionCode().empty(); }))
	{
		updateNeuronsKernelBody << "volatile __local unsigned int shSpk[" << m_KernelWorkGroupSizes[KernelNeuronUpdate] << "];" << std::endl;
		updateNeuronsKernelBody << "volatile __local unsigned int shPosSpk;" << std::endl;
		updateNeuronsKernelBody << "volatile __local unsigned int shSpkCount;" << std::endl;
		updateNeuronsKernelBody << "if (localId == 0);";
		{
			CodeStream::Scope b(updateNeuronsKernelBody);
			updateNeuronsKernelBody << "shSpkCount = 0;" << std::endl;
		}
		updateNeuronsKernelBody << std::endl;
	}

	updateNeuronsKernelBody << "barrier(CLK_LOCAL_MEM_FENCE);" << std::endl;

	// Parallelise over neuron groups
	genParallelGroup<NeuronGroupInternal>(updateNeuronsKernelBody, kernelSubs, model.getLocalNeuronGroups(), idStart,
		[this](const NeuronGroupInternal& ng) { return Utils::padSize(ng.getNumNeurons(), m_KernelWorkGroupSizes[KernelNeuronUpdate]); },
		[&model, simHandler, wuVarUpdateHandler, this, &updateNeuronsKernelParams](CodeStream& updateNeuronsKernelBody, const NeuronGroupInternal& ng, Substitutions& popSubs)
		{
			// If axonal delays are required
			if (ng.isDelayRequired()) {
				// We should READ from delay slot before spkQuePtr
				updateNeuronsKernelBody << "const unsigned int readDelayOffset = " << ng.getPrevQueueOffset("d_") << ";" << std::endl;

				// And we should WRITE to delay slot pointed to be spkQuePtr
				updateNeuronsKernelBody << "const unsigned int writeDelayOffset = " << ng.getCurrentQueueOffset("d_") << ";" << std::endl;
			}
			updateNeuronsKernelBody << std::endl;

			// If this neuron group requires a simulation RNG, substitute in this neuron group's RNG
			//! TO BE IMPLEMENTED - Not using range at this point - 2020/03/08
			if (ng.isSimRNGRequired()) {
				popSubs.addVarSubstitution("rng", "&dd_rng" + ng.getName() + "[" + popSubs["id"] + "]");
			}

			// Call handler to generate generic neuron code
			updateNeuronsKernelBody << "if(" << popSubs["id"] << " < " << ng.getNumNeurons() << ")";
			{
				CodeStream::Scope b(updateNeuronsKernelBody);
				simHandler(updateNeuronsKernelBody, ng, popSubs,
					// Emit true spikes
					[this](CodeStream& updateNeuronsKernelBody, const NeuronGroupInternal&, Substitutions& subs)
					{
						genEmitSpike(updateNeuronsKernelBody, subs, "");
					},
					// Emit spike-like events
					[this](CodeStream& updateNeuronsKernelBody, const NeuronGroupInternal&, Substitutions& subs)
					{
						genEmitSpike(updateNeuronsKernelBody, subs, "Evnt");
					});
			}

			updateNeuronsKernelBody << "barrier(CLK_LOCAL_MEM_FENCE);" << std::endl;

			if (ng.isSpikeEventRequired()) {
				updateNeuronsKernelBody << "if (localId == 1)";
				{
					CodeStream::Scope b(updateNeuronsKernelBody);
					updateNeuronsKernelBody << "if (shSpkEvntCount > 0)";
					{
						CodeStream::Scope b(updateNeuronsKernelBody);
						updateNeuronsKernelBody << "shPosSpkEvnt = atomic_add(&d_glbSpkCntEvnt" << ng.getName();
						updateNeuronsKernelParams.insert({ "d_glbSpkCntEvnt" + ng.getName(), "__global unsigned int*"}); // Add argument
						if (ng.isDelayRequired()) {
							updateNeuronsKernelBody << "[d_spkQuePtr" << ng.getName() << "], shSpkEvntCount);" << std::endl;
							updateNeuronsKernelParams.insert({ "d_spkQuePtr" + ng.getName(), "__global unsigned int*" }); // Add argument
						}
						else {
							updateNeuronsKernelBody << "[0], shSpkEvntCount);" << std::endl;
						}
					}
				} // end if (localId == 0)
				updateNeuronsKernelBody << "barrier(CLK_LOCAL_MEM_FENCE);" << std::endl;
			}

			if (!ng.getNeuronModel()->getThresholdConditionCode().empty()) {
				updateNeuronsKernelBody << "if (localId == 0)";
				{
					CodeStream::Scope b(updateNeuronsKernelBody);
					updateNeuronsKernelBody << "if (shSpkCount > 0)";
					{
						CodeStream::Scope b(updateNeuronsKernelBody);
						updateNeuronsKernelBody << "shPosSpk = atomic_add(&d_glbSpkCnt" << ng.getName();
						updateNeuronsKernelParams.insert({ "d_glbSpkCnt" + ng.getName(), "__global unsigned int*" }); // Add argument
						if (ng.isDelayRequired() && ng.isTrueSpikeRequired()) {
							updateNeuronsKernelBody << "[d_spkQuePtr" << ng.getName() << "], shSpkCount);" << std::endl;
							updateNeuronsKernelParams.insert({ "d_spkQuePtr" + ng.getName(), "__global unsigned int*" }); // Add argument
						}
						else {
							updateNeuronsKernelBody << "[0], shSpkCount);" << std::endl;
						}
					}
				} // end if (localId == 1)
				updateNeuronsKernelBody << "barrier(CLK_LOCAL_MEM_FENCE);" << std::endl;
			}

			const std::string queueOffset = ng.isDelayRequired() ? "writeDelayOffset + " : "";
			if (ng.isSpikeEventRequired()) {
				updateNeuronsKernelBody << "if (localId < shSpkEvntCount)";
				{
					CodeStream::Scope b(updateNeuronsKernelBody);
					updateNeuronsKernelBody << "d_glbSpkEvnt" << ng.getName() << "[" << queueOffset << "shPosSpkEvnt + localId] = shSpkEvnt[localId];" << std::endl;
					updateNeuronsKernelParams.insert({ "d_glbSpkEvnt" + ng.getName(), "__global unsigned int*" }); // Add argument
				}
			}

			if (!ng.getNeuronModel()->getThresholdConditionCode().empty()) {
				const std::string queueOffsetTrueSpk = ng.isTrueSpikeRequired() ? queueOffset : "";

				updateNeuronsKernelBody << "if (localId < shSpkCount)";
				{
					CodeStream::Scope b(updateNeuronsKernelBody);

					updateNeuronsKernelBody << "const unsigned int n = shSpk[localId];" << std::endl;

					// Create new substition stack and explicitly replace id with 'n' and perform WU var update
					Substitutions wuSubs(&popSubs);
					wuSubs.addVarSubstitution("id", "n", true);
					wuVarUpdateHandler(updateNeuronsKernelBody, ng, wuSubs);

					updateNeuronsKernelBody << "d_glbSpk" << ng.getName() << "[" << queueOffsetTrueSpk << "shPosSpk + localId] = n;" << std::endl;
					updateNeuronsKernelParams.insert({ "d_glbSpk" + ng.getName(), "__global unsigned int*" }); // Add argument
					if (ng.isSpikeTimeRequired()) {
						updateNeuronsKernelBody << "d_sT" << ng.getName() << "[" << queueOffset << "n] = t;" << std::endl;
						updateNeuronsKernelParams.insert({ "d_sT" + ng.getName(), "__global unsigned int*" }); // Add argument
					}
				}
			}
		}
	);
	//! KernelNeuronUpdate BODY END
	//! KernelNeuronUpdate END

	// Neuron update kernels
	os << "extern \"C\" const char* " << ProgramNames[ProgramNeuronsUpdate] << "Src = R\"(typedef float scalar;" << std::endl;
	os << std::endl;
	// KernelPreNeuronReset definition
	os << "__kernel void " << KernelNames[KernelPreNeuronReset] << "(";
	{
		int argCnt = 0;
		for (const auto& arg : preNeuronResetKernelParams) {
			if (argCnt == preNeuronResetKernelParams.size() - 1) {
				os << arg.second << " " << arg.first;
			} else {
				os << arg.second << " " << arg.first << ", ";
			}
			argCnt++;
		}
	}
	os << ")";
	{
		CodeStream::Scope b(os);
		os << preNeuronResetKernelBodyStream.str();
	}
	os << std::endl;
	// KernelNeuronUpdate definition
	std::vector<std::string> neuronUpdateKernelArgsForKernel;
	os << "__kernel void " << KernelNames[KernelNeuronUpdate] << "(";
	for (const auto& p : neuronKernelParameters) {
		os << p.second << " " << p.first << ", ";
		neuronUpdateKernelArgsForKernel.push_back(p.first);
	}
	for (const auto& arg : updateNeuronsKernelParams) {
		os << arg.second << " " << arg.first<< ", ";
		neuronUpdateKernelArgsForKernel.push_back(arg.first);
	}
	// Passing the neurons to the kernel as kernel arguments
	// Remote neuron groups
	for (const auto& ng : model.getRemoteNeuronGroups()) {
		auto* nm = ng.second.getNeuronModel();
		for (const auto& v : nm->getVars()) {
			os << "__global " << v.type << "* " << getVarPrefix() << v.name << ng.second.getName() << ", ";
			neuronUpdateKernelArgsForKernel.push_back(getVarPrefix() + v.name + ng.second.getName());
		}
	}
	// Local neuron groups
	for (const auto& ng : model.getLocalNeuronGroups()) {
		auto* nm = ng.second.getNeuronModel();
		for (const auto& v : nm->getVars()) {
			os << "__global " << v.type << "* " << getVarPrefix() << v.name << ng.second.getName() << ", ";
			neuronUpdateKernelArgsForKernel.push_back(getVarPrefix() + v.name + ng.second.getName());
		}
	}
	os << "const float DT, ";
	neuronUpdateKernelArgsForKernel.push_back("DT");
	os << model.getTimePrecision() << " t)";
	{
		CodeStream::Scope b(os);
		os << updateNeuronsKernelBodyStream.str();
	}
	// Closing the multiline char* containing all kernels for updating neurons
	os << ")\";" << std::endl;

	os << std::endl;

	// Function for initializing the KernelNeuronUpdate kernels
	os << "// Initialize the neuronUpdate kernels" << std::endl;
	os << "void initUpdateNeuronsKernels()";
	{
		CodeStream::Scope b(os);

		// KernelPreNeuronReset initialization
		os << KernelNames[KernelPreNeuronReset] << " = cl::Kernel(" << ProgramNames[ProgramNeuronsUpdate] << ", \"" << KernelNames[KernelPreNeuronReset] << "\");" << std::endl;
		{
			int argCnt = 0;
			for (const auto& arg : preNeuronResetKernelParams) {
				os << "CHECK_OPENCL_ERRORS(" << KernelNames[KernelPreNeuronReset] << ".setArg(" << argCnt << ", " << arg.first << "));" << std::endl;
				argCnt++;
			}
		}
		os << std::endl;
		// KernelNeuronUpdate initialization
		os << KernelNames[KernelNeuronUpdate] << " = cl::Kernel(" << ProgramNames[ProgramNeuronsUpdate] << ", \"" << KernelNames[KernelNeuronUpdate] << "\");" << std::endl;
		for (int i = 0; i < neuronUpdateKernelArgsForKernel.size(); i++) {
			os << "CHECK_OPENCL_ERRORS(" << KernelNames[KernelNeuronUpdate] << ".setArg(" << i << ", " << neuronUpdateKernelArgsForKernel[i] << "));" << std::endl;
		}
	}

	os << std::endl;

	os << "void updateNeurons(" << model.getTimePrecision() << " t)";
	{
		CodeStream::Scope b(os);
		if (idPreNeuronReset > 0) {
			os << "CHECK_OPENCL_ERRORS(commandQueue.enqueueNDRangeKernel(" << KernelNames[KernelPreNeuronReset] << ", cl::NullRange, " << "cl::NDRange(" << m_KernelWorkGroupSizes[KernelPreNeuronReset] << ")));" << std::endl;
			os << "CHECK_OPENCL_ERRORS(commandQueue.finish());" << std::endl;
			os << std::endl;
		}
		if (idStart > 0) {
			os << "CHECK_OPENCL_ERRORS(" << KernelNames[KernelNeuronUpdate] << ".setArg(" << neuronUpdateKernelArgsForKernel.size() /*last arg*/ << ", t));" << std::endl;
			os << "CHECK_OPENCL_ERRORS(commandQueue.enqueueNDRangeKernel(" << KernelNames[KernelNeuronUpdate] << ", cl::NullRange, " << "cl::NDRange(" << m_KernelWorkGroupSizes[KernelNeuronUpdate] << ")));" << std::endl;
			os << "CHECK_OPENCL_ERRORS(commandQueue.finish());" << std::endl;
		}
	}
}
//--------------------------------------------------------------------------
void Backend::genSynapseUpdate(CodeStream& os, const ModelSpecInternal& model,
	SynapseGroupHandler wumThreshHandler, SynapseGroupHandler wumSimHandler, SynapseGroupHandler wumEventHandler,
	SynapseGroupHandler postLearnHandler, SynapseGroupHandler synapseDynamicsHandler) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genSynapseUpdate");
	os << "void updateSynapses(float t)";
	{
		CodeStream::Scope b(os);
	}
}
//--------------------------------------------------------------------------
void Backend::genInit(CodeStream &os, const ModelSpecInternal &model,
                      NeuronGroupHandler localNGHandler, NeuronGroupHandler remoteNGHandler,
                      SynapseGroupHandler sgDenseInitHandler, SynapseGroupHandler sgSparseConnectHandler, 
                      SynapseGroupHandler sgSparseInitHandler) const
{
	os << std::endl;
	//! TO BE IMPLEMENTED - Generating minimal kernel

	// Build map of extra global parameters for init kernel
	std::map<std::string, std::string> initKernelParameters;
	for (const auto& s : model.getLocalSynapseGroups()) {
		const auto* initSparseConnectivitySnippet = s.second.getConnectivityInitialiser().getSnippet();
		updateExtraGlobalParams(s.first, "", initSparseConnectivitySnippet->getExtraGlobalParams(), initKernelParameters,
			{ initSparseConnectivitySnippet->getRowBuildCode() });
	}

	// initialization kernel code
	size_t idInitStart = 0;

	//! KernelInitialize BODY START
	Substitutions kernelSubs(openclFunctions, model.getPrecision());

	// Creating the kernel body separately to collect all arguments and put them into the main kernel
	std::stringstream initializeKernelBodyStream;
	CodeStream initializeKernelBody(initializeKernelBodyStream);

	initializeKernelBody << "size_t groupId = get_group_id(0);" << std::endl;
	initializeKernelBody << "size_t localId = get_local_id(0);" << std::endl;
	initializeKernelBody << "const unsigned int id = " << m_KernelWorkGroupSizes[KernelInitialize] << " * groupId + localId;" << std::endl;

	initializeKernelBody << "// ------------------------------------------------------------------------" << std::endl;
	initializeKernelBody << "// Remote neuron groups" << std::endl;
	genParallelGroup<NeuronGroupInternal>(initializeKernelBody, kernelSubs, model.getRemoteNeuronGroups(), idInitStart,
		[this](const NeuronGroupInternal& ng) { return Utils::padSize(ng.getNumNeurons(), m_KernelWorkGroupSizes[KernelInitialize]); },
		[this](const NeuronGroupInternal& ng) { return ng.hasOutputToHost(getLocalHostID()); },
		[this, remoteNGHandler](CodeStream& initializeKernelBody, const NeuronGroupInternal& ng, Substitutions& popSubs)
		{
			initializeKernelBody << "// only do this for existing neurons" << std::endl;
			initializeKernelBody << "if(" << popSubs["id"] << " < " << ng.getNumNeurons() << ")";
			{
				CodeStream::Scope b(initializeKernelBody);

				remoteNGHandler(initializeKernelBody, ng, popSubs);
			}
		});
	initializeKernelBody << std::endl;

	initializeKernelBody << "// ------------------------------------------------------------------------" << std::endl;
	initializeKernelBody << "// Local neuron groups" << std::endl;
	genParallelGroup<NeuronGroupInternal>(initializeKernelBody, kernelSubs, model.getLocalNeuronGroups(), idInitStart,
		[this](const NeuronGroupInternal& ng) { return Utils::padSize(ng.getNumNeurons(), m_KernelWorkGroupSizes[KernelInitialize]); },
		[this](const NeuronGroupInternal&) { return true; },
		[this, &model, localNGHandler](CodeStream& initializeKernelBody, const NeuronGroupInternal& ng, Substitutions& popSubs)
		{
			initializeKernelBody << "// only do this for existing neurons" << std::endl;
			initializeKernelBody << "if(" << popSubs["id"] << " < " << ng.getNumNeurons() << ")";
			{
				CodeStream::Scope b(initializeKernelBody);

				localNGHandler(initializeKernelBody, ng, popSubs);
			}
		});
	initializeKernelBody << std::endl;

	const size_t numStaticInitThreads = idInitStart;

	//! KernelInitialize BODY END

	// Initialization kernels
	os << "extern \"C\" const char* " << ProgramNames[ProgramInitialize] << "Src = R\"(typedef float scalar;" << std::endl;
	os << std::endl;

	// KernelInitialize definition
	std::vector<std::string> initializeKernelArgsForKernel;
	os << "__kernel void " << KernelNames[KernelInitialize] << "(";
	std::string nmName = model.getLocalNeuronGroups().begin()->second.getName();
	os << "__global unsigned int* " << getVarPrefix() << "glbSpkCnt" << nmName << ", ";
	os << "__global unsigned int* " << getVarPrefix() << "glbSpk" << nmName << ", ";
	initializeKernelArgsForKernel.push_back(getVarPrefix() + "glbSpkCnt" + nmName);
	initializeKernelArgsForKernel.push_back(getVarPrefix() + "glbSpk" + nmName);
	// Local neuron groups params
	for (const auto& ng : model.getLocalNeuronGroups()) {
		auto* nm = ng.second.getNeuronModel();
		for (const auto& v : nm->getVars()) {
			// Initialize only READ_WRITE variables
			if (v.access == VarAccess::READ_WRITE) {
				os << "__global " << v.type << "* " << getVarPrefix() << v.name << ng.second.getName() << ", ";
				initializeKernelArgsForKernel.push_back(getVarPrefix() + v.name + ng.second.getName());
			}
		}
	}
	for (const auto& p : initKernelParameters) {
		os << p.second << " " << p.first << ", ";
		initializeKernelArgsForKernel.push_back(p.first);
	}
	os << "unsigned int deviceRNGSeed)";
	{
		CodeStream::Scope b(os);
		os << initializeKernelBodyStream.str();
	}
	os << std::endl;
	// Closing the multiline char* containing all kernels for initializing neurons
	os << ")\";" << std::endl;

	os << std::endl;

	// Function for initializing the initialization kernels
	os << "// Initialize the initialization kernel(s)" << std::endl;
	os << "void initInitializationKernels()";
	{
		CodeStream::Scope b(os);

		// KernelInitialize initialization
		os << KernelNames[KernelInitialize] << " = cl::Kernel(" << ProgramNames[ProgramInitialize] << ", \"" << KernelNames[KernelInitialize] << "\");" << std::endl;
		{
			int argCnt = 0;
			for (const auto& arg : initializeKernelArgsForKernel) {
				os << "CHECK_OPENCL_ERRORS(" << KernelNames[KernelInitialize] << ".setArg(" << argCnt << ", " << arg << "));" << std::endl;
				argCnt++;
			}
		}
	}

	os << std::endl;

	os << "void initialize()";
	{
		CodeStream::Scope b(os);
		//! TO BE IMPLEMENTED - Using hard code deviceRNGSeed for now
		os << "unsigned int deviceRNGSeed = 0;" << std::endl;
		os << std::endl;
		os << "CHECK_OPENCL_ERRORS(" << KernelNames[KernelInitialize] << ".setArg(" << initializeKernelArgsForKernel.size() /*last arg*/ << ", deviceRNGSeed));" << std::endl;
		os << std::endl;
		os << "CHECK_OPENCL_ERRORS(commandQueue.enqueueNDRangeKernel(" << KernelNames[KernelInitialize] << ", cl::NullRange, " << "cl::NDRange(" << m_KernelWorkGroupSizes[KernelInitialize] << ")));" << std::endl;
		os << "CHECK_OPENCL_ERRORS(commandQueue.finish());" << std::endl;
	}

	os << std::endl;

	// Generating code for initializing all OpenCL elements - Using intializeSparse
	os << "// Initialize all OpenCL elements" << std::endl;
	os << "void initializeSparse()";
	{
		CodeStream::Scope b(os);
		// Copy all uninitialised state variables to device
		os << "copyStateToDevice(true);" << std::endl;
	}
}
//--------------------------------------------------------------------------
void Backend::genDefinitionsPreamble(CodeStream& os) const
{
	os << "// Standard C++ includes" << std::endl;
	os << "#include <string>" << std::endl;
	os << "#include <stdexcept>" << std::endl;
	os << std::endl;
	os << "// Standard C includes" << std::endl;
	os << "#include <cstdint>" << std::endl;
	os << "#include <cassert>" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genDefinitionsInternalPreamble(CodeStream& os) const
{
	os << "// OpenCL includes" << std::endl;
	os << "#define CL_USE_DEPRECATED_OPENCL_1_2_APIS" << std::endl;
	os << "#include <CL/cl.hpp>" << std::endl;
	os << std::endl;
	os << "#define DEVICE_INDEX " << m_ChosenDeviceID << std::endl;
	/*os << std::endl;
	os << "// Function for getting OpenCL errors as string" << std::endl;
	os << "const char * clGetErrorString(cl_int error)";
	{
		CodeStream::Scope b(os);
		std::map<cl_int, std::string> allClErrors = {
			// run-time and JIT compiler errors
			{ 0, "CL_SUCCESS" },							{ -1, "CL_DEVICE_NOT_FOUND" },				{ -2, "CL_DEVICE_NOT_AVAILABLE" },
			{ -3, "CL_COMPILER_NOT_AVAILABLE" },			{ -4, "CL_MEM_OBJECT_ALLOCATION_FAILURE" },	{ -5, "CL_OUT_OF_RESOURCES" },
			{ -6, "CL_OUT_OF_HOST_MEMORY" },				{ -7, "CL_PROFILING_INFO_NOT_AVAILABLE" },	{ -8, "CL_MEM_COPY_OVERLAP" },
			{ -9, "CL_IMAGE_FORMAT_MISMATCH" },				{ -10, "CL_IMAGE_FORMAT_NOT_SUPPORTED" },	{ -11, "CL_BUILD_PROGRAM_FAILURE" },
			{ -12, "CL_MAP_FAILURE" },						{ -13, "CL_MISALIGNED_SUB_BUFFER_OFFSET" }, { -14, "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST" },
			{ -15, "CL_COMPILE_PROGRAM_FAILURE" },			{ -16, "CL_LINKER_NOT_AVAILABLE" },			{ -17, "CL_LINK_PROGRAM_FAILURE" },
			{ -18, "CL_DEVICE_PARTITION_FAILED" },			{ -19, "CL_KERNEL_ARG_INFO_NOT_AVAILABLE" },
			// compile-time errors
			{ -30, "CL_INVALID_VALUE" },					{ -31, "CL_INVALID_DEVICE_TYPE" },			{ -32, "CL_INVALID_PLATFORM" },
			{ -33, "CL_INVALID_DEVICE" },					{ -34, "CL_INVALID_CONTEXT" },				{ -35, "CL_INVALID_QUEUE_PROPERTIES" },
			{ -36, "CL_INVALID_COMMAND_QUEUE" },			{ -37, "CL_INVALID_HOST_PTR" },				{ -38, "CL_INVALID_MEM_OBJECT" },
			{ -39, "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR" },	{ -40, "CL_INVALID_IMAGE_SIZE" },			{ -41, "CL_INVALID_SAMPLER" },
			{ -42, "CL_INVALID_BINARY" },					{ -43, "CL_INVALID_BUILD_OPTIONS" },		{ -44, "CL_INVALID_PROGRAM" },
			{ -45, "CL_INVALID_PROGRAM_EXECUTABLE" },		{ -46, "CL_INVALID_KERNEL_NAME" },			{ -47, "CL_INVALID_KERNEL_DEFINITION" },
			{ -48, "CL_INVALID_KERNEL" },					{ -49, "CL_INVALID_ARG_INDEX" },			{ -50, "CL_INVALID_ARG_VALUE" },
			{ -51, "CL_INVALID_ARG_SIZE" },					{ -52, "CL_INVALID_KERNEL_ARGS" },			{ -53, "CL_INVALID_WORK_DIMENSION" },
			{ -54, "CL_INVALID_WORK_GROUP_SIZE" },			{ -55, "CL_INVALID_WORK_ITEM_SIZE" },		{ -56, "CL_INVALID_GLOBAL_OFFSET" },
			{ -57, "CL_INVALID_EVENT_WAIT_LIST" },			{ -58, "CL_INVALID_EVENT" },				{ -59, "CL_INVALID_OPERATION" },
			{ -60, "CL_INVALID_GL_OBJECT" },				{ -61, "CL_INVALID_BUFFER_SIZE" },			{ -62, "CL_INVALID_MIP_LEVEL" },
			{ -63, "CL_INVALID_GLOBAL_WORK_SIZE" },			{ -64, "CL_INVALID_PROPERTY" },				{ -65, "CL_INVALID_IMAGE_DESCRIPTOR" },
			{ -66, "CL_INVALID_COMPILER_OPTIONS" },			{ -67, "CL_INVALID_LINKER_OPTIONS" },		{ -68, "CL_INVALID_DEVICE_PARTITION_COUNT" }
		};

		os << "switch(error)";
		{
			CodeStream::Scope b(os);

			for (const auto& e : allClErrors) {
				os << "case " << e.first << ": return \"" << e.second << "\";" << std::endl;
			}

			os << "default: return \"Unknown OpenCL error\";" << std::endl;
		}
	}*/
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << "// Helper macro for error-checking OpenCL calls" << std::endl;
	os << "#define CHECK_OPENCL_ERRORS(call) {\\" << std::endl;
	os << "    cl_int error = call;\\" << std::endl;
	os << "    if (error != CL_SUCCESS) {\\" << std::endl;
	//os << "        throw std::runtime_error(__FILE__\": \" + std::to_string(__LINE__) + \": opencl error \" + std::to_string(error) + \": \" + clGetErrorString(error));\\" << std::endl;
	os << "    }\\" << std::endl;
	os << "}" << std::endl;

	// Declaration of OpenCL functions
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << "// OpenCL functions declaration" << std::endl;
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << "namespace opencl";
	{
		CodeStream::Scope b(os);
		os << "void setUpContext(cl::Context& context, cl::Device& device, const int deviceIndex);" << std::endl;
		os << "void createProgram(const char* kernelSource, cl::Program& program, cl::Context& context);" << std::endl;
	}
	os << std::endl;

	// Declaration of OpenCL variables
	os << "extern \"C\" {"<<std::endl;
	os << "// OpenCL variables" << std::endl;
	os << "EXPORT_VAR cl::Context clContext;" << std::endl;
	os << "EXPORT_VAR cl::Device clDevice;" << std::endl;
	os << "EXPORT_VAR cl::CommandQueue commandQueue;" << std::endl;
	os << std::endl;
	os << "// OpenCL programs" << std::endl;
	os << "EXPORT_VAR cl::Program " << ProgramNames[ProgramInitialize] << ";" << std::endl;
	os << "EXPORT_VAR cl::Program " << ProgramNames[ProgramNeuronsUpdate] << ";" << std::endl;
	os << std::endl;
	os << "// OpenCL kernels" << std::endl;
	os << "EXPORT_VAR cl::Kernel " << KernelNames[KernelInitialize] << ";" << std::endl;
	os << "EXPORT_VAR cl::Kernel " << KernelNames[KernelPreNeuronReset] << ";" << std::endl;
	os << "EXPORT_VAR cl::Kernel " << KernelNames[KernelNeuronUpdate] << ";" << std::endl;
	os << "EXPORT_FUNC void initInitializationKernels();" << std::endl;
	os << "EXPORT_FUNC void initUpdateNeuronsKernels();" << std::endl;
	os << "// OpenCL kernels sources" << std::endl;
	os << "EXPORT_VAR const char* " << ProgramNames[ProgramInitialize] << "Src;" << std::endl;
	os << "EXPORT_VAR const char* " << ProgramNames[ProgramNeuronsUpdate] << "Src;" << std::endl;
	os << "} // extern \"C\"" << std::endl;
	os << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genRunnerPreamble(CodeStream& os) const
{
	// Generating OpenCL variables for the runner
	os << "extern \"C\"";
	{
		CodeStream::Scope b(os);
		os << "// OpenCL variables" << std::endl;
		os << "cl::Context clContext;" << std::endl;
		os << "cl::Device clDevice;" << std::endl;
		os << "cl::CommandQueue commandQueue;" << std::endl;
		os << std::endl;
		os << "// OpenCL programs" << std::endl;
		os << "cl::Program " << ProgramNames[ProgramInitialize] << ";" << std::endl;
		os << "cl::Program " << ProgramNames[ProgramNeuronsUpdate] << ";" << std::endl;
		os << std::endl;
		os << "// OpenCL kernels" << std::endl;
		os << "cl::Kernel " << KernelNames[KernelInitialize] << ";" << std::endl;
		os << "cl::Kernel " << KernelNames[KernelPreNeuronReset] << ";" << std::endl;
		os << "cl::Kernel " << KernelNames[KernelNeuronUpdate] << ";" << std::endl;
	}

	os << std::endl;

	// Generating code for initializing OpenCL programs
	os << "// Initializing OpenCL programs so that they can be used to run the kernels" << std::endl;
	os << "void initPrograms()";
	{
		CodeStream::Scope b(os);
		os << "opencl::setUpContext(clContext, clDevice, DEVICE_INDEX);" << std::endl;
		os << "commandQueue = cl::CommandQueue(clContext, clDevice);" << std::endl;
		os << std::endl;
		os << "// Create programs for kernels" << std::endl;
		os << "opencl::createProgram(" << ProgramNames[ProgramInitialize] << "Src, " << ProgramNames[ProgramInitialize] << ", clContext);" << std::endl;
		os << "opencl::createProgram(" << ProgramNames[ProgramNeuronsUpdate] << "Src, " << ProgramNames[ProgramNeuronsUpdate] << ", clContext);" << std::endl;
	}

	os << std::endl;

	// Implementation of OpenCL functions declared in definitionsInternal
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << "// OpenCL functions implementation" << std::endl;
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << std::endl;
	os << "// Initialize context with the given device" << std::endl;
	os << "void opencl::setUpContext(cl::Context& context, cl::Device& device, const int deviceIndex)";
	{
		CodeStream::Scope b(os);
		os << "// Getting all platforms to gather devices from" << std::endl;
		os << "std::vector<cl::Platform> platforms;" << std::endl;
		os << "cl::Platform::get(&platforms); // Gets all the platforms" << std::endl;
		os << std::endl;
		os << "assert(platforms.size() > 0);" << std::endl;
		os << std::endl;
		os << "// Getting all devices and putting them into a single vector" << std::endl;
		os << "std::vector<cl::Device> devices;" << std::endl;
		os << "for (int i = 0; i < platforms.size(); i++)";
		{
			CodeStream::Scope b(os);
			os << "std::vector<cl::Device> platformDevices;" << std::endl;
			os << "platforms[i].getDevices(CL_DEVICE_TYPE_ALL, &platformDevices);" << std::endl;
			os << "devices.insert(devices.end(), platformDevices.begin(), platformDevices.end());" << std::endl;
		}
		os << std::endl;
		os << "assert(devices.size() > 0);" << std::endl;
		os << std::endl;
		os << "// Check if the device exists at the given index" << std::endl;
		os << "if (deviceIndex >= devices.size())";
		{
			CodeStream::Scope b(os);
			os << "assert(deviceIndex >= devices.size());" << std::endl;
			os << "device = devices.front();" << std::endl;
		}
		os << "else";
		{
			CodeStream::Scope b(os);
			os << "device = devices[deviceIndex]; // We will perform our operations using this device" << std::endl;
		}
		os << std::endl;
		os << "context = cl::Context(device);";
		os << std::endl;
	}
	os << std::endl;
	os << "// Create OpenCL program with the specified device" << std::endl;
	os << "void opencl::createProgram(const char* kernelSource, cl::Program& program, cl::Context& context)";
	{
		CodeStream::Scope b(os);
		os << "// Reading the kernel source for execution" << std::endl;
		os << "program = cl::Program(context, kernelSource, true);" << std::endl;
		os << "program.build(\"-cl-std=CL1.2\");" << std::endl;
	}
	os << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genAllocateMemPreamble(CodeStream& os, const ModelSpecInternal& model) const
{
	// Initializing OpenCL programs
	os << "initPrograms();" << std::endl;

}
//--------------------------------------------------------------------------
void Backend::genAllocateMemPostamble(CodeStream& os, const ModelSpecInternal& model) const
{
	// Initializing OpenCL kernels - after buffer initialization
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << "// OpenCL kernels initialization" << std::endl;
	os << "// ------------------------------------------------------------------------" << std::endl;
	os << "initInitializationKernels();" << std::endl;
	os << "initUpdateNeuronsKernels();" << std::endl;

}
//--------------------------------------------------------------------------
void Backend::genStepTimeFinalisePreamble(CodeStream& os, const ModelSpecInternal& model) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genStepTimeFinalisePreamble");
}
//--------------------------------------------------------------------------
void Backend::genVariableDefinition(CodeStream& definitions, CodeStream& definitionsInternal, const std::string& type, const std::string& name, VarLocation loc) const
{
	const bool deviceType = isDeviceType(type);

	if(loc & VarLocation::HOST) {
		if (deviceType) {
			throw std::runtime_error("Variable '" + name + "' is of device-only type '" + type + "' but is located on the host");
		}
		definitions << "EXPORT_VAR " << type << " " << name << ";" << std::endl;
	}
	if(loc & VarLocation::DEVICE) {
		definitionsInternal << "EXPORT_VAR cl::Buffer" << " d_" << name << ";" << std::endl;
	}
}
//--------------------------------------------------------------------------
void Backend::genVariableImplementation(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc) const
{
	if(loc & VarLocation::HOST) {
		os << type << " " << name << ";" << std::endl;
	}
	if(loc & VarLocation::DEVICE) {
		os << "cl::Buffer" << " d_" << name << ";" << std::endl;
	}
}
//--------------------------------------------------------------------------
MemAlloc Backend::genVariableAllocation(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc, size_t count) const
{
	auto allocation = MemAlloc::zero();

	if (loc & VarLocation::HOST) {
		os << name << " = (" << type << "*)malloc(" << count << " * sizeof(" << type << "));" << std::endl;
		allocation += MemAlloc::host(count * getSize(type));
	}

	// If variable is present on device then initialize the device buffer
	if (loc & VarLocation::DEVICE) {
		os << getVarPrefix() << name << " = cl::Buffer(clContext, CL_MEM_READ_WRITE, CL_MEM_COPY_HOST_PTR " << count << " * sizeof (" << type << "), " << name << ");" << std::endl;
		allocation += MemAlloc::device(count * getSize(type));
	}

    return allocation;
}
//--------------------------------------------------------------------------
void Backend::genVariableFree(CodeStream& os, const std::string& name, VarLocation loc) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genVariableFree");
}
//--------------------------------------------------------------------------
void Backend::genExtraGlobalParamDefinition(CodeStream& definitions, const std::string& type, const std::string& name, VarLocation loc) const
{
	if (loc & VarLocation::HOST) {
		definitions << "EXPORT_VAR " << type << " " << name << ";" << std::endl;
	}
	if (loc & VarLocation::DEVICE && ::Utils::isTypePointer(type)) {
		definitions << "EXPORT_VAR cl::Buffer" << " d_" << name << ";" << std::endl;
	}
}
//--------------------------------------------------------------------------
void Backend::genExtraGlobalParamImplementation(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genExtraGlobalParamImplementation");
}
//--------------------------------------------------------------------------
void Backend::genExtraGlobalParamAllocation(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genExtraGlobalParamAllocation");
}
//--------------------------------------------------------------------------
void Backend::genExtraGlobalParamPush(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genExtraGlobalParamPush");
}
//--------------------------------------------------------------------------
void Backend::genExtraGlobalParamPull(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genExtraGlobalParamPull");
}
//--------------------------------------------------------------------------
void Backend::genPopVariableInit(CodeStream& os, VarLocation, const Substitutions& kernelSubs, Handler handler) const
{
	Substitutions varSubs(&kernelSubs);

	// If this is first thread in group
	os << "if(" << varSubs["id"] << " == 0)";
	{
		CodeStream::Scope b(os);
		handler(os, varSubs);
	}
}
//--------------------------------------------------------------------------
void Backend::genVariableInit(CodeStream& os, VarLocation, size_t, const std::string& countVarName,
	const Substitutions& kernelSubs, Handler handler) const
{
	// Variable should already be provided via parallelism
	assert(kernelSubs.hasVarSubstitution(countVarName));

	Substitutions varSubs(&kernelSubs);
	handler(os, varSubs);
}
//--------------------------------------------------------------------------
void Backend::genSynapseVariableRowInit(CodeStream& os, VarLocation, const SynapseGroupInternal& sg,
	const Substitutions& kernelSubs, Handler handler) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genSynapseVariableRowInit");
}
//--------------------------------------------------------------------------
void Backend::genVariablePush(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc, bool autoInitialized, size_t count) const
{
	if (!(loc & VarLocation::ZERO_COPY)) {
		// Only copy if uninitialisedOnly isn't set
		if (autoInitialized) {
			os << "if(!uninitialisedOnly)" << CodeStream::OB(1101);
		}

		os << "CHECK_OPENCL_ERRORS(commandQueue.enqueueWriteBuffer(" << getVarPrefix() << name;
		os << ", " << "CL_TRUE";
		os << ", " << "0";
		os << ", " << count << " * sizeof(" << type << ")";
		os << ", " << name << "));" << std::endl;

		if (autoInitialized) {
			os << CodeStream::CB(1101);
		}
	}
}
//--------------------------------------------------------------------------
void Backend::genVariablePull(CodeStream& os, const std::string& type, const std::string& name, VarLocation loc, size_t count) const
{
	if (!(loc & VarLocation::ZERO_COPY)) {
		os << "CHECK_OPENCL_ERRORS(commandQueue.enqueueReadBuffer(" << getVarPrefix() << name;
		os << ", " << "CL_TRUE";
		os << ", " << "0";
		os << ", " << count << " * sizeof(" << type << ")";
		os << ", " << name << "));" << std::endl;
	}
}
//--------------------------------------------------------------------------
void Backend::genCurrentVariablePush(CodeStream& os, const NeuronGroupInternal& ng, const std::string& type, const std::string& name, VarLocation loc) const
{
	// If this variable requires queuing and isn't zero-copy
	if (ng.isVarQueueRequired(name) && ng.isDelayRequired() && !(loc & VarLocation::ZERO_COPY)) {
		// Generate memcpy to copy only current timestep's data
		//! TO BE IMPLEMENTED
		/*os << "CHECK_CUDA_ERRORS(cudaMemcpy(d_" << name << ng.getName() << " + (spkQuePtr" << ng.getName() << " * " << ng.getNumNeurons() << ")";
		os << ", " << name << ng.getName() << " + (spkQuePtr" << ng.getName() << " * " << ng.getNumNeurons() << ")";
		os << ", " << ng.getNumNeurons() << " * sizeof(" << type << "), cudaMemcpyHostToDevice));" << std::endl;*/
	}
	// Otherwise, generate standard push
	else {
		genVariablePush(os, type, name + ng.getName(), loc, false, ng.getNumNeurons());
	}
}
//--------------------------------------------------------------------------
void Backend::genCurrentVariablePull(CodeStream& os, const NeuronGroupInternal& ng, const std::string& type, const std::string& name, VarLocation loc) const
{
	// If this variable requires queuing and isn't zero-copy
	if (ng.isVarQueueRequired(name) && ng.isDelayRequired() && !(loc & VarLocation::ZERO_COPY)) {
		// Generate memcpy to copy only current timestep's data
		//! TO BE IMPLEMENTED
		/*os << "CHECK_CUDA_ERRORS(cudaMemcpy(d_" << name << ng.getName() << " + (spkQuePtr" << ng.getName() << " * " << ng.getNumNeurons() << ")";
		os << ", " << name << ng.getName() << " + (spkQuePtr" << ng.getName() << " * " << ng.getNumNeurons() << ")";
		os << ", " << ng.getNumNeurons() << " * sizeof(" << type << "), cudaMemcpyHostToDevice));" << std::endl;*/
	}
	// Otherwise, generate standard push
	else {
		genVariablePull(os, type, name + ng.getName(), loc, ng.getNumNeurons());
	}
}
//--------------------------------------------------------------------------
MemAlloc Backend::genGlobalRNG(CodeStream& definitions, CodeStream& definitionsInternal, CodeStream& runner, CodeStream& allocations, CodeStream& free, const ModelSpecInternal&) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genGlobalRNG");
	return MemAlloc::zero();
}
//--------------------------------------------------------------------------
MemAlloc Backend::genPopulationRNG(CodeStream& definitions, CodeStream& definitionsInternal, CodeStream& runner, CodeStream& allocations, CodeStream& free,
	const std::string& name, size_t count) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genPopulationRNG");
	return MemAlloc::zero();
}
//--------------------------------------------------------------------------
void Backend::genTimer(CodeStream&, CodeStream& definitionsInternal, CodeStream& runner, CodeStream& allocations, CodeStream& free,
	CodeStream& stepTimeFinalise, const std::string& name, bool updateInStepTime) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genTimer");
}
//--------------------------------------------------------------------------
void Backend::genMakefilePreamble(std::ostream& os) const
{
	os << "LIBS := " << "-lOpenCL" << std::endl;
	os << "INCL := " << "-I$(OPENCL_PATH)/include" << std::endl;
	os << "LINKFLAGS := " << "-shared" << std::endl;
	os << "CXXFLAGS := " << "-c -fPIC -std=c++11 -MMD -MP" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genMakefileLinkRule(std::ostream& os) const
{
	os << "\t@$(CXX) $(LINKFLAGS) -o $@ $(OBJECTS) $(LIBS)" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genMakefileCompileRule(std::ostream& os) const
{
	os << "%.o: %.cc" << std::endl;
	os << "\t@$(CXX) $(CXXFLAGS) $(INCL) -o $@ $<" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genMSBuildConfigProperties(std::ostream& os) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::genMSBuildConfigProperties");
}
//--------------------------------------------------------------------------
void Backend::genMSBuildImportProps(std::ostream& os) const
{
	// Import OpenCL props file
	os << "\t<ImportGroup Label=\"ExtensionSettings\">" << std::endl;
	// Using props provided by Intel
	os << "\t\t<Import Project=\"$(OPENCL_PATH)\\BuildCustomizations\\IntelOpenCL.props\" />" << std::endl;
	os << "\t</ImportGroup>" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genMSBuildItemDefinitions(std::ostream& os) const
{
	// Add item definition for host compilation
	os << "\t\t<ClCompile>" << std::endl;
	os << "\t\t\t<WarningLevel>Level3</WarningLevel>" << std::endl;
	os << "\t\t\t<Optimization Condition=\"'$(Configuration)'=='Release'\">MaxSpeed</Optimization>" << std::endl;
	os << "\t\t\t<Optimization Condition=\"'$(Configuration)'=='Debug'\">Disabled</Optimization>" << std::endl;
	os << "\t\t\t<FunctionLevelLinking Condition=\"'$(Configuration)'=='Release'\">true</FunctionLevelLinking>" << std::endl;
	os << "\t\t\t<IntrinsicFunctions Condition=\"'$(Configuration)'=='Release'\">true</IntrinsicFunctions>" << std::endl;
	os << "\t\t\t<PreprocessorDefinitions Condition=\"'$(Configuration)'=='Release'\">WIN32;WIN64;NDEBUG;_CONSOLE;BUILDING_GENERATED_CODE;%(PreprocessorDefinitions)</PreprocessorDefinitions>" << std::endl;
	os << "\t\t\t<PreprocessorDefinitions Condition=\"'$(Configuration)'=='Debug'\">WIN32;WIN64;_DEBUG;_CONSOLE;BUILDING_GENERATED_CODE;%(PreprocessorDefinitions)</PreprocessorDefinitions>" << std::endl;
	os << "\t\t\t<AdditionalIncludeDirectories>$(OPENCL_PATH)\\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>" << std::endl;
	os << "\t\t</ClCompile>" << std::endl;

	// Add item definition for linking
	os << "\t\t<Link>" << std::endl;
	os << "\t\t\t<GenerateDebugInformation>true</GenerateDebugInformation>" << std::endl;
	os << "\t\t\t<EnableCOMDATFolding Condition=\"'$(Configuration)'=='Release'\">true</EnableCOMDATFolding>" << std::endl;
	os << "\t\t\t<OptimizeReferences Condition=\"'$(Configuration)'=='Release'\">true</OptimizeReferences>" << std::endl;
	os << "\t\t\t<SubSystem>Console</SubSystem>" << std::endl;
	os << "\t\t\t<AdditionalLibraryDirectories>$(OPENCL_PATH)\\lib\\x64;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>" << std::endl;
	os << "\t\t\t<AdditionalDependencies>OpenCL.lib;kernel32.lib;user32.lib;gdi32.lib;winspool.lib;comdlg32.lib;advapi32.lib;shell32.lib;ole32.lib;oleaut32.lib;uuid.lib;odbc32.lib;odbccp32.lib;%(AdditionalDependencies)</AdditionalDependencies>" << std::endl;
	os << "\t\t</Link>" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genMSBuildCompileModule(const std::string& moduleName, std::ostream& os) const
{
	os << "\t\t<ClCompile Include=\"" << moduleName << ".cc\" />" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::genMSBuildImportTarget(std::ostream& os) const
{
	os << "\t<ImportGroup Label=\"ExtensionTargets\">" << std::endl;
	// Using targets provided by Intel
	os << "\t\t<Import Project=\"$(OPENCL_PATH)\\BuildCustomizations\\IntelOpenCL.targets\" />" << std::endl;
	os << "\t</ImportGroup>" << std::endl;
}
//--------------------------------------------------------------------------
bool Backend::isGlobalRNGRequired(const ModelSpecInternal& model) const
{
	printf("\nTO BE IMPLEMENTED: ~virtual~ CodeGenerator::OpenCL::Backend::isGlobalRNGRequired");
	return false;
}
//--------------------------------------------------------------------------
void Backend::genCurrentSpikePull(CodeStream& os, const NeuronGroupInternal& ng, bool spikeEvent) const
{
	printf("\nTO BE IMPLEMENTED: CodeGenerator::OpenCL::Backend::genCurrentSpikePull");
}
//--------------------------------------------------------------------------
void Backend::genCurrentSpikePush(CodeStream& os, const NeuronGroupInternal& ng, bool spikeEvent) const
{
	printf("\nTO BE IMPLEMENTED: CodeGenerator::OpenCL::Backend::genCurrentSpikePush");
}
//--------------------------------------------------------------------------
void Backend::genEmitSpike(CodeStream& os, const Substitutions& subs, const std::string& suffix) const
{
	os << "const unsigned int spk" << suffix << "Idx = atomic_add(&shSpk" << suffix << "Count, 1);" << std::endl;
	os << "shSpk" << suffix << "[spk" << suffix << "Idx] = " << subs["id"] << ";" << std::endl;
}
//--------------------------------------------------------------------------
void Backend::addDeviceType(const std::string& type, size_t size)
{
	addType(type, size);
	m_DeviceTypes.emplace(type);
}
//--------------------------------------------------------------------------
bool Backend::isDeviceType(const std::string& type) const
{
	// Get underlying type
	const std::string underlyingType = ::Utils::isTypePointer(type) ? ::Utils::getUnderlyingType(type) : type;

	// Return true if it is in device types set
	return (m_DeviceTypes.find(underlyingType) != m_DeviceTypes.cend());
}
} // namespace OpenCL
} // namespace CodeGenerator
