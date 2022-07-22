/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
Copyright (C) 2018 Matthias Lungwitz

This file is part of QuantLib, a free-software/open-source library
for financial quantitative analysts and developers - http://quantlib.org/

QuantLib is free software: you can redistribute it and/or modify it
under the terms of the QuantLib license.  You should have received a
copy of the license along with this program; if not, please email
<quantlib-dev@lists.sf.net>. The license is also available online at
<http://quantlib.org/license.shtml>.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

/*! \file reibor.hpp
\brief %REIBOR rate
*/

#ifndef quantlib_reibor_hpp
#define quantlib_reibor_hpp

#include <ql/indexes/iborindex.hpp>
#include <ql/time/calendars/iceland.hpp>
#include <ql/currencies/europe.hpp>

namespace QuantExt {

//! %REIBOR rate
/*! Iceland Interbank Offered Rate.

*/
class Reibor : public QuantLib::IborIndex {
public:
    Reibor(const QuantLib::Period& tenor,
          const QuantLib::Handle<QuantLib::YieldTermStructure>& h = QuantLib::Handle<YieldTermStructure>())
        : QuantLib::IborIndex("REIBOR", tenor, (tenor == 1 * QuantLib::Days ? 0 : 2), QuantLib::ISKCurrency(),
                              QuantLib::Iceland(), QuantLib::ModifiedFollowing, false, QuantLib::Actual360(), h) {}
};

} // namespace QuantExt

#endif
