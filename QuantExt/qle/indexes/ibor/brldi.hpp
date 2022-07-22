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

/*! \file brldi.hpp
\brief %BRLDI rate
*/

#ifndef quantlib_brldi_hpp
#define quantlib_brldi_hpp

#include <ql/indexes/iborindex.hpp>
#include <ql/time/calendars/brazil.hpp>
#include <ql/time/daycounters/business252.hpp>

namespace QuantExt {

//! %BRLDI rate
/*! Brazil Interbank Offered Rate.

*/
class BRLDi : public QuantLib::IborIndex {
public:
    BRLDi(const QuantLib::Period& tenor,
          const QuantLib::Handle<QuantLib::YieldTermStructure>& h = QuantLib::Handle<YieldTermStructure>())
        : QuantLib::IborIndex("BRLDI", tenor, (tenor == 1 * QuantLib::Days ? 0 : 2), QuantLib::BRLCurrency(),
                              QuantLib::Brazil(),
                              QuantLib::ModifiedFollowing,
                              false, QuantLib::Business252(), h) {}
};

} // namespace QuantExt

#endif
