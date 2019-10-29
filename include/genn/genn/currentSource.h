#pragma once

// Standard includes
#include <map>
#include <set>
#include <string>
#include <vector>

// GeNN includes
#include "currentSourceModels.h"
#include "gennExport.h"
#include "variableImplementation.h"
#include "variableMode.h"

//------------------------------------------------------------------------
// CurrentSource
//------------------------------------------------------------------------
class GENN_EXPORT CurrentSource
{
public:
    CurrentSource(const CurrentSource&) = delete;
    CurrentSource() = delete;

    //------------------------------------------------------------------------
    // Public methods
    //------------------------------------------------------------------------
    //! Set location of current source state variable
    void setVarLocation(const std::string &varName, VarLocation loc);

    //! Set variable implentation of current source state variable
    void setVarImplementation(const std::string &varName, VarImplementation impl);

    //! Set location of extra global parameter
    /*! This is ignored for simulations on hardware with a single memory space
        and only applies to extra global parameters which are pointers. */
    void setExtraGlobalParamLocation(const std::string &paramName, VarLocation loc);

    //------------------------------------------------------------------------
    // Public const methods
    //------------------------------------------------------------------------
    const std::string &getName() const{ return m_Name; }

    //! Gets the current source model used by this group
    const CurrentSourceModels::Base *getCurrentSourceModel() const{ return m_CurrentSourceModel; }

    const std::vector<Models::VarInit> &getVarInitialisers() const{ return m_VarInitialisers; }
    const std::vector<VarImplementation> &getVarImplementation() const{ return m_VarImplementation; }

    //! Get variable location for current source model state variable
    VarLocation getVarLocation(const std::string &varName) const;

    //! Get variable location for current source model state variable
    VarLocation getVarLocation(size_t index) const{ return m_VarLocation.at(index); }

    //! Get implementation of current source state variable by name
    VarImplementation getVarImplementation(const std::string &varName) const;

    //! Get implementation of current source state variable by index
    VarImplementation getVarImplementation(size_t index) const{ return m_VarImplementation.at(index); }

    //! Get location of neuron model extra global parameter by name
    /*! This is only used by extra global parameters which are pointers*/
    VarLocation getExtraGlobalParamLocation(const std::string &paramName) const;

    //! Get location of neuron model extra global parameter by omdex
    /*! This is only used by extra global parameters which are pointers*/
    VarLocation getExtraGlobalParamLocation(size_t index) const{ return m_ExtraGlobalParamLocation.at(index); }

protected:
    CurrentSource(const std::string &name, const CurrentSourceModels::Base *currentSourceModel,
                  const std::vector<double> &params, const std::vector<Models::VarInit> &varInitialisers,
                  VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation);

    CurrentSource(const std::string &name, const CurrentSourceModels::Base *currentSourceModel,
                  const std::vector<Models::VarInit> &varInitialisers,
                  VarLocation defaultVarLocation, VarLocation defaultExtraGlobalParamLocation);

    //------------------------------------------------------------------------
    // Protected methods
    //------------------------------------------------------------------------
    void initDerivedParams(double dt);

    //------------------------------------------------------------------------
    // Protected const methods
    //------------------------------------------------------------------------
    const std::vector<double> &getDerivedParams() const{ return m_DerivedParams; }

    //! Does this current source require an RNG to simulate
    bool isSimRNGRequired() const;

    //! Does this current source group require an RNG for it's init code
    bool isInitRNGRequired() const;

private:
    //------------------------------------------------------------------------
    // Members
    //------------------------------------------------------------------------
    std::string m_Name;

    const CurrentSourceModels::Base *m_CurrentSourceModel;
    std::vector<double> m_DerivedParams;
    std::vector<Models::VarInit> m_VarInitialisers;

    //! Location of individual state variables
    std::vector<VarLocation> m_VarLocation;

    //! How should variables be implemented
    std::vector<VarImplementation> m_VarImplementation;

    //! Location of extra global parameters
    std::vector<VarLocation> m_ExtraGlobalParamLocation;
};
