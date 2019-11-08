#include "presynapticUpdateStrategy.h"

// GeNN includes
#include "gennUtils.h"
#include "modelSpecInternal.h"

// GeNN code generator includes
#include "code_generator/codeStream.h"
#include "code_generator/substitutions.h"

// CUDA backend includes
#include "backend.h"
#include "utils.h"

//----------------------------------------------------------------------------
// Anonymous namespace
//----------------------------------------------------------------------------
namespace
{
bool isSmallSharedMemoryPop(const SynapseGroupInternal &sg, const CodeGenerator::CUDA::Backend &backend)
{
    // If device is older than Maxwell, we shouldn't use shared memory as atomics are emulated
    // and actually slower than global memory (see https://devblogs.nvidia.com/gpu-pro-tip-fast-histograms-using-shared-atomics-maxwell/)
    if(backend.getChosenCUDADevice().major < 5) {
        return false;
    }
    // Otherwise, if dendritic delays are required, shared memory approach cannot be used so return false
    else if(sg.isDendriticDelayRequired()) {
        return false;
    }
    // Otherwise, we should accumulate each postsynaptic neuron's input in shared menory if the output
    // population is small enough that input to it can be stored in a shared memory array
    else if(sg.getTrgNeuronGroup()->getNumNeurons() <= backend.getKernelBlockSize(CodeGenerator::CUDA::KernelPresynapticUpdate)) {
        return true;
    }
    else {
        return false;
    }
}
//----------------------------------------------------------------------------
void genSmallSharedMemoryPopPreamble(CodeGenerator::CodeStream &os, const SynapseGroupInternal &sg)
{
    os << "if(threadIdx.x < " << sg.getTrgNeuronGroup()->getNumNeurons() << ")";
    {
        CodeGenerator::CodeStream::Scope b(os);
        os << "shLg[threadIdx.x] = 0;" << std::endl;
    }
    os << "__syncthreads();" << std::endl;
}
//----------------------------------------------------------------------------
void genSmallSharedMemoryPopPostamble(CodeGenerator::CodeStream &os, const ModelSpecInternal &model,
                                      const SynapseGroupInternal &sg, const CodeGenerator::CUDA::Backend &backend)
{
    os << "__syncthreads();" << std::endl;
    os << "if (threadIdx.x < " << sg.getTrgNeuronGroup()->getNumNeurons() << ")";
    {
        CodeGenerator::CodeStream::Scope b(os);
        const std::string inSyn = "dd_inSyn" + sg.getPSModelTargetName() + "[threadIdx.x]";
        if (sg.isPSModelMerged()) {
            os << backend.getFloatAtomicAdd(model.getPrecision()) << "(&" << inSyn << ", shLg[threadIdx.x]);" << std::endl;
        }
        else {
            os << inSyn << " += shLg[threadIdx.x];" << std::endl;
        }
    }
}
}   // Anonymous namespace

//----------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::PreSpan
//----------------------------------------------------------------------------
namespace CodeGenerator
{
namespace CUDA
{
namespace PresynapticUpdateStrategy
{
size_t PreSpan::getNumThreads(const SynapseGroupInternal &sg) const
{
    // Use specified number of threads for each presynaptic neuron
    return sg.getSrcNeuronGroup()->getNumNeurons() * sg.getNumThreadsPerSpike();
}
//----------------------------------------------------------------------------
size_t PreSpan::getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const
{
    return sg.getMaxConnections();
}
//----------------------------------------------------------------------------
bool PreSpan::isCompatible(const SynapseGroupInternal &sg) const
{
    // Presynaptic parallelism can be used when synapse groups request it and they have sparse connectivity
    return (sg.getSpanType() == SynapseGroup::SpanType::PRESYNAPTIC) && (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::SPARSE);
}
//----------------------------------------------------------------------------
size_t PreSpan::getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const
{
    // One element is required per thread if small shared memory optimization should be used for sg
    return isSmallSharedMemoryPop(sg, backend) ? 1 : 0;
}
//----------------------------------------------------------------------------
void PreSpan::genPreamble(CodeStream &os, const ModelSpecInternal &, const SynapseGroupInternal &sg,
                          const Substitutions &, const Backend &backend, size_t) const
{
    if (isSmallSharedMemoryPop(sg, backend)) {
        genSmallSharedMemoryPopPreamble(os, sg);
    }
}
//----------------------------------------------------------------------------
void PreSpan::genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                        const Substitutions &popSubs, const Backend &backend, bool trueSpike, size_t,
                        BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                        BackendBase::SynapseGroupHandler) const
{
    // Get suffix based on type of events
    const std::string eventSuffix = trueSpike ? "" : "Evnt";
    const auto *wu = sg.getWUModel();

    if(sg.getNumThreadsPerSpike() > 1) {
        os << "const unsigned int spike = " << popSubs["id"] << " / " << sg.getNumThreadsPerSpike() << ";" << std::endl;
        os << "const unsigned int thread = " << popSubs["id"] << " % " << sg.getNumThreadsPerSpike() << ";" << std::endl;
    }
    else {
        os << "const unsigned int spike = " << popSubs["id"] << ";" << std::endl;
    }

    os << "if (spike < " ;
    if (sg.getSrcNeuronGroup()->isDelayRequired()) {
        os << "dd_glbSpkCnt" << eventSuffix << sg.getSrcNeuronGroup()->getName() << "[preReadDelaySlot])";
    }
    else {
        os << "dd_glbSpkCnt" << eventSuffix << sg.getSrcNeuronGroup()->getName() << "[0])";
    }
    {
        CodeStream::Scope b(os);

        if (!wu->getSimSupportCode().empty()) {
            os << "using namespace " << sg.getName() << "_weightupdate_simCode;" << std::endl;
        }

        if (sg.getSrcNeuronGroup()->isDelayRequired()) {
            os << "const unsigned int preInd = dd_glbSpk"  << eventSuffix << sg.getSrcNeuronGroup()->getName();
            os << "[(preReadDelaySlot * " << sg.getSrcNeuronGroup()->getNumNeurons() << ") + spike];" << std::endl;
        }
        else {
            os << "const unsigned int preInd = dd_glbSpk"  << eventSuffix << sg.getSrcNeuronGroup()->getName();
            os << "[spike];" << std::endl;
        }

        if(sg.getNumThreadsPerSpike() > 1) {
            os << "unsigned int synAddress = (preInd * " << sg.getMaxConnections() << ") + thread;" << std::endl;
        }
        else {
            os << "unsigned int synAddress = preInd * " << sg.getMaxConnections() << ";" << std::endl;
        }
        os << "const unsigned int npost = dd_rowLength" << sg.getName() << "[preInd];" << std::endl;

        if (!trueSpike && sg.isEventThresholdReTestRequired()) {
            os << "if(";

            Substitutions threshSubs(&popSubs);
            threshSubs.addVarSubstitution("id_pre", "preInd");

            // Generate weight update threshold condition
            wumThreshHandler(os, sg, threshSubs);

            // end code substitutions ----
            os << ")";

            os << CodeStream::OB(130);
        }

        if(sg.getNumThreadsPerSpike() > 1) {
            os << "for(unsigned int i = thread; i < npost; i += " << sg.getNumThreadsPerSpike() << ", synAddress += " << sg.getNumThreadsPerSpike() << ")";
        }
        else {
            os << "for(unsigned int i = 0; i < npost; i++, synAddress++)";
        }
        {
            CodeStream::Scope b(os);

            // **TODO** pretty sure __ldg will boost performance here - basically will bring whole row into cache
            os << "const unsigned int ipost = dd_ind" <<  sg.getName() << "[synAddress];" << std::endl;

            // Create substitution stack for presynaptic simulation code
            Substitutions synSubs(&popSubs);
            synSubs.addVarSubstitution("id_pre", "preInd");
            synSubs.addVarSubstitution("id_post", "ipost");
            synSubs.addVarSubstitution("id_syn", "synAddress");

            // If dendritic delay is required, always use atomic operation to update dendritic delay buffer
            if(sg.isDendriticDelayRequired()) {
                synSubs.addFuncSubstitution("addToInSynDelay", 2, backend.getFloatAtomicAdd(model.getPrecision()) + "(&dd_denDelay" + sg.getPSModelTargetName() + "[" + sg.getDendriticDelayOffset("dd_", "$(1)") + "ipost], $(0))");
            }
            // Otherwise
            else {
                // If postsynaptic input should be accumulated in shared memory, substitute shared memory array for $(inSyn)
                if(isSmallSharedMemoryPop(sg, backend)) {
                    synSubs.addFuncSubstitution("addToInSyn", 1, backend.getFloatAtomicAdd(model.getPrecision()) + "(&shLg[ipost], $(0))");
                }
                // Otherwise, substitute global memory array for $(inSyn)
                else {
                    synSubs.addFuncSubstitution("addToInSyn", 1, backend.getFloatAtomicAdd(model.getPrecision()) + "(&dd_inSyn" + sg.getPSModelTargetName() + "[ipost], $(0))");
                }
            }

            wumSimHandler(os, sg, synSubs);
        }

        if (!trueSpike && sg.isEventThresholdReTestRequired()) {
            os << CodeStream::CB(130);
        }
    }
}
//----------------------------------------------------------------------------
void PreSpan::genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                           const Substitutions &, const Backend &backend, size_t) const
{
    if (isSmallSharedMemoryPop(sg, backend)) {
        genSmallSharedMemoryPopPostamble(os, model, sg, backend);
    }
}

//----------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::PostSpan
//----------------------------------------------------------------------------
size_t PostSpan::getNumThreads(const SynapseGroupInternal &sg) const
{
    return sg.getMaxConnections();
}
//----------------------------------------------------------------------------
size_t PostSpan::getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const
{
    return sg.getMaxConnections();
}
//----------------------------------------------------------------------------
bool PostSpan::isCompatible(const SynapseGroupInternal &sg) const
{
    // Postsynatic parallelism can be used when synapse groups request it
    return (sg.getSpanType() == SynapseGroup::SpanType::POSTSYNAPTIC) && !(sg.getMatrixConnectivity() == SynapseMatrixConnectivity::PROCEDURAL);
}
//----------------------------------------------------------------------------
void PostSpan::genPreamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                           const Substitutions &, const Backend &backend, size_t) const
{
    // If data structure is dense, we can accumulate output directly into register
    if (shouldAccumulateInRegister(sg)) {
        os << model.getPrecision() << " linSyn = 0;" << std::endl;
    }
    else if(isSmallSharedMemoryPop(sg, backend)) {
        genSmallSharedMemoryPopPreamble(os, sg);
    }
}
//----------------------------------------------------------------------------
size_t PostSpan::getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const
{
    // One element is required per thread if small shared memory optimization should be used for sg
    return isSmallSharedMemoryPop(sg, backend) ? 1 : 0;
}
//----------------------------------------------------------------------------
void PostSpan::genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, 
                         const Substitutions &popSubs, const Backend &backend, bool trueSpike, size_t,
                         BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                         BackendBase::SynapseGroupHandler) const
{
    // Get suffix based on type of events
    const std::string eventSuffix = trueSpike ? "" : "Evnt";

    os << "const unsigned int numSpikes = dd_glbSpkCnt" << eventSuffix << sg.getSrcNeuronGroup()->getName();
    if (sg.getSrcNeuronGroup()->isDelayRequired()) {
        os << "[preReadDelaySlot];" << std::endl;
    }
    else {
        os << "[0];" << std::endl;
    }
    os << "const unsigned int numSpikeBlocks = (numSpikes + " << backend.getKernelBlockSize(KernelPresynapticUpdate) << " - 1) / " << backend.getKernelBlockSize(KernelPresynapticUpdate) << ";" << std::endl;


    const auto *wu = sg.getWUModel();
    os << "for (unsigned int r = 0; r < numSpikeBlocks; r++)";
    {
        CodeStream::Scope b(os);
        os << "const unsigned int numSpikesInBlock = (r == numSpikeBlocks - 1) ? ((numSpikes - 1) % " << backend.getKernelBlockSize(KernelPresynapticUpdate) << ") + 1 : " << backend.getKernelBlockSize(KernelPresynapticUpdate) << ";" << std::endl;

        os << "__syncthreads();" << std::endl;
        os << "if (threadIdx.x < numSpikesInBlock)";
        {
            CodeStream::Scope b(os);
            const std::string queueOffset = sg.getSrcNeuronGroup()->isDelayRequired() ? "preReadDelayOffset + " : "";
            os << "const unsigned int spk = dd_glbSpk" << eventSuffix << sg.getSrcNeuronGroup()->getName() << "[" << queueOffset << "(r * " << backend.getKernelBlockSize(KernelPresynapticUpdate) << ") + threadIdx.x];" << std::endl;
            os << "shSpk" << eventSuffix << "[threadIdx.x] = spk;" << std::endl;
            if(sg.getMatrixConnectivity() == SynapseMatrixConnectivity::SPARSE) {
                os << "shRowLength[threadIdx.x] = dd_rowLength" << sg.getName() << "[spk];" << std::endl;
            }
        }
        os << "__syncthreads();" << std::endl;

        os << "// loop through all incoming spikes" << std::endl;
        os << "for (unsigned int j = 0; j < numSpikesInBlock; j++)";
        {
            CodeStream::Scope b(os);
            os << "// only work on existing neurons" << std::endl;
            os << "if (" << popSubs["id"] << " < " << sg.getMaxConnections() << ")";
            {
                CodeStream::Scope b(os);
                if (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::BITMASK) {
                    const size_t maxSynapses = (size_t)sg.getTrgNeuronGroup()->getNumNeurons() * (size_t)sg.getSrcNeuronGroup()->getNumNeurons();
                    if((maxSynapses & 0xFFFFFFFF00000000ULL) != 0) {
                        os << "const uint64_t gid = (shSpk" << eventSuffix << "[j] * " << sg.getTrgNeuronGroup()->getNumNeurons() << "ull + " << popSubs["id"] << ");" << std::endl;
                    }
                    else {
                        os << "const unsigned int gid = (shSpk" << eventSuffix << "[j] * " << sg.getTrgNeuronGroup()->getNumNeurons() << " + " << popSubs["id"] << ");" << std::endl;
                    }
                }

                if (!wu->getSimSupportCode().empty()) {
                    os << "using namespace " << sg.getName() << "_weightupdate_simCode;" << std::endl;
                }
                if (!trueSpike && sg.isEventThresholdReTestRequired()) {
                    os << "if(";
                    if (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::BITMASK) {
                        // Note: we will just access global mem. For compute >= 1.2 simultaneous access to same global mem in the (half-)warp will be coalesced - no worries
                        os << "(B(dd_gp" << sg.getName() << "[gid / 32], gid & 31)) && ";
                    }

                    Substitutions threshSubs(&popSubs);
                    threshSubs.addVarSubstitution("id_pre", "shSpk" + eventSuffix + "[j]");

                    // Generate weight update threshold condition
                    wumThreshHandler(os, sg, threshSubs);

                    // end code substitutions ----
                    os << ")";
                    os << CodeStream::OB(130);
                }
                else if (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::BITMASK) {
                    os << "if (B(dd_gp" << sg.getName() << "[gid / 32], gid & 31))" << CodeStream::OB(135);
                }

                Substitutions synSubs(&popSubs);
                synSubs.addVarSubstitution("id_pre", "shSpk" + eventSuffix + "[j]");
                if(sg.getMatrixConnectivity() == SynapseMatrixConnectivity::SPARSE) {
                    os << "unsigned int synAddress = shSpk" << eventSuffix << "[j] * " << sg.getMaxConnections() << ";" << std::endl;
                    os << "const unsigned int npost = shRowLength[j];" << std::endl;

                    os << "if (" << popSubs["id"] << " < npost)" << CodeStream::OB(140);
                    os << "synAddress += " << popSubs["id"] << ";" << std::endl;
                    os << "const unsigned int ipost = dd_ind" << sg.getName() << "[synAddress];" << std::endl;

                    synSubs.addVarSubstitution("id_post", "ipost");
                }
                else { // DENSE
                    os << "unsigned int synAddress = (shSpk" << eventSuffix << "[j] * " << sg.getTrgNeuronGroup()->getNumNeurons() << ") + " + popSubs["id"] + ";" << std::endl;

                    synSubs.addVarSubstitution("id_post", popSubs["id"]);
                }
                synSubs.addVarSubstitution("id_syn", "synAddress");

                // If dendritic delay is required, always use atomic operation to update dendritic delay buffer
                if(sg.isDendriticDelayRequired()) {
                    synSubs.addFuncSubstitution("addToInSynDelay", 2, backend.getFloatAtomicAdd(model.getPrecision()) + "(&dd_denDelay" + sg.getPSModelTargetName() + "[" + sg.getDendriticDelayOffset("dd_", "$(1)") + synSubs["id_post"] + "], $(0))");
                }
                // Otherwise
                else {
                    // If we should accumulate in register, add parameter to register
                    if(shouldAccumulateInRegister(sg)) {
                        synSubs.addFuncSubstitution("addToInSyn", 1, "linSyn += $(0)");
                    }
                    // Otherwise, if we should use shared memory, add to shared memory
                    // **THINK** this is only correct if there are no multapses i.e. there is only one synapse between any pair of pre and postsynaptic neurons
                    else if(isSmallSharedMemoryPop(sg, backend)) {
                        synSubs.addFuncSubstitution("addToInSyn", 1, "shLg[" + synSubs["id_post"] + "] += $(0)");
                    }
                    // Otherwise, use global memory atomic
                    else {
                        synSubs.addFuncSubstitution("addToInSyn", 1, backend.getFloatAtomicAdd(model.getPrecision()) + "(&dd_inSyn" + sg.getPSModelTargetName() + "[" + synSubs["id_post"] + "], $(0))");
                    }
                }

                wumSimHandler(os, sg, synSubs);

                if (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::SPARSE) {
                    os << CodeStream::CB(140); // end if (id < npost)
                }

                if (!trueSpike && sg.isEventThresholdReTestRequired()) {
                    os << CodeStream::CB(130); // end if (eCode)
                }
                else if (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::BITMASK) {
                    os << CodeStream::CB(135); // end if (B(dd_gp" << sg.getName() << "[gid / 32], gid
                }
            }
        }
    }
}
//----------------------------------------------------------------------------
void PostSpan::genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                            const Substitutions &popSubs, const Backend &backend, size_t) const
{
    // If we should accumulate output directly into register
    if (shouldAccumulateInRegister(sg)) {
        os << "// only do this for existing neurons" << std::endl;
        os << "if (" << popSubs["id"] << " < " << sg.getTrgNeuronGroup()->getNumNeurons() << ")";
        {
            CodeStream::Scope b(os);
            const std::string inSyn = "dd_inSyn" + sg.getPSModelTargetName() + "[" + popSubs["id"] + "]";
            if (sg.isPSModelMerged()) {
                os << backend.getFloatAtomicAdd(model.getPrecision()) << "(&" << inSyn << ", linSyn);" << std::endl;
            }
            else {
                os << inSyn << " += linSyn;" << std::endl;
            }
        }
    }
    // Otherwise, if we should accumulate into shared memory
    else if (isSmallSharedMemoryPop(sg, backend)) {
        genSmallSharedMemoryPopPostamble(os, model, sg, backend);
    }
}
//----------------------------------------------------------------------------
bool PostSpan::shouldAccumulateInRegister(const SynapseGroupInternal &sg) const
{
    // If no dendritic delays are required and data structure is dense, we can accumulate output directly into register
    return (!sg.isDendriticDelayRequired()
            && ((sg.getMatrixConnectivity() == SynapseMatrixConnectivity::DENSE) || (sg.getMatrixConnectivity() == SynapseMatrixConnectivity::BITMASK)));
}

//--------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::PreSpanProcedural
//--------------------------------------------------------------------------
size_t PreSpanProcedural::getNumThreads(const SynapseGroupInternal &sg) const
{
    // Use specified number of threads for each presynaptic neuron
    return sg.getSrcNeuronGroup()->getNumNeurons() * sg.getNumThreadsPerSpike();
}
//----------------------------------------------------------------------------
size_t PreSpanProcedural::getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const
{
    return sg.getMaxConnections();
}
//----------------------------------------------------------------------------
bool PreSpanProcedural::isCompatible(const SynapseGroupInternal &sg) const
{
    // Presynaptic procedural parallelism can be used when synapse groups have procedural connectivity
    if(sg.getMatrixConnectivity() == SynapseMatrixConnectivity::PROCEDURAL) {
        // If all variables are implemented as global, return true
        if(std::all_of(sg.getWUVarImplementation().cbegin(), sg.getWUVarImplementation().cend(),
                       [](VarImplementation v){ return (v == VarImplementation::GLOBAL) || (v == VarImplementation::PROCEDURAL); }))
        {
            return true;
        }
    }

    return false;
}
//----------------------------------------------------------------------------
size_t PreSpanProcedural::getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const
{
    // One element is required per thread if small shared memory optimization should be used for sg
    return isSmallSharedMemoryPop(sg, backend) ? 1 : 0;
}
//----------------------------------------------------------------------------
void PreSpanProcedural::genPreamble(CodeStream &os, const ModelSpecInternal &, const SynapseGroupInternal &sg,
                          const Substitutions &, const Backend &backend, size_t) const
{
    if (isSmallSharedMemoryPop(sg, backend)) {
        genSmallSharedMemoryPopPreamble(os, sg);
    }
}
//----------------------------------------------------------------------------
void PreSpanProcedural::genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                                  const Substitutions &popSubs, const Backend &backend, bool trueSpike, size_t idStart,
                                  BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                                  BackendBase::SynapseGroupHandler wumProceduralConnectHandler) const
{
    // Get suffix based on type of events
    const std::string eventSuffix = trueSpike ? "" : "Evnt";
    const auto *wu = sg.getWUModel();

    const unsigned int numSrcNeurons = sg.getSrcNeuronGroup()->getNumNeurons();
    const unsigned int numTrgNeurons = sg.getTrgNeuronGroup()->getNumNeurons();

    if(sg.getNumThreadsPerSpike() > 1) {
        os << "const unsigned int spike = " << popSubs["id"] << " / " << sg.getNumThreadsPerSpike() << ";" << std::endl;
        os << "const unsigned int thread = " << popSubs["id"] << " % " << sg.getNumThreadsPerSpike() << ";" << std::endl;
    }
    else {
        os << "const unsigned int spike = " << popSubs["id"] << ";" << std::endl;
    }

    // If there is a spike for this thread to process
    os << "if (spike < " ;
    if (sg.getSrcNeuronGroup()->isDelayRequired()) {
        os << "dd_glbSpkCnt" << eventSuffix << sg.getSrcNeuronGroup()->getName() << "[preReadDelaySlot])";
    }
    else {
        os << "dd_glbSpkCnt" << eventSuffix << sg.getSrcNeuronGroup()->getName() << "[0])";
    }
    {
        CodeStream::Scope b(os);

        // Determine the index of the presynaptic neuron this thread is responsible for
        if (sg.getSrcNeuronGroup()->isDelayRequired()) {
            os << "const unsigned int preInd = dd_glbSpk"  << eventSuffix << sg.getSrcNeuronGroup()->getName();
            os << "[(preReadDelaySlot * " << numSrcNeurons << ") + spike];" << std::endl;
        }
        else {
            os << "const unsigned int preInd = dd_glbSpk"  << eventSuffix << sg.getSrcNeuronGroup()->getName();
            os << "[spike];" << std::endl;
        }

        // Add presynaptic index to substitution stack
        Substitutions procPopSubs(&popSubs);
        procPopSubs.addVarSubstitution("id_pre", "preInd");

        // If this connectivity requires an RNG for initialisation,
        // make copy of connect Phillox RNG and skip ahead to id that would have been used to initialize any variables associated with it
        // **TODO** probably skip over ids previously used for initialization
        if(::Utils::isRNGRequired(sg.getConnectivityInitialiser().getSnippet()->getRowBuildCode())) {
            os << "curandStatePhilox4_32_10_t connectRNG = dd_rng[0];" << std::endl;
            if(sg.getNumThreadsPerSpike() > 1) {
                os << "skipahead_sequence((unsigned long long)((preInd * " << sg.getNumThreadsPerSpike() << ") + thread+ " << idStart << "), &connectRNG);" << std::endl;
            }
            else {
                os << "skipahead_sequence((unsigned long long)(preInd + " << idStart << "), &connectRNG);" << std::endl;
            }

            // Add substitution for RNGwumProceduralConnectHandler
            procPopSubs.addVarSubstitution("rng", "&connectRNG");
        }

        if (!wu->getSimSupportCode().empty()) {
            os << "using namespace " << sg.getName() << "_weightupdate_simCode;" << std::endl;
        }


        if (!trueSpike && sg.isEventThresholdReTestRequired()) {
            os << "if(";

            // Generate weight update threshold condition
            Substitutions threshSubs(&procPopSubs);
            wumThreshHandler(os, sg, threshSubs);

            // end code substitutions ----
            os << ")";

            os << CodeStream::OB(130);
        }

        // Create substitution stack for generating presynaptic simulation code
        Substitutions synSubs(&procPopSubs);

        // Replace $(id_post) with first 'function' parameter as simulation code is
        // going to be, in turn, substituted into procedural connectivity generation code
        synSubs.addVarSubstitution("id_post", "$(0)");

        // Create second substitution stack for generating procedural connectivity code
        Substitutions connSubs(&procPopSubs);

        // If we are using more than one thread to process each row
        if(sg.getNumThreadsPerSpike() > 1) {
            // Calculate how long the sub-row to process on each thread is
            const unsigned int numPostPerThread = Utils::ceilDivide(numTrgNeurons,
                                                                    sg.getNumThreadsPerSpike());
            os << "const unsigned int idPostStart = thread * " << numPostPerThread << ";" << std::endl;

            // If number of post neurons per thread directly divides total number of postsynaptic neurons
            if ((numTrgNeurons % numPostPerThread) == 0) {
                connSubs.addVarSubstitution("num_post", std::to_string(numPostPerThread));
            }
            // Otherwise clamp
            else {
                os << "const unsigned int numPost = (thread == " << (sg.getNumThreadsPerSpike() - 1) << ") ? " << (numTrgNeurons % numPostPerThread) << " : " << numPostPerThread << ";" << std::endl;
                connSubs.addVarSubstitution("num_post", "numPost");
            }

            connSubs.addVarSubstitution("id_post_begin", "idPostStart");
        }
        else {
            connSubs.addVarSubstitution("id_post_begin", "0");
            connSubs.addVarSubstitution("num_post", std::to_string(numTrgNeurons));
        }

        synSubs.addVarSubstitution("id_syn", "DEADBEEF");

        // If dendritic delay is required, always use atomic operation to update dendritic delay buffer
        if(sg.isDendriticDelayRequired()) {
            synSubs.addFuncSubstitution("addToInSynDelay", 2, backend.getFloatAtomicAdd(model.getPrecision()) + "(&dd_denDelay" + sg.getPSModelTargetName() + "[" + sg.getDendriticDelayOffset("dd_", "$(1)") + "$(id_post)], $(0))");
        }
        // Otherwise
        else {
            // If postsynaptic input should be accumulated in shared memory, substitute shared memory array for $(inSyn)
            if(isSmallSharedMemoryPop(sg, backend)) {
                synSubs.addFuncSubstitution("addToInSyn", 1, backend.getFloatAtomicAdd(model.getPrecision()) + "(&shLg[$(id_post)], $(0))");
            }
            // Otherwise, substitute global memory array for $(inSyn)
            else {
                synSubs.addFuncSubstitution("addToInSyn", 1, backend.getFloatAtomicAdd(model.getPrecision()) + "(&dd_inSyn" + sg.getPSModelTargetName() + "[$(id_post)], $(0))");
            }
        }

        // Generate presynaptic simulation code into new stream
        std::ostringstream presynapticUpdateStream;
        CodeStream presynapticUpdate(presynapticUpdateStream);
        wumSimHandler(presynapticUpdate, sg, synSubs);

        // When a synapse should be 'added', substitute in presynaptic update code
        connSubs.addFuncSubstitution("addSynapse", 1, presynapticUpdateStream.str());

        // Generate procedural connectivity code
        wumProceduralConnectHandler(os, sg, connSubs);

        if (!trueSpike && sg.isEventThresholdReTestRequired()) {
            os << CodeStream::CB(130);
        }
    }
}
//----------------------------------------------------------------------------
void PreSpanProcedural::genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                                     const Substitutions &, const Backend &backend, size_t) const
{
    if (isSmallSharedMemoryPop(sg, backend)) {
        genSmallSharedMemoryPopPostamble(os, model, sg, backend);
    }
}
}   // namespace PresynapticUpdateStrategy
}   // namespace CUDA
}   // namespace CodeGenerator
