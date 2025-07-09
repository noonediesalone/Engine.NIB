/*
 Copyright (C) 2023 Quaternion Risk Management Ltd
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

/*! \file oreap/app/analytics/bacvaanalytic.hpp
    \brief ORE BACVA Analytic
*/
#pragma once

#include <orea/app/analytic.hpp>
#include <orea/app/analytics/analyticfactory.hpp>
#include <orea/app/analytics/saccranalytic.hpp>

namespace ore {
namespace analytics {

class BaCvaAnalyticImpl : public Analytic::Impl {
public:
    static constexpr const char* LABEL = "BA_CVA";
    static constexpr const char* saccrLookupKey = "SA_CCR";

    BaCvaAnalyticImpl(const QuantLib::ext::shared_ptr<ore::analytics::InputParameters>& inputs)
        : Analytic::Impl(inputs) {
        setLabel(LABEL);
    }
    void runAnalytic(const QuantLib::ext::shared_ptr<ore::data::InMemoryLoader>& loader,
                     const std::set<std::string>& runTypes = {}) override;

    void setUpConfigurations() override;
    void buildDependencies() override;
};

class BaCvaAnalytic : public Analytic {
public:
    BaCvaAnalytic(const QuantLib::ext::shared_ptr<ore::analytics::InputParameters>& inputs,
                  const QuantLib::ext::weak_ptr<ore::analytics::AnalyticsManager>& analyticsManager)
        : Analytic(std::make_unique<BaCvaAnalyticImpl>(inputs), {"BA_CVA"}, inputs, analyticsManager) {}
};

} // namespace analytics
} // namespace ore
