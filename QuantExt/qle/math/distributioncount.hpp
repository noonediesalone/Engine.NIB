/*
 Copyright (C) 2025 Quaternion Risk Management Ltd
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

/*! \file qle/math/distributioncount.hpp
    \brief histogramm utility
    \ingroup indexes
*/

#pragma once

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>

namespace QuantExt {

template <class I>
void distributionCount(I begin, I end, const Size steps, std::vector<Real>& bounds, std::vector<Size>& counts,
                       const Real coveredStdDevs = Null<Real>()) {

    std::vector<Real> v(begin, end);
    std::sort(v.begin(), v.end());

    Real xmin, xmax;
    if (coveredStdDevs == Null<Real>()) {

        // cover [xmin, xmax] in output

        xmin = *std::min_element(begin, end);
        xmax = *std::max_element(begin, end);

    } else {

        // cover given number of std devs around mean in output

        boost::accumulators::accumulator_set<
            double, boost::accumulators::stats<boost::accumulators::tag::mean, boost::accumulators::tag::variance>>
            acc;
        std::for_each(v.begin(), v.end(), [&acc](double x) { acc(x); });
        double mean = boost::accumulators::mean(acc);
        double stdDev = std::sqrt(boost::accumulators::variance(acc));
        xmin = mean - coveredStdDevs * stdDev;
        xmax = mean + coveredStdDevs * stdDev;
    }

    Real h = (xmax - xmin) / static_cast<Real>(steps);
    Size idx0 = coveredStdDevs == Null<Real>() ? 0 : std::upper_bound(v.begin(), v.end(), xmin) - v.begin();
    counts.resize(steps);
    bounds.resize(steps);
    for (Size i = 0; i < steps; ++i) {
        Real v1 = xmin + static_cast<Real>(i + 1) * h;
        // if all data should be covered, ensure the counts sum up to v.size()
        Size idx1 = coveredStdDevs == Null<Real>() && i == steps - 1
                        ? v.size()
                        : std::upper_bound(v.begin(), v.end(), v1) - v.begin();
        counts[i] = idx1 - idx0;
        bounds[i] = v1;
        idx0 = idx1;
    }
}

} // namespace QuantExt
