/*
 Copyright (C) 2024 Quaternion Risk Management Ltd
 All rights reserved.

 This file is part of ORE, a free-software/open-source library
 for transparent pricing and risk analysis - http://opensourcerisk.org

 ORE is free software: you can redistribute it and/or modify it
 under the terms of the Modified BSD License.  You should have received a
 copy of the license along with this program.
 The license is also available online at <http://opensourcerisk.org>

 This program is distributed on the basis that it will form a useful
 contribution to risk analytics and model standardisation, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the license for more details.
*/

#include <qle/models/lgmfdsolver.hpp>
#include <qle/methods/fdmlgmop.hpp>

#include <ql/methods/finitedifferences/meshers/fdmmeshercomposite.hpp>
#include <ql/methods/finitedifferences/meshers/fdmsimpleprocess1dmesher.hpp>


namespace QuantExt {

LgmFdSolver::LgmFdSolver(const boost::shared_ptr<LinearGaussMarkovModel>& model, const Real maxTime,
                         const Size stateGridPoints, const Size timeStepsPerYear, const Real mesherEpsilon)
    : model_(model), maxTime_(maxTime), stateGridPoints_(stateGridPoints), timeStepsPerYear_(timeStepsPerYear),
      mesherEpsilon_(mesherEpsilon) {
    mesher_ = boost::make_shared<FdmMesherComposite>(boost::make_shared<FdmSimpleProcess1dMesher>(
        stateGridPoints, boost::dynamic_pointer_cast<StochasticProcess1D>(model->stateProcess()), maxTime_,
        timeStepsPerYear_, mesherEpsilon_));
    mesherLocations_ = RandomVariable(mesher_->locations(0));
    operator_ = boost::make_shared<QuantExt::FdmLgmOp>(
        mesher_, boost::dynamic_pointer_cast<StochasticProcess1D>(model->stateProcess()));
    solver_ = boost::make_shared<FdmBackwardSolver>(
        operator_, std::vector<boost::shared_ptr<BoundaryCondition<FdmLinearOp>>>(), nullptr, FdmSchemeDesc::Douglas());
}

Size LgmFdSolver::gridSize() const { return stateGridPoints_; }

RandomVariable LgmFdSolver::stateGrid() const { return mesherLocations_; }

const boost::shared_ptr<LinearGaussMarkovModel>& LgmFdSolver::model() const { return model_; }

RandomVariable LgmFdSolver::rollback(const RandomVariable& v, const Real t1, const Real t0) const {
    Size steps = std::max<Size>(1, static_cast<Size>(static_cast<double>(timeStepsPerYear_) * (t1 - t0) + 0.5));
    Array workingArray(v.size());
    v.copyToArray(workingArray);
    solver_->rollback(workingArray, t1, t0, steps, 0);
    return RandomVariable(workingArray);
}

} // namespace QuantExt
