/*
 Copyright (C) 2021 Quaternion Risk Management Ltd
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

#include <ored/scripting/models/modelcg.hpp>

#include <qle/ad/computationgraph.hpp>

namespace ore {
namespace data {

ModelCG::ModelCG(const QuantLib::Size n) : n_(n) { g_ = QuantLib::ext::make_shared<QuantExt::ComputationGraph>(); }

std::size_t ModelCG::dt(const Date& d1, const Date& d2) const {
    return cg_const(*g_, QuantLib::ActualActual(QuantLib::ActualActual::ISDA).yearFraction(d1, d2));
}

void ModelCG::useStickyCloseOutDates(const bool b) const {
    QL_FAIL("ModelCG::useStickyCloseOutDates(): not supported by this model instance");
}

std::tuple<QuantLib::Date, QuantLib::Date, std::size_t, std::size_t>
ModelCG::getInterpolationWeights(const QuantLib::Date& d, const std::set<Date>& knownDates) const {
    auto u = std::upper_bound(knownDates.begin(), knownDates.end(), d);
    QL_REQUIRE(u != knownDates.begin(), "getInterpolationWeights(" << d << "): outside knownDates range");
    auto l = std::next(u, -1);
    Real tmp = static_cast<double>(d - *l) / static_cast<double>(*u - *l);
    std::size_t w1 = cg_const(*g_, 1.0 - tmp);
    std::size_t w2 = cg_const(*g_, tmp);
    return std::make_tuple(*l, *u, w1, w2);
}

std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>
ModelCG::getInterpolationWeights(const double t, const QuantLib::TimeGrid& knownTimes) const {
    auto u = std::upper_bound(knownTimes.begin(), knownTimes.end(), t);
    QL_REQUIRE(u != knownTimes.begin(), "getInterpolationWeights(" << t << "): outside knownTimes range");
    auto l = std::next(u, -1);
    Real tmp = (t - *l) / (*u - *l);
    std::size_t w1 = cg_const(*g_, 1.0 - tmp);
    std::size_t w2 = cg_const(*g_, tmp);
    return std::make_tuple(std::distance(knownTimes.begin(), l), std::distance(knownTimes.begin(), u), w1, w2);
}

ModelCG::ModelParameter::ModelParameter(const Type type, const std::string& qualifier, const std::string& qualifier2,
                                        const QuantLib::Date& date, const QuantLib::Date& date2,
                                        const QuantLib::Date& date3, const std::size_t index, const std::size_t index2,
                                        const std::size_t hash, const double time)
    : type_(type), qualifier_(qualifier), qualifier2_(qualifier2), date_(date), date2_(date2), date3_(date3),
      index_(index), index2_(index2), hash_(hash), time_(time) {}

bool operator==(const ModelCG::ModelParameter& x, const ModelCG::ModelParameter& y) {
    return x.type_ == y.type_ && x.qualifier_ == y.qualifier_ && x.qualifier2_ == y.qualifier2_ && x.date_ == y.date_ &&
           x.date2_ == y.date2_ && x.date3_ == y.date3_ && x.index_ == y.index_ && x.index2_ == y.index2_ &&
           x.hash_ == y.hash_ && x.time_ == y.time_;
}

bool operator<(const ModelCG::ModelParameter& x, const ModelCG::ModelParameter& y) {
    return std::tie(x.type_, x.qualifier_, x.qualifier2_, x.date_, x.date2_, x.date3_, x.index_, x.index2_, x.hash_,
                    x.time_) < std::tie(y.type_, y.qualifier_, y.qualifier2_, y.date_, y.date2_, x.date3_, y.index_,
                                        y.index2_, y.hash_, y.time_);
}

std::size_t ModelCG::addModelParameter(const ModelParameter& p, const std::function<double(void)>& f) const {
    return ::ore::data::addModelParameter(*g_, modelParameters_, p, f);
}

std::size_t addModelParameter(QuantExt::ComputationGraph& g, std::set<ModelCG::ModelParameter>& modelParameters,
                              const ModelCG::ModelParameter& p, const std::function<double(void)>& f) {
    if (auto m = modelParameters.find(p); m != modelParameters.end()) {
        return m->node();
    } else {
        std::size_t node = cg_insert(g);
        p.setFunctor(f);
        p.setNode(node);
        modelParameters.insert(p);
        return node;
    }
}

std::ostream& operator<<(std::ostream& o, const ModelCG::ModelParameter::Type& t) {
    switch (t) {
    case ModelCG::ModelParameter::Type::none:
        return o << "none";
    case ModelCG::ModelParameter::Type::fix:
        return o << "fix";
    case ModelCG::ModelParameter::Type::complexRate:
        return o << "complexRate";
    case ModelCG::ModelParameter::Type::dsc:
        return o << "dsc";
    case ModelCG::ModelParameter::Type::fwdCompAvg:
        return o << "fwdCompAvg";
    case ModelCG::ModelParameter::Type::div:
        return o << "div";
    case ModelCG::ModelParameter::Type::rfr:
        return o << "rfr";
    case ModelCG::ModelParameter::Type::defaultProb:
        return o << "defaultProb";
    case ModelCG::ModelParameter::Type::lgm_H:
        return o << "lgm_H";
    case ModelCG::ModelParameter::Type::lgm_Hprime:
        return o << "lgm_Hprime";
    case ModelCG::ModelParameter::Type::lgm_zeta:
        return o << "lgm_zeta";
    case ModelCG::ModelParameter::Type::lgm_numeraire:
        return o << "lgm_numeraire";
    case ModelCG::ModelParameter::Type::lgm_discountBond:
        return o << "lgm_discountBond";
    case ModelCG::ModelParameter::Type::lgm_reducedDiscountBond:
        return o << "lgm_reducedDiscountBond";
    case ModelCG::ModelParameter::Type::fxbs_sigma:
        return o << "fxbs_sigma";
    case ModelCG::ModelParameter::Type::logX0:
        return o << "logX0";
    case ModelCG::ModelParameter::Type::logFxSpot:
        return o << "logFxSpot";
    case ModelCG::ModelParameter::Type::sqrtCorr:
        return o << "sqrtCorr";
    case ModelCG::ModelParameter::Type::sqrtCov:
        return o << "sqrtCov";
    case ModelCG::ModelParameter::Type::corr:
        return o << "corr";
    case ModelCG::ModelParameter::Type::cov:
        return o << "cov";
    case ModelCG::ModelParameter::Type::cam_corrzz:
        return o << "cam_corrzz";
    case ModelCG::ModelParameter::Type::cam_corrzx:
        return o << "cam_corrzx";
    case ModelCG::ModelParameter::Type::interpolated_undpath:
        return o << "interpolated_undpath";
    case ModelCG::ModelParameter::Type::interpolated_irstate:
        return o << "interpolated_irstate";
    }
    return o << "unknown";
}

std::ostream& operator<<(std::ostream& o, const ModelCG::ModelParameter& p) {
    return o << "(" << p.type() << "," << p.qualifier() << "," << p.qualifier2() << ","
             << QuantLib::io::iso_date(p.date()) << "," << QuantLib::io::iso_date(p.date2()) << ","
             << QuantLib::io::iso_date(p.date3()) << "," << p.index() << "," << p.index2() << "," << p.time() << ","
             << p.hash() << ")";
}

} // namespace data
} // namespace ore
