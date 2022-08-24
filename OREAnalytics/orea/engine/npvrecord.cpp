/*
 Copyright (C) 2020 Quaternion Risk Management Ltd
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

#include <orea/engine/npvrecord.hpp>
#include <iostream>

using std::ostream;
using std::string;

namespace ore {
namespace analytics {

ostream& operator<<(ostream& out, const NpvRecord& nr) {
  return out << "[" <<
        nr.tradeId << ", " <<
        nr.portfolioId << ", " <<
        nr.valuationDate << ", " <<
        nr.baseCurrency << ", " <<
        nr.baseAmount << 
    "]";
}

}
}
