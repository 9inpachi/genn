#pragma once

// Standard includes
#include <array>
#include <functional>
#include <string>
#include <tuple>
#include <vector>
#include <cmath>

// GeNN includes
#include "gennExport.h"
#include "models.h"

//----------------------------------------------------------------------------
// Macros
//----------------------------------------------------------------------------
#define SET_INJECTION_CODE(INJECTION_CODE) virtual std::string getInjectionCode() const override{ return INJECTION_CODE; }

//----------------------------------------------------------------------------
// CurrentSourceModels::Base
//----------------------------------------------------------------------------
namespace CurrentSourceModels
{
//! Base class for all current source models
class Base : public Models::Base
{
public:
    //----------------------------------------------------------------------------
    // Declared virtuals
    //----------------------------------------------------------------------------
    //! Gets the code that defines current injected each timestep 
    virtual std::string getInjectionCode() const{ return ""; }
};

//----------------------------------------------------------------------------
// CurrentSourceModels::DC
//----------------------------------------------------------------------------
//! DC source
/*! It has a single parameter:

    - \c amp    - amplitude of the current [nA]
*/
class DC : public Base
{
    DECLARE_MODEL(DC, 1, 0);

    SET_INJECTION_CODE("$(injectCurrent, $(amp));\n");

    SET_PARAM_NAMES({"amp"});
};

//----------------------------------------------------------------------------
// CurrentSourceModels::DCAuto
//----------------------------------------------------------------------------
//! DC source
/*! It has a single parameter:

    - \c amp    - amplitude of the current [nA]
*/
class DCAuto : public Base
{
    DECLARE_MODEL(DCAuto, 0, 1);

    SET_INJECTION_CODE("$(injectCurrent, $(amp));\n");

    SET_VARS({{"amp", "scalar", VarAccess::READ_ONLY}});
};

//----------------------------------------------------------------------------
// CurrentSourceModels::GaussianNoise
//----------------------------------------------------------------------------
//! Noisy current source with noise drawn from normal distribution
/*! It has 2 parameters:
    - \c mean   - mean of the normal distribution [nA]
    - \c sd     - standard deviation of the normal distribution [nA]
*/
class GaussianNoise : public Base
{
    DECLARE_MODEL(GaussianNoise, 2, 0);

    SET_INJECTION_CODE("$(injectCurrent, $(mean) + $(gennrand_normal) * $(sd));\n");

    SET_PARAM_NAMES({"mean", "sd"} );
};

//----------------------------------------------------------------------------
// CurrentSourceModels::GaussianNoiseAuto
//----------------------------------------------------------------------------
//! Noisy current source with noise drawn from normal distribution
/*! It has 2 parameters:
    - \c mean   - mean of the normal distribution [nA]
    - \c sd     - standard deviation of the normal distribution [nA]
*/
class GaussianNoiseAuto : public Base
{
    DECLARE_MODEL(GaussianNoiseAuto, 0, 2);

    SET_INJECTION_CODE("$(injectCurrent, $(mean) + $(gennrand_normal) * $(sd));\n");

    SET_VARS({{"mean",  "scalar",   VarAccess::READ_ONLY},
              {"sd",    "scalar",   VarAccess::READ_ONLY}});
};
} // CurrentSourceModels
