#pragma once

// GeNN code generator includes
#include "code_generator/backendBase.h"

// Forward declarations
class ModelSpecInternal;
class SynapseGroupInternal;

namespace CodeGenerator
{
namespace CUDA
{
class Backend;
}
}

//--------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::Base
//--------------------------------------------------------------------------
namespace CodeGenerator
{
namespace CUDA
{
namespace PresynapticUpdateStrategy
{
class Base
{
public:
    //------------------------------------------------------------------------
    // Declared virtuals
    //------------------------------------------------------------------------
    //! Get the number of threads that presynaptic updates should be parallelised across
    virtual size_t getNumThreads(const SynapseGroupInternal &sg) const = 0;

    //! Gets the stride used to access synaptic matrix rows, taking into account sparse data structure, padding etc
    virtual size_t getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const = 0;

    //! Is this presynaptic update strategy compatible with a given synapse group?
    virtual bool isCompatible(const SynapseGroupInternal &sg) const = 0;

    //! How many neurons does each thread accumulate the outputs of into shared memory
    virtual size_t getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const = 0;

    virtual void genPreamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const = 0;

    //! Generate presynaptic update code
    virtual void genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, 
                           const Substitutions &popSubs, const Backend &backend, bool trueSpike,
                           BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                           BackendBase::SynapseGroupHandler wumProceduralConnectHandler) const = 0;

    virtual void genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const = 0;
};

//--------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::PreSpan
//--------------------------------------------------------------------------
//! Presynaptic parallelism
class PreSpan : public Base
{
public:
    //------------------------------------------------------------------------
    // PresynapticUpdateStrategy::Base virtuals
    //------------------------------------------------------------------------
    //! Get the number of threads that presynaptic updates should be parallelised across
    virtual size_t getNumThreads(const SynapseGroupInternal &sg) const override;

    //! Gets the stride used to access synaptic matrix rows, taking into account sparse data structure, padding etc
    virtual size_t getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const override;

    //! Is this presynaptic update strategy compatible with a given synapse group?
    virtual bool isCompatible(const SynapseGroupInternal &sg) const override;

    //! How many neurons does each thread accumulate the outputs of into shared memory
    virtual size_t getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const override;

    virtual void genPreamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const override;

    //! Generate presynaptic update code
    virtual void genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                           const Substitutions &popSubs, const Backend &backend, bool trueSpike,
                           BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                           BackendBase::SynapseGroupHandler wumProceduralConnectHandler) const override;

    virtual void genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const override;
};

//--------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::PostSpan
//--------------------------------------------------------------------------
//! Postsynaptic parallelism
class PostSpan : public Base
{
public:
    //------------------------------------------------------------------------
    // PresynapticUpdateStrategy::Base virtuals
    //------------------------------------------------------------------------
    //! Get the number of threads that presynaptic updates should be parallelised across
    virtual size_t getNumThreads(const SynapseGroupInternal &sg) const override;

    //! Gets the stride used to access synaptic matrix rows, taking into account sparse data structure, padding etc
    virtual size_t getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const override;

    //! Is this presynaptic update strategy compatible with a given synapse group?
    virtual bool isCompatible(const SynapseGroupInternal &sg) const override;

    //! How many neurons does each thread accumulate the outputs of into shared memory
    virtual size_t getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const override;

    virtual void genPreamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const override;

    //! Generate presynaptic update code
    virtual void genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                           const Substitutions &popSubs, const Backend &backend, bool trueSpike,
                           BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                           BackendBase::SynapseGroupHandler wumProceduralConnectHandler) const override;

    virtual void genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const override;
};

//--------------------------------------------------------------------------
// CodeGenerator::CUDA::PresynapticUpdateStrategy::PreSpanProcedural
//--------------------------------------------------------------------------
//! Presynaptic parallelism with procedural connectivity
class PreSpanProcedural : public Base
{
public:
    //------------------------------------------------------------------------
    // PresynapticUpdateStrategy::Base virtuals
    //------------------------------------------------------------------------
    //! Get the number of threads that presynaptic updates should be parallelised across
    virtual size_t getNumThreads(const SynapseGroupInternal &sg) const override;

    //! Gets the stride used to access synaptic matrix rows, taking into account sparse data structure, padding etc
    virtual size_t getSynapticMatrixRowStride(const SynapseGroupInternal &sg) const override;

    //! Is this presynaptic update strategy compatible with a given synapse group?
    virtual bool isCompatible(const SynapseGroupInternal &sg) const override;

    //! How many neurons does each thread accumulate the outputs of into shared memory
    virtual size_t getSharedMemoryPerThread(const SynapseGroupInternal &sg, const Backend &backend) const override;

    virtual void genPreamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const override;

    //! Generate presynaptic update code
    virtual void genUpdate(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg,
                           const Substitutions &popSubs, const Backend &backend, bool trueSpike,
                           BackendBase::SynapseGroupHandler wumThreshHandler, BackendBase::SynapseGroupHandler wumSimHandler,
                           BackendBase::SynapseGroupHandler wumProceduralConnectHandler) const override;

    virtual void genPostamble(CodeStream &os, const ModelSpecInternal &model, const SynapseGroupInternal &sg, const Backend &backend) const override;
};

}   // namespace PresynapticUpdateStrategy
}   // namespace CUDA
}   // namespace CodeGenerator
