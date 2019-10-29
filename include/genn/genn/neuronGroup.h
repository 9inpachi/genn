#pragma once

// Standard includes
#include <map>
#include <set>
#include <string>
#include <vector>

// GeNN includes
#include "gennExport.h"
#include "neuronModels.h"
#include "variableImplementation.h"
#include "variableMode.h"

// Forward declarations
class CurrentSourceInternal;
class SynapseGroupInternal;

//------------------------------------------------------------------------
// NeuronGroup
//------------------------------------------------------------------------
class GENN_EXPORT NeuronGroup
{
public:
    NeuronGroup(const NeuronGroup&) = delete;
    NeuronGroup() = delete;

    //------------------------------------------------------------------------
    // Public methods
    //------------------------------------------------------------------------
    //! Set location of this neuron group's output spikes
    /*! This is ignored for simulations on hardware with a single memory space */
    void setSpikeLocation(VarLocation loc) { m_SpikeLocation = loc; }

     //! Set location of this neuron group's output spike events
     /*! This is ignored for simulations on hardware with a single memory space */
    void setSpikeEventLocation(VarLocation loc) { m_SpikeEventLocation = loc; }

    //! Set location of this neuron group's output spike times
    /*! This is ignored for simulations on hardware with a single memory space */
    void setSpikeTimeLocation(VarLocation loc) { m_SpikeTimeLocation = loc; }

    //! Set variable location of neuron model state variable
    /*! This is ignored for simulations on hardware with a single memory space */
    void setVarLocation(const std::string &varName, VarLocation loc);

    //! Set variable implentation of neuron model state variable
    void setVarImplementation(const std::string &varName, VarImplementation impl);

    //! Set location of neuron model extra global parameter
    /*! This is ignored for simulations on hardware with a single memory space
        and only applies to extra global parameters which are pointers. */
    void setExtraGlobalParamLocation(const std::string &paramName, VarLocation loc);

    //------------------------------------------------------------------------
    // Public const methods
    //------------------------------------------------------------------------
    const std::string &getName() const{ return m_Name; }

    //! Gets number of neurons in group
    unsigned int getNumNeurons() const{ return m_NumNeurons; }

    //! Gets the neuron model used by this group
    const NeuronModels::Base *getNeuronModel() const{ return m_NeuronModel; }

    const std::vector<Models::VarInit> &getVarInitialisers() const{ return m_VarInitialisers; }
    const std::vector<VarImplementation> &getVarImplementation() const{ return m_VarImplementation; }

    int getClusterHostID() const{ return m_HostID; }

    bool isSpikeTimeRequired() const;
    bool isTrueSpikeRequired() const;
    bool isSpikeEventRequired() const;

    unsigned int getNumDelaySlots() const{ return m_NumDelaySlots; }
    bool isDelayRequired() const{ return (m_NumDelaySlots > 1); }
    bool isZeroCopyEnabled() const;

    //! Get location of this neuron group's output spikes
    VarLocation getSpikeLocation() const{ return m_SpikeLocation; }

    //! Get location of this neuron group's output spike events
    VarLocation getSpikeEventLocation() const{ return m_SpikeEventLocation; }

    //! Get location of this neuron group's output spike times
    VarLocation getSpikeTimeLocation() const{ return m_SpikeTimeLocation; }

    //! Get location of neuron model state variable by name
    VarLocation getVarLocation(const std::string &varName) const;

    //! Get location of neuron model state variable by index
    VarLocation getVarLocation(size_t index) const{ return m_VarLocation.at(index); }

    //! Get implementation of neuron model state variable by name
    VarImplementation getVarImplementation(const std::string &varName) const;

    //! Get implementation of neuron model state variable by index
    VarImplementation getVarImplementation(size_t index) const{ return m_VarImplementation.at(index); }

    //! Get location of neuron model extra global parameter by name
    /*! This is only used by extra global parameters which are pointers*/
    VarLocation getExtraGlobalParamLocation(const std::string &paramName) const;

    //! Get location of neuron model extra global parameter by omdex
    /*! This is only used by extra global parameters which are pointers*/
    VarLocation getExtraGlobalParamLocation(size_t index) const{ return m_ExtraGlobalParamLocation.at(index); }

    //! Does this neuron group require an RNG to simulate?
    bool isSimRNGRequired() const;

    //! Does this neuron group require an RNG for it's init code?
    bool isInitRNGRequired() const;

    //! Does this neuron group have outgoing connections specified host id?
    bool hasOutputToHost(int targetHostID) const;


protected:
    NeuronGroup(const std::string &name, int numNeurons, const NeuronModels::Base *neuronModel,
                const std::vector<double> &params, const std::vector<Models::VarInit> &varInitialisers,
                VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation, int hostID);

     NeuronGroup(const std::string &name, int numNeurons, const NeuronModels::Base *neuronModel,
                const std::vector<Models::VarInit> &varInitialisers,
                VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation, int hostID);

    //------------------------------------------------------------------------
    // Protected methods
    //------------------------------------------------------------------------
    //! Checks delay slots currently provided by the neuron group against a required delay and extends if required
    void checkNumDelaySlots(unsigned int requiredDelay);

    //! Update which presynaptic variables require queues based on piece of code
    void updatePreVarQueues(const std::string &code);

    //! Update which postsynaptic variables  require queues based on piece of code
    void updatePostVarQueues(const std::string &code);

    void addSpkEventCondition(const std::string &code, const std::string &supportCodeNamespace);

    void addInSyn(SynapseGroupInternal *synapseGroup){ m_InSyn.push_back(synapseGroup); }
    void addOutSyn(SynapseGroupInternal *synapseGroup){ m_OutSyn.push_back(synapseGroup); }

    void initDerivedParams(double dt);

    //! Merge incoming postsynaptic models
    void mergeIncomingPSM(bool merge);

    //! add input current source
    void injectCurrent(CurrentSourceInternal *source);

    //------------------------------------------------------------------------
    // Protected const methods
    //------------------------------------------------------------------------
    //! Gets pointers to all synapse groups which provide input to this neuron group
    const std::vector<SynapseGroupInternal*> &getInSyn() const{ return m_InSyn; }
    const std::vector<std::pair<SynapseGroupInternal*, std::vector<SynapseGroupInternal*>>> &getMergedInSyn() const{ return m_MergedInSyn; }

    //! Gets pointers to all synapse groups emanating from this neuron group
    const std::vector<SynapseGroupInternal*> &getOutSyn() const{ return m_OutSyn; }

    //! Gets pointers to all current sources which provide input to this neuron group
    const std::vector<CurrentSourceInternal*> &getCurrentSources() const { return m_CurrentSources; }

    const std::vector<double> &getDerivedParams() const{ return m_DerivedParams; }

    const std::set<std::pair<std::string, std::string>> &getSpikeEventCondition() const{ return m_SpikeEventCondition; }

    //! Do any of the spike event conditions tested by this neuron require specified parameter?
    bool isParamRequiredBySpikeEventCondition(const std::string &pnamefull) const;

    //! Get the expression to calculate the queue offset for accessing state of variables this timestep
    std::string getCurrentQueueOffset(const std::string &devPrefix) const;

    //! Get the expression to calculate the queue offset for accessing state of variables in previous timestep
    std::string getPrevQueueOffset(const std::string &devPrefix) const;

    bool isVarQueueRequired(const std::string &var) const;
    bool isVarQueueRequired(size_t index) const{ return m_VarQueueRequired[index]; }

private:
    //------------------------------------------------------------------------
    // Private methods
    //------------------------------------------------------------------------
    //! Update which variables require queues based on piece of code
    void updateVarQueues(const std::string &code, const std::string &suffix);

    //------------------------------------------------------------------------
    // Members
    //------------------------------------------------------------------------
    const std::string m_Name;

    const unsigned int m_NumNeurons;

    const NeuronModels::Base *m_NeuronModel;
    std::vector<double> m_DerivedParams;
    std::vector<Models::VarInit> m_VarInitialisers;
    std::vector<SynapseGroupInternal*> m_InSyn;
    std::vector<SynapseGroupInternal*> m_OutSyn;
    std::vector<std::pair<SynapseGroupInternal*, std::vector<SynapseGroupInternal*>>> m_MergedInSyn;
    std::set<std::pair<std::string, std::string>> m_SpikeEventCondition;
    unsigned int m_NumDelaySlots;
    std::vector<CurrentSourceInternal*> m_CurrentSources;

    //! Vector specifying which variables require queues
    std::vector<bool> m_VarQueueRequired;

    //! Whether spikes from neuron group should use zero-copied memory
    VarLocation m_SpikeLocation;

    //! Whether spike-like events from neuron group should use zero-copied memory
    VarLocation m_SpikeEventLocation;

    //! Whether spike times from neuron group should use zero-copied memory
    VarLocation m_SpikeTimeLocation;

    //! Location of individual state variables
    std::vector<VarLocation> m_VarLocation;

    //! How should variables be implemented
    std::vector<VarImplementation> m_VarImplementation;

    //! Location of extra global parameters
    std::vector<VarLocation> m_ExtraGlobalParamLocation;

    //! The ID of the cluster node which the neuron groups are computed on
    const int m_HostID;
};
