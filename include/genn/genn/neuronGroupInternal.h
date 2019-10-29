#pragma once

// GeNN includes
#include "neuronGroup.h"

//------------------------------------------------------------------------
// NeuronGroupInternal
//------------------------------------------------------------------------
class NeuronGroupInternal : public NeuronGroup
{
public:
    NeuronGroupInternal(const std::string &name, int numNeurons, const NeuronModels::Base *neuronModel,
                        const std::vector<double> &params, const std::vector<Models::VarInit> &varInitialisers,
                        VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation, int hostID)
    :   NeuronGroup(name, numNeurons, neuronModel, params, varInitialisers,
                    defaultVarLocation, defaultExtraGlobalParamLocation, hostID)
    {
    }

    NeuronGroupInternal(const std::string &name, int numNeurons, const NeuronModels::Base *neuronModel,
                        const std::vector<Models::VarInit> &varInitialisers,
                        VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation, int hostID)
    :   NeuronGroup(name, numNeurons, neuronModel, varInitialisers,
                    defaultVarLocation, defaultExtraGlobalParamLocation, hostID)
    {
    }
    
    using NeuronGroup::checkNumDelaySlots;
    using NeuronGroup::updatePreVarQueues;
    using NeuronGroup::updatePostVarQueues;
    using NeuronGroup::addSpkEventCondition;
    using NeuronGroup::addInSyn;
    using NeuronGroup::addOutSyn;
    using NeuronGroup::initDerivedParams;
    using NeuronGroup::mergeIncomingPSM;
    using NeuronGroup::injectCurrent;
    using NeuronGroup::getInSyn;
    using NeuronGroup::getMergedInSyn;
    using NeuronGroup::getOutSyn;
    using NeuronGroup::getCurrentSources;
    using NeuronGroup::getDerivedParams;
    using NeuronGroup::getSpikeEventCondition;
    using NeuronGroup::isParamRequiredBySpikeEventCondition;
    using NeuronGroup::getCurrentQueueOffset;
    using NeuronGroup::getPrevQueueOffset;
    using NeuronGroup::isVarQueueRequired;
};
