/*
  Copyright (C) 2021 Skandinaviska Enskilda Banken AB (publ)
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

 #include "toplevelfixture.hpp"
 #include <boost/test/unit_test.hpp>
 #include <ql/termstructures/yield/zerocurve.hpp>
 #include <qle/math/continuousinterpolation.hpp>

 #include <boost/make_shared.hpp>

 using namespace boost::unit_test_framework;
 using namespace QuantExt;
 using namespace QuantLib;

 BOOST_FIXTURE_TEST_SUITE(QuantExtTestSuite, qle::test::TopLevelFixture)

 BOOST_AUTO_TEST_SUITE(ContinuousInterpolationTest)

 BOOST_AUTO_TEST_CASE(testContinuousInterpolation) {

     BOOST_TEST_MESSAGE("Testing QuantExt ContinuousInterpolation");

     SavedSettings backup;
     Settings::instance().evaluationDate() = Date(19, Jul, 2024);
     Date today = Settings::instance().evaluationDate();

     std::vector<Date> dates = {Date(22, Jul, 2024), Date(22, Jan, 2025), Date(22, Jul, 2025), Date(22, Jul, 2026)};
     std::vector<Rate> zeros = {0.03, 0.035, 0.04, 0.05};

     std::vector<Time> times(dates.size());
     for (Size i = 0; i < dates.size(); ++i)
         times[i] = Actual365Fixed().yearFraction(today, dates[i]);
     ContinuousInterpolation q(times.begin(), times.end(), zeros.begin());

     BOOST_TEST_MESSAGE("Interpolation should be exact at pillars");
     for (Size i = 0; i < zeros.size(); ++i) {
         BOOST_CHECK_CLOSE(q(times[i]), zeros[i], 0.0000001);
     }
 }

 BOOST_AUTO_TEST_SUITE_END()

 BOOST_AUTO_TEST_SUITE_END()
