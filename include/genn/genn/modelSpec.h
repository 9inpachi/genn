/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
              Falmer, Brighton BN1 9QJ, UK
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
   
   This file contains neuron model declarations.
  
--------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file modelSpec.h

\brief Header file that contains the class (struct) definition of neuronModel for 
defining a neuron model and the class definition of ModelSpec for defining a neuronal network model. 
Part of the code generation and generated code sections.
*/
//--------------------------------------------------------------------------
#pragma once

// Standard C++ includes
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef MPI_ENABLE
#include <mpi.h>
#endif

// GeNN includes
#include "gennExport.h"
#include "neuronGroupInternal.h"
#include "synapseGroupInternal.h"
#include "currentSourceInternal.h"


#define NO_DELAY 0 //!< Macro used to indicate no synapse delay for the group (only one queue slot will be generated)

//! Floating point precision to use for models
enum FloatType
{
    GENN_FLOAT,
    GENN_DOUBLE,
    GENN_LONG_DOUBLE,
};

//! Precision to use for variables which store time
enum class TimePrecision
{
    DEFAULT,    //!< Time uses default model precision
    FLOAT,      //!< Time uses single precision - not suitable for long simulations
    DOUBLE,     //!< Time uses double precision - may reduce performance
};

//! Initialise a variable using an initialisation snippet
/*! \tparam S       type of variable initialisation snippet (derived from InitVarSnippet::Base).
    \param params   parameters for snippet wrapped in S::ParamValues object.
    \return         Models::VarInit object for use within model's VarValues*/
template<typename S>
inline Models::VarInit initVar(const typename S::ParamValues &params)
{
    return Models::VarInit(S::getInstance(), params.getValues());
}

//! Initialise a variable using an initialisation snippet with no parameters
/*! \tparam S       type of variable initialisation snippet (derived from InitVarSnippet::Base).
    \return         Models::VarInit object for use within model's VarValues*/
template<typename S>
inline typename std::enable_if<std::is_same<typename S::ParamValues, Snippet::ValueBase<0>>::value, Models::VarInit>::type initVar()
{
   return Models::VarInit(S::getInstance(), {});
}

//! Mark a variable as uninitialised
/*! This means that the backend will not generate any automatic initialization code, but will instead
    copy the variable from host to device during ``initializeSparse`` function */
inline Models::VarInit uninitialisedVar()
{
    return Models::VarInit(InitVarSnippet::Uninitialised::getInstance(), {});
}

//! Initialise connectivity using a sparse connectivity snippet
/*! \tparam S       type of sparse connectivity initialisation snippet (derived from InitSparseConnectivitySnippet::Base).
    \param params   parameters for snippet wrapped in S::ParamValues object.
    \return         InitSparseConnectivitySnippet::Init object for passing to ``ModelSpec::addSynapsePopulation``*/
template<typename S>
inline InitSparseConnectivitySnippet::Init initConnectivity(const typename S::ParamValues &params)
{
    return InitSparseConnectivitySnippet::Init(S::getInstance(), params.getValues());
}

//! Initialise connectivity using a sparse connectivity snippet with no parameters
/*! \tparam S       type of sparse connectivity initialisation snippet (derived from InitSparseConnectivitySnippet::Base).
    \return         InitSparseConnectivitySnippet::Init object for passing to ``ModelSpec::addSynapsePopulation``*/
template<typename S>
inline typename std::enable_if<std::is_same<typename S::ParamValues, Snippet::ValueBase<0>>::value, InitSparseConnectivitySnippet::Init>::type initConnectivity()
{
    return InitSparseConnectivitySnippet::Init(S::getInstance(), {});
}

//! Mark a synapse group's sparse connectivity as uninitialised
/*! This means that the backend will not generate any automatic initialization code, but will instead
    copy the connectivity from host to device during ``initializeSparse`` function
    (and, if necessary generate any additional data structures it requires)*/
inline InitSparseConnectivitySnippet::Init uninitialisedConnectivity()
{
    return InitSparseConnectivitySnippet::Init(InitSparseConnectivitySnippet::Uninitialised::getInstance(), {});
}

//----------------------------------------------------------------------------
// ModelSpec
//----------------------------------------------------------------------------
//! Object used for specifying a neuronal network model
class GENN_EXPORT ModelSpec
{
public:
    // Typedefines
    //=======================
    typedef std::map<std::string, NeuronGroupInternal>::value_type NeuronGroupValueType;
    typedef std::map<std::string, SynapseGroupInternal>::value_type SynapseGroupValueType;

    ModelSpec();
    ModelSpec(const ModelSpec&) = delete;
    ModelSpec &operator=(const ModelSpec &) = delete;
    ~ModelSpec();

    // PUBLIC MODEL FUNCTIONS
    //=======================
    //! Method to set the neuronal network model name
    void setName(const std::string &name){ m_Name = name; }

    //! Set numerical precision for floating point
    void setPrecision(FloatType);

    //! Set numerical precision for time
    void setTimePrecision(TimePrecision timePrecision){ m_TimePrecision = timePrecision; }

    //! Set the integration step size of the model
    void setDT(double dt){ m_DT = dt; }

    //! Set whether timers and timing commands are to be included
    void setTiming(bool timingEnabled){ m_TimingEnabled = timingEnabled; }

    //! Set the random seed (disables automatic seeding if argument not 0).
    void setSeed(unsigned int rngSeed){ m_Seed = rngSeed; }

    //! What is the default location for model state variables?
    /*! Historically, everything was allocated on both the host AND device */
    void setDefaultVarLocation(VarLocation loc){ m_DefaultVarLocation = loc; }

    //! What is the default location for model extra global parameters?
    /*! Historically, this was just left up to the user to handle*/
    void setDefaultExtraGlobalParamLocation(VarLocation loc){ m_DefaultExtraGlobalParamLocation = loc; }

    //! What is the default location for sparse synaptic connectivity? 
    /*! Historically, everything was allocated on both the host AND device */
    void setDefaultSparseConnectivityLocation(VarLocation loc){ m_DefaultSparseConnectivityLocation = loc; }

    //! Sets default for whether narrow i.e. less than 32-bit types are used for sparse matrix indices
    void setDefaultNarrowSparseIndEnabled(bool enabled){ m_DefaultNarrowSparseIndEnabled = enabled; }

    //! Should compatible postsynaptic models and dendritic delay buffers be merged?
    /*! This can significantly reduce the cost of updating neuron population but means that per-synapse group inSyn arrays can not be retrieved */
    void setMergePostsynapticModels(bool merge){ m_ShouldMergePostsynapticModels = merge; }


    //! Gets the name of the neuronal network model
    const std::string &getName() const{ return m_Name; }

    //! Gets the floating point numerical precision
    const std::string &getPrecision() const{ return m_Precision; }

    //! Gets the floating point numerical precision used to represent time
    std::string getTimePrecision() const;

    //! Gets the model integration step size
    double getDT() const { return m_DT; }

    //! Get the random seed
    unsigned int getSeed() const { return m_Seed; }

    //! Are timers and timing commands enabled
    bool isTimingEnabled() const{ return m_TimingEnabled; }

    // PUBLIC NEURON FUNCTIONS
    //========================
    //! How many neurons are simulated locally in this model
    unsigned int getNumLocalNeurons() const;

    //! How many neurons are simulated remotely in this model
    unsigned int getNumRemoteNeurons() const;

    //! How many neurons make up the entire model
    unsigned int getNumNeurons() const{ return getNumLocalNeurons() + getNumRemoteNeurons(); }

    //! Find a neuron group by name
    NeuronGroup *findNeuronGroup(const std::string &name){ return static_cast<NeuronGroup*>(findNeuronGroupInternal(name)); }

    //! Adds a new neuron group to the model using a neuron model managed by the user
    /*! \tparam NeuronModel type of neuron model (derived from NeuronModels::Base).
        \param name string containing unique name of neuron population.
        \param size integer specifying how many neurons are in the population.
        \param model neuron model to use for neuron group.
        \param paramValues parameters for model wrapped in NeuronModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param hostID if using MPI, the ID of the node to simulate this population on.
        \return pointer to newly created NeuronGroup */
    template<typename NeuronModel>
    NeuronGroup *addNeuronPopulation(const std::string &name, unsigned int size, const NeuronModel *model,
                                     const typename NeuronModel::ParamValues &paramValues,
                                     const typename NeuronModel::VarValues &varInitialisers,
                                     int hostID = 0)
    {
#ifdef MPI_ENABLE
        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalNeuronGroups : m_RemoteNeuronGroups;
#else
        // If MPI is disabled always add to local neuron groups and zero host id
        auto &groupMap = m_LocalNeuronGroups;
        hostID = 0;
#endif

        // Add neuron group to map
        auto result = groupMap.emplace(std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(name, size, model,
                                  paramValues.getValues(), varInitialisers.getInitialisers(), 
                                  m_DefaultVarLocation, m_DefaultExtraGlobalParamLocation, hostID));

        if(!result.second) {
            throw std::runtime_error("Cannot add a neuron population with duplicate name:" + name);
        }
        else {
            return &result.first->second;
        }
    }

    //! Adds a new neuron group to the model using a neuron model managed by the user
    /*! \tparam NeuronModel type of neuron model (derived from NeuronModels::Base).
        \param model neuron model to use for neuron group.
        \param name string containing unique name of neuron population.
        \param size integer specifying how many neurons are in the population.
        \param varInitialisers variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param hostID if using MPI, the ID of the node to simulate this population on.
        \return pointer to newly created NeuronGroup */
    template<typename NeuronModel>
    NeuronGroup *addNeuronPopulation(const NeuronModel *model, const std::string &name, unsigned int size,
                                     const typename NeuronModel::VarValues &varInitialisers,
                                     int hostID = 0)
    {
#ifdef MPI_ENABLE
        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalNeuronGroups : m_RemoteNeuronGroups;
#else
        // If MPI is disabled always add to local neuron groups and zero host id
        auto &groupMap = m_LocalNeuronGroups;
        hostID = 0;
#endif

        // Add neuron group to map
        auto result = groupMap.emplace(std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(name, size, model,
                                  varInitialisers.getInitialisers(),
                                  m_DefaultVarLocation, m_DefaultExtraGlobalParamLocation, hostID));

        if(!result.second) {
            throw std::runtime_error("Cannot add a neuron population with duplicate name:" + name);
        }
        else {
            return &result.first->second;
        }
    }

    //! Adds a new neuron group to the model using a singleton neuron model created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam NeuronModel type of neuron model (derived from NeuronModels::Base).
        \param name string containing unique name of neuron population.
        \param size integer specifying how many neurons are in the population.
        \param paramValues parameters for model wrapped in NeuronModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param hostID if using MPI, the ID of the node to simulate this population on.
        \return pointer to newly created NeuronGroup */
    template<typename NeuronModel>
    NeuronGroup *addNeuronPopulation(const std::string &name, unsigned int size,
                                     const typename NeuronModel::ParamValues &paramValues, const typename NeuronModel::VarValues &varInitialisers,
                                     int hostID = 0)
    {
        return addNeuronPopulation<NeuronModel>(name, size, NeuronModel::getInstance(), paramValues, varInitialisers, hostID);
    }

    //! Adds a new neuron group to the model using a singleton neuron model created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam NeuronModel type of neuron model (derived from NeuronModels::Base).
        \param name string containing unique name of neuron population.
        \param size integer specifying how many neurons are in the population.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param hostID if using MPI, the ID of the node to simulate this population on.
        \return pointer to newly created NeuronGroup */
    template<typename NeuronModel>
    NeuronGroup *addNeuronPopulation(const std::string &name, unsigned int size,
                                     const typename NeuronModel::VarValues &varInitialisers,
                                     int hostID = 0)
    {
        return addNeuronPopulation<NeuronModel>(NeuronModel::getInstance(), name, size, varInitialisers, hostID);
    }

    // PUBLIC SYNAPSE FUNCTIONS
    //=========================
    //! Find a synapse group by name
    SynapseGroup *findSynapseGroup(const std::string &name);    

    //! Adds a synapse population to the model using weight update and postsynaptic models managed by the user
    /*! \tparam WeightUpdateModel           type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel           type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name                         string containing unique name of neuron population.
        \param mtype                        how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps                   integer specifying number of timesteps delay this synaptic connection should incur
                                            (or NO_DELAY for none)
        \param src                          string specifying name of presynaptic (source) population
        \param trg                          string specifying name of postsynaptic (target) population
        \param wum                          weight update model to use for synapse group.
        \param weightParamValues            parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers        weight update model state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPreVarInitialisers     weight update model presynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPostVarInitialisers    weight update model postsynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param psm                          postsynaptic model to use for synapse group.
        \param postsynapticParamValues      parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers  postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param connectivityInitialiser      sparse connectivity initialisation snippet used to initialise connectivity for
                                            SynapseMatrixConnectivity::SPARSE or SynapseMatrixConnectivity::BITMASK.
                                            Typically wrapped with it's parameters using ``initConnectivity`` function
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const std::string &name, SynapseMatrixType mtype, unsigned int delaySteps, const std::string& src, const std::string& trg,
                                       const WeightUpdateModel *wum, const typename WeightUpdateModel::ParamValues &weightParamValues, const typename WeightUpdateModel::VarValues &weightVarInitialisers, const typename WeightUpdateModel::PreVarValues &weightPreVarInitialisers, const typename WeightUpdateModel::PostVarValues &weightPostVarInitialisers,
                                       const PostsynapticModel *psm, const typename PostsynapticModel::ParamValues &postsynapticParamValues, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        // Get source and target neuron groups
        auto srcNeuronGrp = findNeuronGroupInternal(src);
        auto trgNeuronGrp = findNeuronGroupInternal(trg);

#ifdef MPI_ENABLE
        // Get host ID of target neuron group
        const int hostID = trgNeuronGrp->getClusterHostID();

        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalSynapseGroups : m_RemoteSynapseGroups;
#else
        // If MPI is disabled always add to local synapse groups
        auto &groupMap = m_LocalSynapseGroups;
#endif

        // Add synapse group to map
        auto result = groupMap.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(name, mtype, delaySteps,
                                  wum, weightParamValues.getValues(), weightVarInitialisers.getInitialisers(), weightPreVarInitialisers.getInitialisers(), weightPostVarInitialisers.getInitialisers(),
                                  psm, postsynapticParamValues.getValues(), postsynapticVarInitialisers.getInitialisers(),
                                  srcNeuronGrp, trgNeuronGrp,
                                  connectivityInitialiser, m_DefaultVarLocation, m_DefaultExtraGlobalParamLocation,
                                  m_DefaultSparseConnectivityLocation, m_DefaultNarrowSparseIndEnabled));

        if(!result.second) {
            throw std::runtime_error("Cannot add a synapse population with duplicate name:" + name);
        }
        else {
            return &result.first->second;
        }
    }

    //! Adds a synapse population to the model using weight update and postsynaptic models managed by the user
    /*! \tparam WeightUpdateModel           type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel           type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name                         string containing unique name of neuron population.
        \param connectivity                 how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps                   integer specifying number of timesteps delay this synaptic connection should incur
                                            (or NO_DELAY for none)
        \param src                          string specifying name of presynaptic (source) population
        \param trg                          string specifying name of postsynaptic (target) population
        \param wum                          weight update model to use for synapse group.
        \param weightParamValues            parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers        weight update model state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPreVarInitialisers     weight update model presynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPostVarInitialisers    weight update model postsynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param psm                          postsynaptic model to use for synapse group.
        \param postsynapticParamValues      parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers  postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param connectivityInitialiser      sparse connectivity initialisation snippet used to initialise connectivity for
                                            SynapseMatrixConnectivity::SPARSE or SynapseMatrixConnectivity::BITMASK.
                                            Typically wrapped with it's parameters using ``initConnectivity`` function
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const std::string &name, SynapseMatrixConnectivity connectivity, unsigned int delaySteps, const std::string& src, const std::string& trg,
                                       const WeightUpdateModel *wum, const typename WeightUpdateModel::VarValues &weightVarInitialisers, const typename WeightUpdateModel::PreVarValues &weightPreVarInitialisers, const typename WeightUpdateModel::PostVarValues &weightPostVarInitialisers,
                                       const PostsynapticModel *psm, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        // Get source and target neuron groups
        auto srcNeuronGrp = findNeuronGroupInternal(src);
        auto trgNeuronGrp = findNeuronGroupInternal(trg);

#ifdef MPI_ENABLE
        // Get host ID of target neuron group
        const int hostID = trgNeuronGrp->getClusterHostID();

        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalSynapseGroups : m_RemoteSynapseGroups;
#else
        // If MPI is disabled always add to local synapse groups
        auto &groupMap = m_LocalSynapseGroups;
#endif

        // Add synapse group to map
        auto result = groupMap.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(name, connectivity, delaySteps,
                                  wum, weightVarInitialisers.getInitialisers(), weightPreVarInitialisers.getInitialisers(), weightPostVarInitialisers.getInitialisers(),
                                  psm, postsynapticVarInitialisers.getInitialisers(),
                                  srcNeuronGrp, trgNeuronGrp,
                                  connectivityInitialiser, m_DefaultVarLocation, m_DefaultExtraGlobalParamLocation,
                                  m_DefaultSparseConnectivityLocation, m_DefaultNarrowSparseIndEnabled));

        if(!result.second) {
            throw std::runtime_error("Cannot add a synapse population with duplicate name:" + name);
        }
        else {
            return &result.first->second;
        }
    }

    //! Adds a synapse population to the model using singleton weight update and postsynaptic models created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam WeightUpdateModel           type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel           type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name                         string containing unique name of neuron population.
        \param mtype                        how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps                   integer specifying number of timesteps delay this synaptic connection should incur (or NO_DELAY for none)
        \param src                          string specifying name of presynaptic (source) population
        \param trg                          string specifying name of postsynaptic (target) population
        \param weightParamValues            parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers        weight update model state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param postsynapticParamValues      parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers  postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param connectivityInitialiser      sparse connectivity initialisation snippet used to initialise connectivity for
                                            SynapseMatrixConnectivity::SPARSE or SynapseMatrixConnectivity::BITMASK.
                                            Typically wrapped with it's parameters using ``initConnectivity`` function
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const std::string &name, SynapseMatrixType mtype, unsigned int delaySteps, const std::string& src, const std::string& trg,
                                       const typename WeightUpdateModel::ParamValues &weightParamValues, const typename WeightUpdateModel::VarValues &weightVarInitialisers,
                                       const typename PostsynapticModel::ParamValues &postsynapticParamValues, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        // Create empty pre and postsynaptic weight update variable initialisers
        typename WeightUpdateModel::PreVarValues weightPreVarInitialisers;
        typename WeightUpdateModel::PostVarValues weightPostVarInitialisers;

        return addSynapsePopulation(name, mtype, delaySteps, src, trg,
                                    WeightUpdateModel::getInstance(), weightParamValues, weightVarInitialisers, weightPreVarInitialisers, weightPostVarInitialisers,
                                    PostsynapticModel::getInstance(), postsynapticParamValues, postsynapticVarInitialisers,
                                    connectivityInitialiser);
    }

    //! Adds a synapse population to the model using singleton weight update and postsynaptic models created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam WeightUpdateModel           type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel           type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name                         string containing unique name of neuron population.
        \param connectivity                 how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps                   integer specifying number of timesteps delay this synaptic connection should incur (or NO_DELAY for none)
        \param src                          string specifying name of presynaptic (source) population
        \param trg                          string specifying name of postsynaptic (target) population
        \param weightVarInitialisers        weight update model state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param postsynapticVarInitialisers  postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param connectivityInitialiser      sparse connectivity initialisation snippet used to initialise connectivity for
                                            SynapseMatrixConnectivity::SPARSE or SynapseMatrixConnectivity::BITMASK.
                                            Typically wrapped with it's parameters using ``initConnectivity`` function
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const std::string &name, SynapseMatrixConnectivity connectivity, unsigned int delaySteps, const std::string& src, const std::string& trg,
                                       const typename WeightUpdateModel::VarValues &weightVarInitialisers,
                                       const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        // Create empty pre and postsynaptic weight update variable initialisers
        typename WeightUpdateModel::PreVarValues weightPreVarInitialisers;
        typename WeightUpdateModel::PostVarValues weightPostVarInitialisers;

        return addSynapsePopulation(name, connectivity, delaySteps, src, trg,
                                    WeightUpdateModel::getInstance(), weightVarInitialisers, weightPreVarInitialisers, weightPostVarInitialisers,
                                    PostsynapticModel::getInstance(), postsynapticVarInitialisers,
                                    connectivityInitialiser);
    }

    //! Adds a synapse population to the model using singleton weight update and postsynaptic models created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam WeightUpdateModel           type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel           type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name                         string containing unique name of neuron population.
        \param mtype                        how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps                   integer specifying number of timesteps delay this synaptic connection should incur (or NO_DELAY for none)
        \param src                          string specifying name of presynaptic (source) population
        \param trg                          string specifying name of postsynaptic (target) population
        \param weightParamValues            parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers        weight update model per-synapse state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPreVarInitialisers     weight update model presynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPostVarInitialisers    weight update model postsynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param postsynapticParamValues      parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers  postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param connectivityInitialiser      sparse connectivity initialisation snippet used to initialise connectivity for
                                            SynapseMatrixConnectivity::SPARSE or SynapseMatrixConnectivity::BITMASK.
                                            Typically wrapped with it's parameters using ``initConnectivity`` function
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const std::string &name, SynapseMatrixType mtype, unsigned int delaySteps, const std::string& src, const std::string& trg,
                                       const typename WeightUpdateModel::ParamValues &weightParamValues, const typename WeightUpdateModel::VarValues &weightVarInitialisers, const typename WeightUpdateModel::PreVarValues &weightPreVarInitialisers, const typename WeightUpdateModel::PostVarValues &weightPostVarInitialisers,
                                       const typename PostsynapticModel::ParamValues &postsynapticParamValues, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        return addSynapsePopulation(name, mtype, delaySteps, src, trg,
                                    WeightUpdateModel::getInstance(), weightParamValues, weightVarInitialisers, weightPreVarInitialisers, weightPostVarInitialisers,
                                    PostsynapticModel::getInstance(), postsynapticParamValues, postsynapticVarInitialisers,
                                    connectivityInitialiser);

    }

    //! Adds a synapse population to the model using singleton weight update and postsynaptic models created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam WeightUpdateModel           type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel           type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name                         string containing unique name of neuron population.
        \param connectivity                 how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps                   integer specifying number of timesteps delay this synaptic connection should incur (or NO_DELAY for none)
        \param src                          string specifying name of presynaptic (source) population
        \param trg                          string specifying name of postsynaptic (target) population
        \param weightParamValues            parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers        weight update model per-synapse state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPreVarInitialisers     weight update model presynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param weightPostVarInitialisers    weight update model postsynaptic state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param postsynapticParamValues      parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers  postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \param connectivityInitialiser      sparse connectivity initialisation snippet used to initialise connectivity for
                                            SynapseMatrixConnectivity::SPARSE or SynapseMatrixConnectivity::BITMASK.
                                            Typically wrapped with it's parameters using ``initConnectivity`` function
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const std::string &name, SynapseMatrixConnectivity connectivity, unsigned int delaySteps, const std::string& src, const std::string& trg,
                                       const typename WeightUpdateModel::ParamValues &weightParamValues, const typename WeightUpdateModel::VarValues &weightVarInitialisers, const typename WeightUpdateModel::PreVarValues &weightPreVarInitialisers, const typename WeightUpdateModel::PostVarValues &weightPostVarInitialisers,
                                       const typename PostsynapticModel::ParamValues &postsynapticParamValues, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        return addSynapsePopulation(name, connectivity, delaySteps, src, trg,
                                    WeightUpdateModel::getInstance(), weightVarInitialisers, weightPreVarInitialisers, weightPostVarInitialisers,
                                    PostsynapticModel::getInstance(), postsynapticVarInitialisers,
                                    connectivityInitialiser);

    }

    // PUBLIC CURRENT SOURCE FUNCTIONS
    //================================
    //! Find a current source by name
    CurrentSource *findCurrentSource(const std::string &name);

    //! Adds a new current source to the model using a current source model managed by the user
    /*! \tparam CurrentSourceModel type of current source model (derived from CurrentSourceModels::Base).
        \param currentSourceName string containing unique name of current source.
        \param model current source model to use for current source.
        \param targetNeuronGroupName string name of the target neuron group
        \param paramValues parameters for model wrapped in CurrentSourceModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in CurrentSource::VarValues object.
        \return pointer to newly created CurrentSource */
    template<typename CurrentSourceModel>
    CurrentSource *addCurrentSource(const std::string &currentSourceName, const CurrentSourceModel *model,
                                    const std::string &targetNeuronGroupName,
                                    const typename CurrentSourceModel::ParamValues &paramValues,
                                    const typename CurrentSourceModel::VarValues &varInitialisers)
    {
        auto targetGroup = findNeuronGroupInternal(targetNeuronGroupName);

#ifdef MPI_ENABLE
        // Get host ID of target neuron group
        const int hostID = targetGroup->getClusterHostID();

        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalCurrentSources : m_RemoteCurrentSources;
#else
        // If MPI is disabled always add to local current sources
        auto &groupMap = m_LocalCurrentSources;
#endif

        // Add current source to map
        auto result = groupMap.emplace(std::piecewise_construct,
            std::forward_as_tuple(currentSourceName),
            std::forward_as_tuple(currentSourceName, model,
                                  paramValues.getValues(), varInitialisers.getInitialisers(),
                                  m_DefaultVarLocation, m_DefaultExtraGlobalParamLocation));

        if(!result.second) {
            throw std::runtime_error("Cannot add a current source with duplicate name:" + currentSourceName);
        }
        else {
            targetGroup->injectCurrent(&result.first->second);
            return &result.first->second;
        }
    }

    //! Adds a new current source to the model using a current source model managed by the user
    /*! \tparam CurrentSourceModel type of current source model (derived from CurrentSourceModels::Base).
        \param currentSourceName string containing unique name of current source.
        \param model current source model to use for current source.
        \param targetNeuronGroupName string name of the target neuron group
        \param varInitialisers state variable initialiser snippets and parameters wrapped in CurrentSource::VarValues object.
        \return pointer to newly created CurrentSource */
    template<typename CurrentSourceModel>
    CurrentSource *addCurrentSource(const std::string &currentSourceName, const CurrentSourceModel *model,
                                    const std::string &targetNeuronGroupName,
                                    const typename CurrentSourceModel::VarValues &varInitialisers)
    {
        auto targetGroup = findNeuronGroupInternal(targetNeuronGroupName);

#ifdef MPI_ENABLE
        // Get host ID of target neuron group
        const int hostID = targetGroup->getClusterHostID();

        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalCurrentSources : m_RemoteCurrentSources;
#else
        // If MPI is disabled always add to local current sources
        auto &groupMap = m_LocalCurrentSources;
#endif

        // Add current source to map
        auto result = groupMap.emplace(std::piecewise_construct,
            std::forward_as_tuple(currentSourceName),
            std::forward_as_tuple(currentSourceName, model,
                                  varInitialisers.getInitialisers(),
                                  m_DefaultVarLocation, m_DefaultExtraGlobalParamLocation));

        if(!result.second) {
            throw std::runtime_error("Cannot add a current source with duplicate name:" + currentSourceName);
        }
        else {
            targetGroup->injectCurrent(&result.first->second);
            return &result.first->second;
        }
    }

    //! Adds a new current source to the model using a singleton current source model created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam CurrentSourceModel type of neuron model (derived from CurrentSourceModel::Base).
        \param currentSourceName string containing unique name of current source.
        \param targetNeuronGroupName string name of the target neuron group
        \param paramValues parameters for model wrapped in CurrentSourceModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in CurrentSourceModel::VarValues object.
        \return pointer to newly created CurrentSource */
    template<typename CurrentSourceModel>
    CurrentSource *addCurrentSource(const std::string &currentSourceName, const std::string &targetNeuronGroupName,
                                    const typename CurrentSourceModel::ParamValues &paramValues,
                                    const typename CurrentSourceModel::VarValues &varInitialisers)
    {
        return addCurrentSource<CurrentSourceModel>(currentSourceName, CurrentSourceModel::getInstance(),
                                                    targetNeuronGroupName, paramValues, varInitialisers);
    }

    //! Adds a new current source to the model using a singleton current source model created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam CurrentSourceModel type of neuron model (derived from CurrentSourceModel::Base).
        \param currentSourceName string containing unique name of current source.
        \param targetNeuronGroupName string name of the target neuron group
        \param varInitialisers state variable initialiser snippets and parameters wrapped in CurrentSourceModel::VarValues object.
        \return pointer to newly created CurrentSource */
    template<typename CurrentSourceModel>
    CurrentSource *addCurrentSource(const std::string &currentSourceName, const std::string &targetNeuronGroupName,
                                    const typename CurrentSourceModel::VarValues &varInitialisers)
    {
        return addCurrentSource<CurrentSourceModel>(currentSourceName, CurrentSourceModel::getInstance(),
                                                    targetNeuronGroupName, varInitialisers);
    }

protected:
    //--------------------------------------------------------------------------
    // Protected methods
    //--------------------------------------------------------------------------
    //! Finalise model
    void finalize();

    //--------------------------------------------------------------------------
    // Protected const methods
    //--------------------------------------------------------------------------
    //! Get the string literal that should be used to represent a value in the model's floating-point type
    std::string scalarExpr(double) const;

    //! Are any variables in any populations in this model using zero-copy memory?
    bool zeroCopyInUse() const;

    //! Get std::map containing local named NeuronGroup objects in model
    const std::map<std::string, NeuronGroupInternal> &getLocalNeuronGroups() const{ return m_LocalNeuronGroups; }

    //! Get std::map containing remote named NeuronGroup objects in model
    const std::map<std::string, NeuronGroupInternal> &getRemoteNeuronGroups() const{ return m_RemoteNeuronGroups; }

    //! Get std::map containing local named SynapseGroup objects in model
    const std::map<std::string, SynapseGroupInternal> &getLocalSynapseGroups() const{ return m_LocalSynapseGroups; }

    //! Get std::map containing remote named SynapseGroup objects in model
    const std::map<std::string, SynapseGroupInternal> &getRemoteSynapseGroups() const{ return m_RemoteSynapseGroups; }

    //! Get std::map containing local named CurrentSource objects in model
    const std::map<std::string, CurrentSourceInternal> &getLocalCurrentSources() const{ return m_LocalCurrentSources; }

    //! Get std::map containing remote named CurrentSource objects in model
    const std::map<std::string, CurrentSourceInternal> &getRemoteCurrentSources() const{ return m_RemoteCurrentSources; }

private:
    //--------------------------------------------------------------------------
    // Private methods
    //--------------------------------------------------------------------------
    //! Find a neuron group by name
    NeuronGroupInternal *findNeuronGroupInternal(const std::string &name);
    
    //--------------------------------------------------------------------------
    // Private members
    //--------------------------------------------------------------------------
    //! Named local neuron groups
    std::map<std::string, NeuronGroupInternal> m_LocalNeuronGroups;

    //! Named remote neuron groups
    std::map<std::string, NeuronGroupInternal> m_RemoteNeuronGroups;

    //! Named local synapse groups
    std::map<std::string, SynapseGroupInternal> m_LocalSynapseGroups;

    //! Named remote synapse groups
    std::map<std::string, SynapseGroupInternal> m_RemoteSynapseGroups;

    //! Named local current sources
    std::map<std::string, CurrentSourceInternal> m_LocalCurrentSources;

    //! Named remote current sources
    std::map<std::string, CurrentSourceInternal> m_RemoteCurrentSources;

    //! Name of the neuronal newtwork model
    std::string m_Name;

    //! Type of floating point variables (float, double, ...; default: float)
    std::string m_Precision;

    //! Type of floating point variables used to store time
    TimePrecision m_TimePrecision;

    //! The integration time step of the model
    double m_DT;

    //! Whether timing code should be inserted into model
    bool m_TimingEnabled;

    //! RNG seed
    unsigned int m_Seed;

    //! What is the default location for model state variables? Historically, everything was allocated on both host AND device
    VarLocation m_DefaultVarLocation;

    //! What is the default location for model extra global parameters? Historically, this was just left up to the user to handle
    VarLocation m_DefaultExtraGlobalParamLocation;

    //! What is the default location for sparse synaptic connectivity? Historically, everything was allocated on both the host AND device
    VarLocation m_DefaultSparseConnectivityLocation; 

    //! The default for whether narrow i.e. less than 32-bit types are used for sparse matrix indices
    bool m_DefaultNarrowSparseIndEnabled;

    //! Should compatible postsynaptic models and dendritic delay buffers be merged?
    /*! This can significantly reduce the cost of updating neuron population but means that per-synapse group inSyn arrays can not be retrieved */
    bool m_ShouldMergePostsynapticModels; 
};

// Typedefine NNmodel for backward compatibility
typedef ModelSpec NNmodel;
