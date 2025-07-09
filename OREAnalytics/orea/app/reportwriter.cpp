/*
 Copyright (C) 2017 Quaternion Risk Management Ltd
 Copyright (C) 2017 Aareal Bank AG

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

#include <orea/app/reportwriter.hpp>
#include <orea/app/structuredanalyticserror.hpp>
#include <orea/simm/utilities.hpp>
#include <orea/scenario/scenariowriter.hpp>
#include <orea/engine/cashflowreportgenerator.hpp>

#include <ored/utilities/marketdata.hpp>
#include <ored/portfolio/structuredtradeerror.hpp>
#include <ored/utilities/to_string.hpp>

#include <qle/currencies/currencycomparator.hpp>
#include <qle/math/distributioncount.hpp>

#include <ql/cashflows/floatingratecoupon.hpp>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/framework/accumulator_set.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/accumulators/statistics/count.hpp>
#include <boost/accumulators/statistics/covariance.hpp>
#include <boost/accumulators/statistics/kurtosis.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/skewness.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/tail_quantile.hpp>
#include <boost/accumulators/statistics/variates/covariate.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptor/indexed.hpp>

#include <ostream>
#include <stdio.h>

using ore::data::to_string;
using QuantLib::Date;
using std::string;
using std::vector;

namespace ore {
namespace analytics {

typedef std::map<Currency, Matrix, CurrencyComparator> result_type_matrix;
typedef std::map<Currency, std::vector<Real>, CurrencyComparator> result_type_vector;
typedef std::map<Currency, Real, CurrencyComparator> result_type_scalar;

void ReportWriter::writeNpv(ore::data::Report& report, const std::string& baseCurrency,
                            QuantLib::ext::shared_ptr<Market> market, const std::string& configuration,
                            QuantLib::ext::shared_ptr<Portfolio> portfolio) {
    LOG("portfolio valuation");
    DayCounter dc = ActualActual(ActualActual::ISDA);
    Date today = Settings::instance().evaluationDate();
    report.addColumn("TradeId", string())
        .addColumn("TradeType", string())
        .addColumn("Maturity", Date())
        .addColumn("MaturityTime", double(), 6)
        .addColumn("NPV", double(), 6)
        .addColumn("NpvCurrency", string())
        .addColumn("NPV(Base)", double(), 6)
        .addColumn("BaseCurrency", string())
        .addColumn("Notional", double(), 2)
        .addColumn("NotionalCurrency", string())
        .addColumn("Notional(Base)", double(), 2)
        .addColumn("NettingSet", string())
        .addColumn("CounterParty", string());
    for (auto & [tradeId, trade] : portfolio->trades()) {
        try {
            string npvCcy = trade->npvCurrency();
            Real fx = 1.0, fxNotional = 1.0;
            if (npvCcy != baseCurrency)
                fx = market->fxRate(npvCcy + baseCurrency, configuration)->value();
            if (trade->notionalCurrency() != "" && trade->notionalCurrency() != baseCurrency)
                fxNotional = market->fxRate(trade->notionalCurrency() + baseCurrency, configuration)->value();
            Real npv = trade->instrument()->NPV();
            QL_REQUIRE(std::isfinite(npv), "npv is not finite (" << npv << ")");
            Date maturity = trade->maturity();
            report.next()
                .add(trade->id())
                .add(trade->tradeType())
                .add(maturity)
                .add(maturity == QuantLib::Null<Date>() ? Null<Real>() : dc.yearFraction(today, maturity))
                .add(npv)
                .add(npvCcy)
                .add(npv * fx)
                .add(baseCurrency)
                .add(trade->notional())
                .add(trade->notionalCurrency() == "" ? nullString_ : trade->notionalCurrency())
                .add(trade->notional() == Null<Real>() || trade->notionalCurrency() == ""
                         ? Null<Real>()
                         : trade->notional() * fxNotional)
                .add(trade->envelope().nettingSetId())
                .add(trade->envelope().counterparty());
        } catch (std::exception& e) {
            StructuredTradeErrorMessage(trade->id(), trade->tradeType(), "Error during trade pricing", e.what()).log();
            Date maturity = trade->maturity();
            report.next()
                .add(trade->id())
                .add(trade->tradeType())
                .add(maturity)
                .add(maturity == QuantLib::Null<Date>() ? Null<Real>() : dc.yearFraction(today, maturity))
                .add(Null<Real>())
                .add(nullString_)
                .add(Null<Real>())
                .add(nullString_)
                .add(Null<Real>())
                .add(nullString_)
                .add(Null<Real>())
                .add(nullString_)
                .add(nullString_);
        }
    }
    report.end();
    LOG("NPV file written");
}

void ReportWriter::writeCashflow(ore::data::Report& report, const std::string& baseCurrency,
                                 QuantLib::ext::shared_ptr<ore::data::Portfolio> portfolio,
                                 QuantLib::ext::shared_ptr<ore::data::Market> market, const std::string& configuration,
                                 const bool includePastCashflows) {

    LOG("Writing cashflow report");

    report.addColumn("TradeId", string())
        .addColumn("Type", string())
        .addColumn("CashflowNo", Size())
        .addColumn("LegNo", Size())
        .addColumn("PayDate", Date())
        .addColumn("FlowType", string())
        .addColumn("Amount", double(), 4)
        .addColumn("Currency", string())
        .addColumn("Coupon", double(), 10)
        .addColumn("Accrual", double(), 10)
        .addColumn("AccrualStartDate", Date(), 4)
        .addColumn("AccrualEndDate", Date(), 4)
        .addColumn("AccruedAmount", double(), 4)
        .addColumn("fixingDate", Date())
        .addColumn("fixingValue", double(), 10)
        .addColumn("Notional", double(), 4)
        .addColumn("DiscountFactor", double(), 10)
        .addColumn("PresentValue", double(), 10)
        .addColumn("FXRate(Local-Base)", double(), 10)
        .addColumn("PresentValue(Base)", double(), 10)
        .addColumn("BaseCurrency", string())
        .addColumn("FloorStrike", double(), 10)
        .addColumn("CapStrike", double(), 10)
        .addColumn("FloorVolatility", double(), 10)
        .addColumn("CapVolatility", double(), 10)
        .addColumn("EffectiveFloorVolatility", double(), 10)
        .addColumn("EffectiveCapVolatility", double(), 10);

    for (auto [tradeId, trade]: portfolio->trades()) {

        if (!trade->hasCashflows()) {
            DLOG("cashflow for " << trade->tradeType() << " " << trade->id() << " skipped");
            continue;
        }

        try {

            auto data = generateCashflowReportData(trade, baseCurrency, market, configuration, includePastCashflows);

            for(auto const& d: data) {
                    report.next()
                        .add(trade->id())
                        .add(trade->tradeType())
                        .add(d.cashflowNo)
                        .add(d.legNo)
                        .add(d.payDate)
                        .add(d.flowType)
                        .add(d.amount)
                        .add(d.currency)
                        .add(d.coupon)
                        .add(d.accrual)
                        .add(d.accrualStartDate)
                        .add(d.accrualEndDate)
                        .add(d.accruedAmount)
                        .add(d.fixingDate)
                        .add(d.fixingValue)
                        .add(d.notional)
                        .add(d.discountFactor)
                        .add(d.presentValue)
                        .add(d.fxRateLocalBase)
                        .add(d.presentValueBase)
                        .add(d.baseCurrency)
                        .add(d.floorStrike)
                        .add(d.capStrike)
                        .add(d.floorVolatility)
                        .add(d.capVolatility)
                        .add(d.effectiveFloorVolatility)
                        .add(d.effectiveCapVolatility);
            }

        } catch (std::exception& e) {
            StructuredTradeErrorMessage(trade->id(), trade->tradeType(), "Error during cashflow report generation",
                                        e.what())
                .log();
        }
    }

    report.end();
    LOG("Cashflow report written");
}

void ReportWriter::writeCashflowNpv(ore::data::Report& report, const ore::data::InMemoryReport& cashflowReport,
                                    QuantLib::ext::shared_ptr<ore::data::Market> market, const std::string& configuration,
                                    const std::string& baseCcy, const Date& horizon) {
    // Pick the following fields form the in memory report:
    // - tradeId 
    // - payment date 
    // - currency 
    // - present value 
    // Then convert PVs into base currency, aggrate per trade if payment date is within the horizon
    // Write the resulting aggregate PV per trade into the report.

    Size tradeIdColumn = 0;
    Size tradeTypeColumn = 1;
    Size payDateColumn = 4;
    Size ccyColumn = 7;
    Size pvColumn = 17;
    QL_REQUIRE(cashflowReport.header(tradeIdColumn) == "TradeId", "incorrect trade id column " << tradeIdColumn);
    QL_REQUIRE(cashflowReport.header(tradeTypeColumn) == "Type", "incorrect trade type column " << tradeTypeColumn);
    QL_REQUIRE(cashflowReport.header(payDateColumn) == "PayDate", "incorrect payment date column " << payDateColumn);
    QL_REQUIRE(cashflowReport.header(ccyColumn) == "Currency", "incorrect currency column " << ccyColumn);
    QL_REQUIRE(cashflowReport.header(pvColumn) == "PresentValue", "incorrect pv column " << pvColumn);
    
    map<string, Real> npvMap;
    Date asof = Settings::instance().evaluationDate();
    for (Size i = 0; i < cashflowReport.rows(); ++i) {
        string tradeId = boost::get<string>(cashflowReport.data(tradeIdColumn, i));
        string tradeType = boost::get<string>(cashflowReport.data(tradeTypeColumn, i));
        Date payDate = boost::get<Date>(cashflowReport.data(payDateColumn, i));
        string ccy = boost::get<string>(cashflowReport.data(ccyColumn, i));
        Real pv = boost::get<Real>(cashflowReport.data(pvColumn, i));
        Real fx = 1.0;
    // There shouldn't be entries in the cf report without ccy. We assume ccy = baseCcy in this case and log an error.
        if (ccy.empty()) {
            StructuredTradeErrorMessage(tradeId, tradeType, "Error during CashflowNpv calculation.",
                                        "Cashflow in row " + std::to_string(i) +
                                            " has no ccy. Assuming ccy = baseCcy = " + baseCcy + ".")
                .log();
        }
        if (!ccy.empty() && ccy != baseCcy)
            fx = market->fxRate(ccy + baseCcy, configuration)->value();
        if (npvMap.find(tradeId) == npvMap.end())
            npvMap[tradeId] = 0.0;
        if (payDate > asof && payDate <= horizon) {
            npvMap[tradeId] += pv * fx;
            DLOG("Cashflow NPV for trade " << tradeId << ": pv " << pv << " fx " << fx << " sum " << npvMap[tradeId]);
        }   
    }   

    LOG("Writing cashflow NPV report for " << asof);
    report.addColumn("TradeId", string())
        .addColumn("PresentValue", double(), 10)
        .addColumn("BaseCurrency", string())
        .addColumn("Horizon", string());        

    for (auto r: npvMap)
        report.next().add(r.first).add(r.second).add(baseCcy).add(horizon < Date::maxDate() ? ore::data::to_string(horizon) : "infinite");

    report.end();
    LOG("Cashflow NPV report written");
}

void ReportWriter::writeCurves(ore::data::Report& report, const std::string& configID, const DateGrid& grid,
                               const TodaysMarketParameters& marketConfig, const QuantLib::ext::shared_ptr<Market>& market,
                               const bool continueOnError) {
    LOG("Write curves... ");

    QL_REQUIRE(marketConfig.hasConfiguration(configID), "curve configuration " << configID << " not found");

    map<string, string> discountCurves = marketConfig.mapping(MarketObject::DiscountCurve, configID);
    map<string, string> YieldCurves = marketConfig.mapping(MarketObject::YieldCurve, configID);
    map<string, string> indexCurves = marketConfig.mapping(MarketObject::IndexCurve, configID);
    map<string, string> zeroInflationIndices, defaultCurves;
    if (marketConfig.hasMarketObject(MarketObject::ZeroInflationCurve))
        zeroInflationIndices = marketConfig.mapping(MarketObject::ZeroInflationCurve, configID);
    if (marketConfig.hasMarketObject(MarketObject::DefaultCurve))
        defaultCurves = marketConfig.mapping(MarketObject::DefaultCurve, configID);

    vector<Handle<YieldTermStructure>> yieldCurves;
    vector<Handle<ZeroInflationIndex>> zeroInflationFixings;
    vector<Handle<DefaultProbabilityTermStructure>> probabilityCurves;

    report.addColumn("Tenor", Period()).addColumn("Date", Date());

    for (auto it : discountCurves) {
        DLOG("discount curve - " << it.first);
        try {
            yieldCurves.push_back(market->discountCurve(it.first, configID));
            report.addColumn(it.first, double(), 15);
        } catch (const std::exception& e) {
            if (continueOnError) {
                WLOG("skip this curve: " << e.what());
            } else {
                QL_FAIL(e.what());
            }
        }
    }
    for (auto it : YieldCurves) {
        DLOG("yield curve - " << it.first);
        try {
            yieldCurves.push_back(market->yieldCurve(it.first, configID));
            report.addColumn(it.first, double(), 15);
        } catch (const std::exception& e) {
            if (continueOnError) {
                WLOG("skip this curve: " << e.what());
            } else {
                QL_FAIL(e.what());
            }
        }
    }
    for (auto it : indexCurves) {
        DLOG("index curve - " << it.first);
        try {
            yieldCurves.push_back(market->iborIndex(it.first, configID)->forwardingTermStructure());
            report.addColumn(it.first, double(), 15);
        } catch (const std::exception& e) {
            if (continueOnError) {
                WLOG("skip this curve: " << e.what());
            } else {
                QL_FAIL(e.what());
            }
        }
    }
    for (auto it : zeroInflationIndices) {
        DLOG("inflation curve - " << it.first);
        try {
            zeroInflationFixings.push_back(market->zeroInflationIndex(it.first, configID));
            report.addColumn(it.first, double(), 15);
        } catch (const std::exception& e) {
            if (continueOnError) {
                WLOG("skip this curve: " << e.what());
            } else {
                QL_FAIL(e.what());
            }
        }
    }
    for (auto it : defaultCurves) {
        DLOG("default curve - " << it.first);
        try {
            probabilityCurves.push_back(market->defaultCurve(it.first, configID)->curve());
            report.addColumn(it.first, double(), 15);
        } catch (const std::exception& e) {
            if (continueOnError) {
                WLOG("skip this curve: " << e.what());
            } else {
                QL_FAIL(e.what());
            }
        }
    }

    for (Size j = 0; j < grid.size(); ++j) {
        Date date = grid[j];
        report.next().add(grid.tenors()[j]).add(date);
        for (Size i = 0; i < yieldCurves.size(); ++i)
            report.add(yieldCurves[i]->discount(date));
        for (Size i = 0; i < zeroInflationFixings.size(); ++i)
            report.add(zeroInflationFixings[i]->fixing(date));
        for (Size i = 0; i < probabilityCurves.size(); ++i)
            report.add(probabilityCurves[i]->survivalProbability(date));
    }
    report.end();
}

void ReportWriter::writeTradeExposures(ore::data::Report& report, QuantLib::ext::shared_ptr<PostProcess> postProcess,
                                       const string& tradeId) {
    const vector<Date> dates = postProcess->cube()->dates();
    Date today = Settings::instance().evaluationDate();
    DayCounter dc = ActualActual(ActualActual::ISDA);
    const vector<Real>& epe = postProcess->tradeEPE(tradeId);
    const vector<Real>& ene = postProcess->tradeENE(tradeId);
    const vector<Real>& ee_b = postProcess->tradeEE_B(tradeId);
    const vector<Real>& eee_b = postProcess->tradeEEE_B(tradeId);
    const vector<Real>& pfe = postProcess->tradePFE(tradeId);
    const vector<Real>& aepe = postProcess->allocatedTradeEPE(tradeId);
    const vector<Real>& aene = postProcess->allocatedTradeENE(tradeId);
    const vector<Real>& epe_b = postProcess->tradeEPE_B_timeWeighted(tradeId);
    const vector<Real>& eepe_b = postProcess->tradeEEPE_B_timeWeighted(tradeId);
    report.addColumn("TradeId", string())
        .addColumn("Date", Date())
        .addColumn("Time", double(), 6)
        .addColumn("EPE", double())
        .addColumn("ENE", double())
        .addColumn("AllocatedEPE", double())
        .addColumn("AllocatedENE", double())
        .addColumn("PFE", double())
        .addColumn("BaselEE", double())
        .addColumn("BaselEEE", double())
        .addColumn("TimeWeightedBaselEPE", double(), 2)
        .addColumn("TimeWeightedBaselEEPE", double(), 2);
    report.next()
        .add(tradeId)
        .add(today)
        .add(0.0)
        .add(epe[0])
        .add(ene[0])
        .add(aepe[0])
        .add(aene[0])
        .add(pfe[0])
        .add(ee_b[0])
        .add(eee_b[0])
        .add(epe_b[0])
        .add(eepe_b[0]);
    for (Size j = 0; j < dates.size(); ++j) {

        Time time = dc.yearFraction(today, dates[j]);
        report.next()
            .add(tradeId)
            .add(dates[j])
            .add(time)
            .add(epe[j + 1])
            .add(ene[j + 1])
            .add(aepe[j + 1])
            .add(aene[j + 1])
            .add(pfe[j + 1])
            .add(ee_b[j + 1])
            .add(eee_b[j + 1])
            .add(epe_b[j + 1])
            .add(eepe_b[j + 1]);
    }
    report.end();
}

void addNettingSetExposure(ore::data::Report& report, QuantLib::ext::shared_ptr<PostProcess> postProcess,
                           const string& nettingSetId) {
    const vector<Date> dates = postProcess->cube()->dates();
    Date today = Settings::instance().evaluationDate();
    DayCounter dc = ActualActual(ActualActual::ISDA);
    const vector<Real>& epe = postProcess->netEPE(nettingSetId);
    const vector<Real>& ene = postProcess->netENE(nettingSetId);
    const vector<Real>& ee_b = postProcess->netEE_B(nettingSetId);
    const vector<Real>& eee_b = postProcess->netEEE_B(nettingSetId);
    const vector<Real>& pfe = postProcess->netPFE(nettingSetId);
    const vector<Real>& ecb = postProcess->expectedCollateral(nettingSetId);
    const vector<Real>& epe_b = postProcess->netEPE_B_timeWeighted(nettingSetId);
    const vector<Real>& eepe_b = postProcess->netEEPE_B_timeWeighted(nettingSetId);
    report.next()
        .add(nettingSetId)
        .add(today)
        .add(0.0)
        .add(epe[0])
        .add(ene[0])
        .add(pfe[0])
        .add(ecb[0])
        .add(ee_b[0])
        .add(eee_b[0])
        .add(epe_b[0])
        .add(eepe_b[0]);
    for (Size j = 0; j < dates.size(); ++j) {
        Real time = dc.yearFraction(today, dates[j]);
        report.next()
            .add(nettingSetId)
            .add(dates[j])
            .add(time)
            .add(epe[j + 1])
            .add(ene[j + 1])
            .add(pfe[j + 1])
            .add(ecb[j + 1])
            .add(ee_b[j + 1])
            .add(eee_b[j + 1])
            .add(epe_b[j + 1])
            .add(eepe_b[j + 1]);
    }
}

void ReportWriter::writeNettingSetExposures(ore::data::Report& report, QuantLib::ext::shared_ptr<PostProcess> postProcess,
                                            const string& nettingSetId) {
    report.addColumn("NettingSet", string())
        .addColumn("Date", Date())
        .addColumn("Time", double(), 6)
        .addColumn("EPE", double(), 2)
        .addColumn("ENE", double(), 2)
        .addColumn("PFE", double(), 2)
        .addColumn("ExpectedCollateral", double(), 2)
        .addColumn("BaselEE", double(), 2)
        .addColumn("BaselEEE", double(), 2)
        .addColumn("TimeWeightedBaselEPE", double(), 2)
        .addColumn("TimeWeightedBaselEEPE", double(), 2);
    addNettingSetExposure(report, postProcess, nettingSetId);
    report.end();
}

void ReportWriter::writeNettingSetExposures(ore::data::Report& report, QuantLib::ext::shared_ptr<PostProcess> postProcess) {
    report.addColumn("NettingSet", string())
        .addColumn("Date", Date())
        .addColumn("Time", double(), 6)
        .addColumn("EPE", double(), 2)
        .addColumn("ENE", double(), 2)
        .addColumn("PFE", double(), 2)
        .addColumn("ExpectedCollateral", double(), 2)
        .addColumn("BaselEE", double(), 2)
        .addColumn("BaselEEE", double(), 2);

    for (const auto& [n,_] : postProcess->nettingSetIds()) {
        addNettingSetExposure(report, postProcess, n);
    }
    report.end();
}

void ReportWriter::writeNettingSetCvaSensitivities(ore::data::Report& report,
                                                   QuantLib::ext::shared_ptr<PostProcess> postProcess,
                                                   const string& nettingSetId) {
    const vector<Real> grid = postProcess->spreadSensitivityTimes();
    const vector<Real>& sensiHazardRate = postProcess->netCvaHazardRateSensitivity(nettingSetId);
    const vector<Real>& sensiCdsSpread = postProcess->netCvaSpreadSensitivity(nettingSetId);
    report.addColumn("NettingSet", string())
        .addColumn("Time", double(), 6)
        .addColumn("CvaHazardRateSensitivity", double(), 6)
        .addColumn("CvaSpreadSensitivity", double(), 6);

    if (sensiHazardRate.size() == 0 || sensiCdsSpread.size() == 0)
        return;

    for (Size j = 0; j < grid.size(); ++j) {
        report.next().add(nettingSetId).add(grid[j]).add(sensiHazardRate[j]).add(sensiCdsSpread[j]);
    }
    report.end();
}

void ReportWriter::writeXVA(ore::data::Report& report, const string& allocationMethod,
                            QuantLib::ext::shared_ptr<Portfolio> portfolio, QuantLib::ext::shared_ptr<PostProcess> postProcess) {
    const vector<Date> dates = postProcess->cube()->dates();
    DayCounter dc = ActualActual(ActualActual::ISDA);
    Size precision = 2;
    report.addColumn("TradeId", string())
        .addColumn("NettingSetId", string())
        .addColumn("CVA", double(), precision)
        .addColumn("DVA", double(), precision)
        .addColumn("FBA", double(), precision)
        .addColumn("FCA", double(), precision)
        .addColumn("FBAexOwnSP", double(), precision)
        .addColumn("FCAexOwnSP", double(), precision)
        .addColumn("FBAexAllSP", double(), precision)
        .addColumn("FCAexAllSP", double(), precision)
        .addColumn("COLVA", double(), precision)
        .addColumn("MVA", double(), precision)
        .addColumn("OurKVACCR", double(), precision)
        .addColumn("TheirKVACCR", double(), precision)
        .addColumn("OurKVACVA", double(), precision)
        .addColumn("TheirKVACVA", double(), precision)
        .addColumn("CollateralFloor", double(), precision)
        .addColumn("AllocatedCVA", double(), precision)
        .addColumn("AllocatedDVA", double(), precision)
        .addColumn("AllocationMethod", string())
        .addColumn("BaselEPE", double(), precision)
        .addColumn("BaselEEPE", double(), precision);

    for (const auto& [n, _] : postProcess->nettingSetIds()) {
        try {
            postProcess->nettingSetCVA(n);
            report.next()
                .add("")
                .add(n)
                .add(postProcess->nettingSetCVA(n))
                .add(postProcess->nettingSetDVA(n))
                .add(postProcess->nettingSetFBA(n))
                .add(postProcess->nettingSetFCA(n))
                .add(postProcess->nettingSetFBA_exOwnSP(n))
                .add(postProcess->nettingSetFCA_exOwnSP(n))
                .add(postProcess->nettingSetFBA_exAllSP(n))
                .add(postProcess->nettingSetFCA_exAllSP(n))
                .add(postProcess->nettingSetCOLVA(n))
                .add(postProcess->nettingSetMVA(n))
                .add(postProcess->nettingSetOurKVACCR(n))
                .add(postProcess->nettingSetTheirKVACCR(n))
                .add(postProcess->nettingSetOurKVACVA(n))
                .add(postProcess->nettingSetTheirKVACVA(n))
                .add(postProcess->nettingSetCollateralFloor(n))
                .add(postProcess->nettingSetCVA(n))
                .add(postProcess->nettingSetDVA(n))
                .add(allocationMethod)
                .add(postProcess->netEPE_B(n))
                .add(postProcess->netEEPE_B(n));
        } catch (const std::exception& e) {
            StructuredAnalyticsErrorMessage("XVA Report", "Error during writing xva for netting set.", e.what(),
                                            {{"nettingSetId", n}})
                .log();
        }

        for (auto& [tid, trade] : portfolio->trades()) {

            string nid = trade->envelope().nettingSetId();
            if (nid != n)
                continue;
            try {
                postProcess->tradeCVA(tid);
                report.next()
                    .add(tid)
                    .add(nid)
                    .add(postProcess->tradeCVA(tid))
                    .add(postProcess->tradeDVA(tid))
                    .add(postProcess->tradeFBA(tid))
                    .add(postProcess->tradeFCA(tid))
                    .add(postProcess->tradeFBA_exOwnSP(tid))
                    .add(postProcess->tradeFCA_exOwnSP(tid))
                    .add(postProcess->tradeFBA_exAllSP(tid))
                    .add(postProcess->tradeFCA_exAllSP(tid))
                    .add(Null<Real>())
                    .add(Null<Real>())
                    .add(Null<Real>())
                    .add(Null<Real>())
                    .add(Null<Real>())
                    .add(Null<Real>())
                    .add(Null<Real>())
                    .add(postProcess->allocatedTradeCVA(tid))
                    .add(postProcess->allocatedTradeDVA(tid))
                    .add(allocationMethod)
                    .add(postProcess->tradeEPE_B(tid))
                    .add(postProcess->tradeEEPE_B(tid));
            } catch (const std::exception& e) {
                StructuredAnalyticsErrorMessage("XVA Report", "Error during writing xva for trade.", e.what(),
                                                {{"tradeId", n}})
                    .log();
            }
        }
    }
    report.end();
}

void ReportWriter::writeNettingSetColva(ore::data::Report& report, QuantLib::ext::shared_ptr<PostProcess> postProcess,
                                        const string& nettingSetId) {
    const vector<Date> dates = postProcess->cube()->dates();
    Date today = Settings::instance().evaluationDate();
    DayCounter dc = ActualActual(ActualActual::ISDA);
    const vector<Real>& collateral = postProcess->expectedCollateral(nettingSetId);
    const vector<Real>& colvaInc = postProcess->colvaIncrements(nettingSetId);
    const vector<Real>& floorInc = postProcess->collateralFloorIncrements(nettingSetId);
    Real colva = postProcess->nettingSetCOLVA(nettingSetId);
    Real floorValue = postProcess->nettingSetCollateralFloor(nettingSetId);

    report.addColumn("NettingSet", string())
        .addColumn("Date", Date())
        .addColumn("Time", double(), 4)
        .addColumn("CollateralBalance", double(), 4)
        .addColumn("COLVA Increment", double(), 4)
        .addColumn("COLVA", double(), 4)
        .addColumn("CollateralFloor Increment", double(), 4)
        .addColumn("CollateralFloor", double(), 4);

    report.next()
        .add(nettingSetId)
        .add(Null<Date>())
        .add(Null<Real>())
        .add(Null<Real>())
        .add(Null<Real>())
        .add(colva)
        .add(Null<Real>())
        .add(floorValue);
    Real colvaSum = 0.0;
    Real floorSum = 0.0;
    for (Size j = 0; j < dates.size(); ++j) {
        Real time = dc.yearFraction(today, dates[j]);
        colvaSum += colvaInc[j + 1];
        floorSum += floorInc[j + 1];
        report.next()
            .add(nettingSetId)
            .add(dates[j])
            .add(time)
            .add(collateral[j + 1])
            .add(colvaInc[j + 1])
            .add(colvaSum)
            .add(floorInc[j + 1])
            .add(floorSum);
    }
    report.end();
}

void ReportWriter::writeAggregationScenarioData(ore::data::Report& report, const AggregationScenarioData& data) {
    report.addColumn("Date", Size()).addColumn("Scenario", Size());
    for (auto const& k : data.keys()) {
        std::string tmp = ore::data::to_string(k.first) + k.second;
        report.addColumn(tmp.c_str(), double(), 8);
    }
    for (Size d = 0; d < data.dimDates(); ++d) {
        for (Size s = 0; s < data.dimSamples(); ++s) {
            report.next();
            report.add(d).add(s);
            for (auto const& k : data.keys()) {
                report.add(data.get(d, s, k.first, k.second));
            }
        }
    }
    report.end();
}

void ReportWriter::writeScenarioReport(ore::data::Report& report,
                                       const std::vector<QuantLib::ext::shared_ptr<SensitivityCube>>& sensitivityCubes,
                                       Real outputThreshold) {

    LOG("Writing Scenario report");

    report.addColumn("TradeId", string());
    report.addColumn("Factor", string());
    report.addColumn("Up/Down", string());
    report.addColumn("Base NPV", double(), 2);
    report.addColumn("ShiftSize_1", double(), 6);
    report.addColumn("ShiftSize_2", double(), 6);
    report.addColumn("Scenario NPV", double(), 2);
    report.addColumn("Difference", double(), 2);

    for (auto const& sensitivityCube : sensitivityCubes) {

        auto scenarioDescriptions = sensitivityCube->scenarioDescriptions();
        auto tradeIds = sensitivityCube->tradeIdx();
        auto npvCube = sensitivityCube->npvCube();

        for (const auto& [tradeId, i] : tradeIds) {

            Real baseNpv = npvCube->getT0(i);
            for (const auto& [j, scenarioNpv] : npvCube->getTradeNPVs(i)) {
                auto scenarioDescription = scenarioDescriptions[j];
                Real difference = scenarioNpv - baseNpv;
                Real shift1 = scenarioDescription.key1().keytype == RiskFactorKey::KeyType::None
                                  ? Null<Real>()
                                  : sensitivityCube->actualShiftSize(scenarioDescription.key1());
                Real shift2 = scenarioDescription.key2().keytype == RiskFactorKey::KeyType::None
                                  ? Null<Real>()
                                  : sensitivityCube->actualShiftSize(scenarioDescription.key2());
                if (fabs(difference) > outputThreshold) {
                    report.next();
                    report.add(tradeId);
                    report.add(prettyPrintInternalCurveName(scenarioDescription.factors()));
                    report.add(scenarioDescription.typeString());
                    report.add(baseNpv);
                    report.add(shift1);
                    report.add(shift2);
                    report.add(scenarioNpv);
                    report.add(difference);
                } else if (!std::isfinite(difference)) {
                    // TODO: is this needed?
                    ALOG("sensitivity scenario for trade " << tradeId << ", factor " << scenarioDescription.factors()
                                                           << " is not finite (" << difference << ")");
                }
            }
        }
    }

    report.end();
    LOG("Scenario report finished");
}

void ReportWriter::writeSensitivityReport(Report& report, const QuantLib::ext::shared_ptr<SensitivityStream>& ss,
                                          Real outputThreshold, Size outputPrecision) {

    LOG("Writing Sensitivity report");

    Size shiftSizePrecision = outputPrecision < 6 ? 6 : outputPrecision;
    Size amountPrecision = outputPrecision < 2 ? 2 : outputPrecision;

    report.addColumn("TradeId", string());
    report.addColumn("IsPar", string());
    report.addColumn("Factor_1", string());
    report.addColumn("ShiftSize_1", double(), shiftSizePrecision);
    report.addColumn("Factor_2", string());
    report.addColumn("ShiftSize_2", double(), shiftSizePrecision);
    report.addColumn("Currency", string());
    report.addColumn("Base NPV", double(), amountPrecision);
    report.addColumn("Delta", double(), amountPrecision);
    report.addColumn("Gamma", double(), amountPrecision);

    // Make sure that we are starting from the start
    ss->reset();
    while (SensitivityRecord sr = ss->next()) {
        if ((outputThreshold == Null<Real>()) || (fabs(sr.delta) > outputThreshold ||
            (sr.gamma != Null<Real>() && fabs(sr.gamma) > outputThreshold))) {
            report.next();
            report.add(sr.tradeId);
            report.add(ore::data::to_string(sr.isPar));
            report.add(prettyPrintInternalCurveName(QuantExt::reconstructFactor(sr.key_1, sr.desc_1)));
            report.add(sr.shift_1);
            report.add(prettyPrintInternalCurveName(QuantExt::reconstructFactor(sr.key_2, sr.desc_2)));
            report.add(sr.shift_2);
            report.add(sr.currency);
            report.add(sr.baseNpv);
            report.add(sr.delta);
            report.add(sr.gamma);
        } else if (!std::isfinite(sr.delta) || !std::isfinite(sr.gamma)) {
            // TODO: Again, is this needed?
            ALOG("sensitivity record has infinite values: " << sr);
        }
    }

    report.end();
    LOG("Sensitivity report finished");
}

void ReportWriter::writeXvaSensitivityReport(Report& report, const QuantLib::ext::shared_ptr<SensitivityStream>& ssTrades,
        const QuantLib::ext::shared_ptr<SensitivityStream>& ssNettingSets, const std::map<std::string, std::string>& tradeNettingSetMap, Real outputThreshold,
                                             Size outputPrecision) {

    LOG("Writing XVA Sensitivity report");

    Size shiftSizePrecision = outputPrecision < 6 ? 6 : outputPrecision;
    Size amountPrecision = outputPrecision < 2 ? 2 : outputPrecision;

    report.addColumn("NettingSetId", string());
    report.addColumn("TradeId", string());
    report.addColumn("IsPar", string());
    report.addColumn("Factor_1", string());
    report.addColumn("ShiftSize_1", double(), shiftSizePrecision);
    report.addColumn("Factor_2", string());
    report.addColumn("ShiftSize_2", double(), shiftSizePrecision);
    report.addColumn("Currency", string());
    report.addColumn("Base XVA", double(), amountPrecision);
    report.addColumn("Delta", double(), amountPrecision);
    report.addColumn("Gamma", double(), amountPrecision);

    // Make sure that we are starting from the start
    ssTrades->reset();
    while (SensitivityRecord sr = ssTrades->next()) {
        if ((outputThreshold == Null<Real>()) ||
            (fabs(sr.delta) > outputThreshold || (sr.gamma != Null<Real>() && fabs(sr.gamma) > outputThreshold))) {
            auto it = tradeNettingSetMap.find(sr.tradeId);
            report.next();
            report.add(it != tradeNettingSetMap.end() ? it->second : "");
            report.add(sr.tradeId);
            report.add(ore::data::to_string(sr.isPar));
            report.add(prettyPrintInternalCurveName(QuantExt::reconstructFactor(sr.key_1, sr.desc_1)));
            report.add(sr.shift_1);
            report.add(prettyPrintInternalCurveName(QuantExt::reconstructFactor(sr.key_2, sr.desc_2)));
            report.add(sr.shift_2);
            report.add(sr.currency);
            report.add(sr.baseNpv);
            report.add(sr.delta);
            report.add(sr.gamma);
        } else if (!std::isfinite(sr.delta) || !std::isfinite(sr.gamma)) {
            // TODO: Again, is this needed?
            ALOG("sensitivity record has infinite values: " << sr);
        }
    }

    ssNettingSets->reset();
    while (SensitivityRecord sr = ssNettingSets->next()) {
        if ((outputThreshold == Null<Real>()) ||
            (fabs(sr.delta) > outputThreshold || (sr.gamma != Null<Real>() && fabs(sr.gamma) > outputThreshold))) {
            report.next();
            report.add(sr.tradeId);
            report.add("");
            report.add(ore::data::to_string(sr.isPar));
            report.add(prettyPrintInternalCurveName(QuantExt::reconstructFactor(sr.key_1, sr.desc_1)));
            report.add(sr.shift_1);
            report.add(prettyPrintInternalCurveName(QuantExt::reconstructFactor(sr.key_2, sr.desc_2)));
            report.add(sr.shift_2);
            report.add(sr.currency);
            report.add(sr.baseNpv);
            report.add(sr.delta);
            report.add(sr.gamma);
        } else if (!std::isfinite(sr.delta) || !std::isfinite(sr.gamma)) {
            // TODO: Again, is this needed?
            ALOG("sensitivity record has infinite values: " << sr);
        }
    }

    report.end();
    LOG("Sensitivity report finished");
}

void ReportWriter::writeSensitivityConfigReport(ore::data::Report& report,
                                                const std::map<RiskFactorKey, QuantLib::Real>& shiftSizes,
                                                const std::map<RiskFactorKey, QuantLib::Real>& baseValues,
                                                const std::map<RiskFactorKey, std::string>& keyToFactor) {
    LOG("Writing Sensitivity Config report");

    report.addColumn("Key", string())
        .addColumn("Factor", string())
        .addColumn("BaseValue", double(), 8)
        .addColumn("ShiftSize", double(), 8);

    for (auto const& [key, shift] : shiftSizes) {
        report.next();
        std::string keyStr = "na", factorStr = "na";
        Real baseValue = Null<Real>();
        keyStr = ore::data::to_string(key);
        if (auto it = keyToFactor.find(key); it != keyToFactor.end())
            factorStr = it->second;
        if (auto it = baseValues.find(key); it != baseValues.end())
            baseValue = it->second;
        report.add(keyStr).add(factorStr).add(baseValue).add(shift);
    }

    report.end();
    LOG("Sensitivity Config report finished.");
}

namespace {
template <class T>
void addMapResults(boost::any resultMap, const std::string& tradeId, const std::string& resultName, Report& report) {
    T map = boost::any_cast<T>(resultMap);
    for (auto it : map) {
        std::string name = resultName + "_" + it.first.code();
        boost::any tmp = it.second;
        auto p = parseBoostAny(tmp);
        report.next().add(tradeId).add(name).add(p.first).add(p.second);
    }
}

void addAnyResults(Report& report, const std::string& tradeId, const std::string& field, const boost::any& result,
                 const std::size_t precision) {
    auto p = parseBoostAny(result, precision);
    if (boost::starts_with(p.first, "vector")) {
        vector<std::string> tokens;
        string vect = p.second;
        vect.erase(remove(vect.begin(), vect.end(), '\"'), vect.end());
        boost::split(tokens, vect, boost::is_any_of(","));
        for (Size i = 0; i < tokens.size(); ++i) {
            boost::trim(tokens[i]);
            report.next().add(tradeId).add(field + "[" + std::to_string(i) + "]").add(p.first.substr(7)).add(tokens[i]);
        }
    } else {
        report.next().add(tradeId).add(field).add(p.first).add(p.second);
    }
}
} // namespace

void ReportWriter::writeAdditionalResultsReport(Report& report, QuantLib::ext::shared_ptr<Portfolio> portfolio,
                                                QuantLib::ext::shared_ptr<Market> market,
                                                const std::string& configuration, const std::string& baseCurrency,
                                                const std::size_t precision) {

    LOG("Writing AdditionalResults report");

    report.addColumn("TradeId", string())
        .addColumn("ResultId", string())
        .addColumn("ResultType", string())
        .addColumn("ResultValue", string());

    for (auto & [tId, trade] : portfolio->trades()) {
        try {
            // we first add any additional trade data.
            string tradeId = tId;
            string tradeType = trade->tradeType();
            Real notional2 = Null<Real>();
            string notional2Ccy = "";
            // Get the additional data for the current instrument.
            auto additionalData = trade->additionalData();
            for (const auto& kv : additionalData) {
                addAnyResults(report, tradeId, kv.first, kv.second, precision);
            }
            // if the 'notional[2]' has been provided convert it to base currency
            if (additionalData.count("notional[2]") != 0 && additionalData.count("notionalCurrency[2]") != 0) {
                notional2 = trade->additionalDatum<Real>("notional[2]");
                notional2Ccy = trade->additionalDatum<string>("notionalCurrency[2]");
            }

            auto additionalResults = trade->instrument()->additionalResults();
            if (additionalResults.count("notional[2]") != 0 && additionalResults.count("notionalCurrency[2]") != 0) {
                notional2 = trade->instrument()->qlInstrument()->result<Real>("notional[2]");
                notional2Ccy = trade->instrument()->qlInstrument()->result<string>("notionalCurrency[2]");
            }

            if (notional2 != Null<Real>() && notional2Ccy != "") {
                Real fx = 1.0;
                if (notional2Ccy != baseCurrency)
                    fx = market->fxRate(notional2Ccy + baseCurrency, configuration)->value();
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(8) << notional2 * fx;
                // report.next().add(tradeId).add("notionalInBaseCurrency[2]").add("double").add(oss.str());
            }

            // Just use the unadjusted trade ID in the additional results report for the main instrument.
            // If we have one or more additional instruments, use "_i" as suffix where i = 1, 2, 3, ... for each
            // additional instrument in turn and underscore as prefix to reduce risk of ID clash. We also add the
            // multiplier as an extra additional result if additional results exist.
            auto instruments = trade->instrument()->additionalInstruments();
            auto multipliers = trade->instrument()->additionalMultipliers();
            QL_REQUIRE(instruments.size() == multipliers.size(),
                       "Expected the number of "
                           << "additional instruments (" << instruments.size() << ") to equal the number of "
                           << "additional multipliers (" << multipliers.size() << ").");

            for (Size i = 0; i <= instruments.size(); ++i) {

                if (i > 0 && instruments[i - 1] == nullptr)
                    continue;

                std::map<std::string, boost::any> thisAddResults =
                    i == 0 ? additionalResults : instruments[i - 1]->additionalResults();

                // Trade ID suffix for additional instruments. Put underscores to reduce risk of clash with other IDs in
                // the portfolio (still a risk).
                tradeId = i == 0 ? trade->id() : ("_" + trade->id() + "_" + std::to_string(i));

                // Add the multiplier if there are additional results.
                // Check on 'instMultiplier' already existing is probably unnecessary.
                if (!thisAddResults.empty() && thisAddResults.count("instMultiplier") == 0) {
                    thisAddResults["instMultiplier"] = i == 0 ? trade->instrument()->multiplier() : multipliers[i - 1];
                }

                // Write current instrument's additional results.
                for (const auto& kv : thisAddResults) {
                    // some results are stored as maps. We loop over these so that there is one result per line
                    if (kv.second.type() == typeid(result_type_matrix)) {
                        addMapResults<result_type_matrix>(kv.second, tradeId, kv.first, report);
                    } else if (kv.second.type() == typeid(result_type_vector)) {
                        addMapResults<result_type_vector>(kv.second, tradeId, kv.first, report);
                    } else if (kv.second.type() == typeid(result_type_scalar)) {
                        addMapResults<result_type_scalar>(kv.second, tradeId, kv.first, report);
                    } else {
                        addAnyResults(report, tradeId, kv.first, kv.second, precision);
                    }
                }
            }

            // add coupon additional results

            for (Size i = 0; i < trade->legs().size(); ++i) {
                for (Size j = 0; j < trade->legs()[i].size(); ++j) {
                    if (auto c = QuantLib::ext::dynamic_pointer_cast<FloatingRateCoupon>(trade->legs()[i][j])) {
                        try {
                            c->rate(); // ensure the additional results results are available
                            for (auto const& kv : c->additionalResults()) {
                                addAnyResults(report, tradeId,
                                              kv.first + "[" + std::to_string(i) + "][" + std::to_string(j) + "]",
                                              kv.second, precision);
                            }
                        } catch (...) {
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            StructuredTradeErrorMessage(trade->id(), trade->tradeType(),
                                        "Error during trade pricing (additional results)", e.what())
                .log();
        }
    }

    report.end();

    LOG("AdditionalResults report written");
}

void ReportWriter::addMarketDatum(Report& report, const ore::data::MarketDatum& md, const Date& actualDate) {
    const Date& d = actualDate == Null<Date>() ? md.asofDate() : actualDate;
    report.next().add(d).add(md.name()).add(md.quote()->value());
}

void ReportWriter::writeMarketData(Report& report, const QuantLib::ext::shared_ptr<Loader>& loader, const Date& asof,
                                   const set<string>& quoteNames, bool returnAll) {

    LOG("Writing MarketData report");

    report.addColumn("datumDate", Date()).addColumn("datumId", string()).addColumn("datumValue", double(), 10);

    if (returnAll) {
        for (const auto& md : loader->loadQuotes(asof)) {
            addMarketDatum(report, *md, loader->actualDate());
        }
        return;
    }

    set<string> names;
    set<string> regexStrs;
    partitionQuotes(quoteNames, names, regexStrs);

    vector<std::regex> regexes;
    regexes.reserve(regexStrs.size());
    for (const auto& regexStr : regexStrs) {
        regexes.push_back(regex(regexStr));
    }

    for (const auto& md : loader->loadQuotes(asof)) {
        const auto& mdName = md->name();

        if (names.find(mdName) != names.end()) {
            addMarketDatum(report, *md, loader->actualDate());
            continue;
        }

        // This could be slow
        for (const auto& regex : regexes) {
            if (std::regex_match(mdName, regex)) {
                addMarketDatum(report, *md, loader->actualDate());
                break;
            }
        }
    }

    report.end();
    LOG("MarketData report written");
}

void ReportWriter::writeFixings(Report& report, const QuantLib::ext::shared_ptr<Loader>& loader) {

    LOG("Writing Fixings report");

    report.addColumn("fixingDate", Date()).addColumn("fixingId", string()).addColumn("fixingValue", double(), 10);

    for (const auto& f : loader->loadFixings()) {
        report.next().add(f.date).add(f.name).add(f.fixing);
    }

    report.end();
    LOG("Fixings report written");
}

void ReportWriter::writeDividends(Report& report, const QuantLib::ext::shared_ptr<Loader>& loader) {

    LOG("Writing Dividends report");

    report.addColumn("dividendExDate", Date())
        .addColumn("equityId", string())
        .addColumn("dividendRate", double(), 10)
        .addColumn("dividendPaymentDate", Date());

    for (const auto& f : loader->loadDividends()) {
        report.next().add(f.exDate).add(f.name).add(f.rate).add(f.payDate);
    }

    report.end();
    LOG("Dividends report written");
}

void ReportWriter::writePricingStats(ore::data::Report& report, const QuantLib::ext::shared_ptr<Portfolio>& portfolio) {

    LOG("Writing Pricing stats report");

    report.addColumn("TradeId", string())
        .addColumn("TradeType", string())
        .addColumn("NumberOfPricings", Size())
        .addColumn("CumulativeTiming", Size())
        .addColumn("AverageTiming", Size());

    for (auto const& [tid, trade] : portfolio->trades()) {
        std::size_t num = trade->getNumberOfPricings();
        Size cumulative = trade->getCumulativePricingTime() / 1000;
        Size average = num > 0 ? cumulative / num : 0;
        report.next().add(tid).add(trade->tradeType()).add(num).add(cumulative).add(average);
    }

    report.end();
    LOG("Pricing stats report written");
}

void ReportWriter::writeRunTimes(ore::data::Report& report, const Timer& timer) {

    LOG("Writing runtimes report");

    report.addColumn("Key", string())
        .addColumn("Total", Size())
        .addColumn("Count", Size())
        .addColumn("Max", Size())
        .addColumn("Min", Size())
        .addColumn("Average", double(), 2);
    for (const auto& [key, stats] : timer.getTimes()) {
        Size totalTime = stats.totalTime / 1000;
        Size maxTime = stats.maxTime / 1000;
        Size minTime = stats.minTime / 1000;
        Size count = stats.count;
        Real averageTime = stats.avgTime() / 1000;
        report.next()
            .add(boost::algorithm::join(key, "|"))
            .add(totalTime)
            .add(count)
            .add(maxTime)
            .add(minTime)
            .add(averageTime);
    }

    report.end();
    LOG("Finished writing runtimes report")
}

void ReportWriter::writeTimeAveragedNettedExposure(
    ore::data::Report& report,
    const std::map<std::string, std::vector<NettedExposureCalculator::TimeAveragedExposure>>& data) {

    LOG("Writing time averaged netted exposure");

    report.addColumn("NettingSetId", string())
        .addColumn("Sample", Size())
        .addColumn("PosExpNoColl", double(), 4)
        .addColumn("NegExpNoColl", double(), 4)
        .addColumn("PosExpWithColl", double(), 4)
        .addColumn("NegExpWithColl", double(), 4);

    for (auto const& [n, avg] : data) {
        for (Size k = 0; k < avg.size(); ++k) {
            report.next()
                .add(n)
                .add(k)
                .add(avg[k].positiveExposureBeforeCollateral)
                .add(avg[k].negativeExposureBeforeCollateral)
                .add(avg[k].positiveExposureAfterCollateral)
                .add(avg[k].negativeExposureAfterCollateral);
        }
    }

    report.end();
}

void ReportWriter::writeCube(ore::data::Report& report, const QuantLib::ext::shared_ptr<NPVCube>& cube,
                             const std::map<std::string, std::string>& nettingSetMap) {
    LOG("Writing cube report");

    report.addColumn("Id", string())
        .addColumn("NettingSet", string())
        .addColumn("DateIndex", Size())
        .addColumn("Date", string())
        .addColumn("Sample", Size())
        .addColumn("Depth", Size())
        .addColumn("Value", double(), 4);

    const map<string, Size>& idsAndPos = cube->idsAndIndexes();
    vector<string> dateStrings(cube->numDates());
    for (Size i = 0; i < cube->numDates(); ++i) {
        std::ostringstream oss;
        oss << QuantLib::io::iso_date(cube->dates()[i]);
        dateStrings[i] = oss.str();
    }

    std::ostringstream oss;
    oss << QuantLib::io::iso_date(cube->asof());
    string asofString = oss.str();

    vector<string> ids(idsAndPos.size());
    vector<string> nettingSetIds(idsAndPos.size());
    for (const auto& [id, idCubePos] : idsAndPos) {
        ids[idCubePos] = id;
        auto it = nettingSetMap.find(id);
        if (it != nettingSetMap.end())
            nettingSetIds[idCubePos] = it->second;
        else
            nettingSetIds[idCubePos] = "";
    }

    // T0
    for (Size i = 0; i < ids.size(); ++i) {
        report.next();
        report.add(ids[i])
            .add(nettingSetIds[i])
            .add(static_cast<Size>(0))
            .add(asofString)
            .add(static_cast<Size>(0))
            .add(static_cast<Size>(0))
            .add(cube->getT0(i));
    }
    
    // Cube
    for (Size i = 0; i < ids.size(); i++) {
        for (Size j = 0; j < cube->numDates(); j++) {
            for (Size k = 0; k < cube->samples(); k++) {
                for (Size l = 0; l < cube->depth(); l++) {
                    report.next();
                    report.add(ids[i])
                        .add(nettingSetIds[i])
                        .add(j + 1)
                        .add(dateStrings[j])
                        .add(k+1)
                        .add(l)
                        .add(cube->get(i, j, k, l));
                }
            }
        }
    }

    report.end();

    LOG("Cube report written");
}

// Ease notation again
typedef CrifRecord::ProductClass ProductClass;
typedef SimmConfiguration::RiskClass RiskClass;
typedef SimmConfiguration::MarginType MarginType;
typedef SimmConfiguration::SimmSide SimmSide;

void ReportWriter::writeSIMMReport(
    const map<SimmSide, map<NettingSetDetails, pair<CrifRecord::Regulation, SimmResults>>>& finalSimmResultsMap,
    const QuantLib::ext::shared_ptr<Report> report, const bool hasNettingSetDetails, const string& simmResultCcy,
    const string& simmCalcCcyCall, const string& simmCalcCcyPost, const string& reportCcy, Real fxSpot,
    Real outputThreshold) {


    // Transform final SIMM results
    map<SimmSide, map<NettingSetDetails, map<set<CrifRecord::Regulation>, SimmResults>>> finalSimmResults;
    for (const auto& sv : finalSimmResultsMap) {
        const SimmSide& side = sv.first;
        for (const auto& nv : sv.second) {
            const NettingSetDetails& nettingSetDetails = nv.first;
            set<CrifRecord::Regulation> regulation({nv.second.first});
            const SimmResults& simmResults = nv.second.second;

            finalSimmResults[side][nettingSetDetails][regulation] = simmResults;
        }
    }

    writeSIMMReport(finalSimmResults, report, hasNettingSetDetails, simmResultCcy, simmCalcCcyCall, simmCalcCcyPost,
                    reportCcy, true, fxSpot, outputThreshold);
}

void ReportWriter::writeSIMMReport(
    const map<SimmSide, map<NettingSetDetails, map<set<CrifRecord::Regulation>, SimmResults>>>& simmResultsMap,
    const QuantLib::ext::shared_ptr<Report> report, const bool hasNettingSetDetails, const string& simmResultCcy,
    const string& simmCalcCcyCall, const string& simmCalcCcyPost, const string& reportCcy, const bool isFinalSimm, Real fxSpot,
    Real outputThreshold) {

    if (isFinalSimm) {
        LOG("Writing SIMM results report.");
    } else {
        LOG("Writing full SIMM results report.");
    }

    // netting set headers
    report->addColumn("Portfolio", string());
    if (hasNettingSetDetails) {
        for (const string& field : NettingSetDetails::optionalFieldNames())
            report->addColumn(field, string());
    }

    report->addColumn("ProductClass", string())
        .addColumn("RiskClass", string())
        .addColumn("MarginType", string())
        .addColumn("Bucket", string())
        .addColumn("SimmSide", string())
        .addColumn("Regulation", string())
        .addColumn("InitialMargin", double(), 9)
        .addColumn("Currency", string())
        .addColumn("CalculationCurrency", string());
    if (!reportCcy.empty()) {
        report->addColumn("InitialMargin(Report)", double(), 2).addColumn("ReportCurrency", string());
    }

    // Ensure that fxSpot is 1 if no reporting currency provided
    if (reportCcy.empty() && fxSpot != 1.0)
        fxSpot = 1.0;

    const vector<SimmSide> sides({SimmSide::Call, SimmSide::Post});
    for (const SimmSide side : sides) {
        const string& sideString = ore::data::to_string(side);

        // Variable to hold sum of initial margin over all portfolios
        Real sumSidePortfolios = 0.0;
        Real sumSidePortfoliosReporting = 0.0;

        set<CrifRecord::Regulation> winningRegs;
        if (simmResultsMap.find(side) != simmResultsMap.end()) {
            for (const auto& nv : simmResultsMap.at(side)) {
                const NettingSetDetails& portfolioId = nv.first;
                const auto& simmResultsMap = nv.second;

                if (isFinalSimm)
                    QL_REQUIRE(simmResultsMap.size() <= 1,
                               "Final SIMM results should only have one (winning) regulation per netting set.");

                for (const auto& rv : simmResultsMap) {
                    const set<CrifRecord::Regulation>& regulations = rv.first;
                    const SimmResults& results = rv.second;

                    if (isFinalSimm)
                        winningRegs.insert(regulations.begin(), regulations.end());

                    QL_REQUIRE(results.resultCurrency() == simmResultCcy,
                               "writeSIMMReport(): SIMM results ("
                                   << results.resultCurrency() << ") should be denominated in the SIMM result currency ("
                                   << simmResultCcy << ").");

                    // Loop over the results for this portfolio
                    for (const auto& result : results.data()) {
                        // Get the key values
                        const auto& key = result.first;
                        ProductClass pc = get<0>(key);
                        RiskClass rc = get<1>(key);
                        MarginType mt = get<2>(key);
                        string b = get<3>(key);
                        Real im = result.second;
                        Real simmReporting = 0.0;

                        // Write row if IM not negligible relative to outputThreshold.
                        if (fabs(im) >= outputThreshold ||
                            (pc == ProductClass::All && rc == RiskClass::All && mt == MarginType::All)) {
                            report->next();
                            map<string, string> nettingSetMap = portfolioId.mapRepresentation();
                            for (const string& field : NettingSetDetails::fieldNames(hasNettingSetDetails)) {
                                report->add(nettingSetMap[field]);
                            }
                            report->add(ore::data::to_string(pc))
                                .add(ore::data::to_string(rc))
                                .add(ore::data::to_string(mt))
                                .add(b)
                                .add(sideString)
                                .add(regulationsToString(regulations))
                                .add(result.second)
                                .add(results.resultCurrency())
                                .add(results.calculationCurrency());
                            if (!reportCcy.empty()) {
                                simmReporting = result.second * fxSpot;
                                report->add(simmReporting).add(reportCcy);
                            }
                            // Update aggregate portfolio IM value if necessary
                            // SimmResults should always contain an entry with this key - it is the portfolio IM
                            if (isFinalSimm &&
                                (pc == ProductClass::All && rc == RiskClass::All && mt == MarginType::All)) {
                                sumSidePortfolios += result.second;
                                sumSidePortfoliosReporting += simmReporting;
                            }
                        }
                    }
                }
            }
        }

        // Write out a row for the aggregate IM over all portfolios
        // We only write out this row if either reporting ccy was provided or if currency of all the results is the same
        if (isFinalSimm) {
            string finalWinningReg =
                winningRegs.size() > 1 || winningRegs.empty() ? "" : ore::data::to_string(*winningRegs.begin());

            // Write out common columns
            report->next();
            Size numNettingSetFields = NettingSetDetails::fieldNames(hasNettingSetDetails).size();
            for (Size t = 0; t < numNettingSetFields; t++)
                report->add("All");
            report->add("All")
                .add("All")
                .add("All")
                .add("All")
                .add(sideString)
                .add(finalWinningReg)
                .add(sumSidePortfolios)
                .add(simmResultCcy)
                .add(side == SimmSide::Call ? simmCalcCcyCall : simmCalcCcyPost);

            // Write out SIMM in reporting currency if we can
            if (!reportCcy.empty())
                report->add(sumSidePortfoliosReporting).add(reportCcy);
        }

        report->end();

        LOG("SIMM results report written.");
    }
}

void ReportWriter::writeSIMMData(const ore::analytics::Crif& simmData, const QuantLib::ext::shared_ptr<Report>& dataReport,
                                 const bool hasNettingSetDetails) {

    LOG("Writing SIMM data report.");

    // Add the headers to the report

    bool hasRegulations = false;
    for (const auto& scr : simmData) {
        CrifRecord cr = scr.toCrifRecord();
        if (!cr.collectRegulations.empty() || !cr.postRegulations.empty()) {
            hasRegulations = true;
            break;
        }
    }
        
    // netting set headers
    dataReport->addColumn("Portfolio", string());
    if (hasNettingSetDetails) {
        for (const string& field : NettingSetDetails::optionalFieldNames())
            dataReport->addColumn(field, string());
    }

    dataReport->addColumn("RiskType", string())
        .addColumn("ProductClass", string())
        .addColumn("Bucket", string())
        .addColumn("Qualifier", string())
        .addColumn("Label1", string())
        .addColumn("Label2", string())
        .addColumn("AmountCurrency", string())
        .addColumn("Amount", double(), 2)
        .addColumn("AmountUSD", double(), 2);

    if (hasRegulations)
        dataReport->addColumn("collect_regulations", string())
            .addColumn("post_regulations", string());

    // Write the report body by looping over the netted CRIF records
    for (const auto& scr : simmData) {
        CrifRecord cr = scr.toCrifRecord();

        // Skip to next netted CRIF record if 'AmountUSD' is negligible
        if (close_enough(cr.amountUsd, 0.0))
            continue;

        // Skip Schedule IM records
        if (cr.imModel == CrifRecord::IMModel::Schedule)
            continue;
        
        // Skip if the CRIF Record type is not SIMM
        if (cr.type() != CrifRecord::RecordType::SIMM)
            continue;
        
        // Same check as above, but for backwards compatibility, if im_model is not used
        // but Risk::Type is PV or Notional
        if (cr.imModel == CrifRecord::IMModel::Empty &&
            (cr.riskType == CrifRecord::RiskType::Notional || cr.riskType == CrifRecord::RiskType::PV))
            continue;

        // Write current netted CRIF record
        dataReport->next();
        map<string, string> nettingSetMap = cr.nettingSetDetails.mapRepresentation();
        for (const string& field : NettingSetDetails::fieldNames(hasNettingSetDetails))
            dataReport->add(nettingSetMap[field]);
        dataReport->add(ore::data::to_string(cr.riskType))
            .add(ore::data::to_string(cr.productClass))
            .add(cr.bucket)
            .add(cr.qualifier)
            .add(cr.label1)
            .add(cr.label2)
            .add(cr.amountCurrency)
            .add(cr.amount)
            .add(cr.amountUsd);

        if (hasRegulations) {
            string collectRegString = escapeCommaSeparatedList(regulationsToString(cr.collectRegulations), '\0');
            string postRegString = escapeCommaSeparatedList(regulationsToString(cr.postRegulations), '\0');

            dataReport->add(collectRegString)
                .add(postRegString);
        }
    }

    dataReport->end();

    LOG("SIMM data report written.");
}

void ReportWriter::writeCrifReport(const QuantLib::ext::shared_ptr<Report>& report,
                                   const QuantLib::ext::shared_ptr<Crif>& crif) {

    if (crif) {
        // If we have SIMM parameters, check if at least one of them uses netting set details optional field/s
        // It is easier to check here than to pass the flag from other places, since otherwise we'd have to handle certain
        // edge cases e.g. SIMM parameters use optional NSDs, but trades don't. So SIMM report should not display NSDs, but
        // CRIF report still should.
        bool hasNettingSetDetails = false;
        for (const auto& scr : *crif) {
            CrifRecord cr = scr.toCrifRecord();

            if (!cr.nettingSetDetails.emptyOptionalFields())
                hasNettingSetDetails = true;
        }

        std::vector<string> addFields;
        bool hasCollectRegulations = false;
        bool hasPostRegulations = false;
        bool hasScheduleTrades = false;
        for (const auto& scr : *crif) {
            CrifRecord cr = scr.toCrifRecord();

            // Check which additional fields are being used/populated
            for (const auto& af : cr.additionalFields) {
                if (std::find(addFields.begin(), addFields.end(), af.first) == addFields.end()) {
                    addFields.push_back(af.first);
                }
            }

            // Check if regulations are being used
            if (!hasCollectRegulations)
                hasCollectRegulations = !cr.collectRegulations.empty();
            if (!hasPostRegulations)
                hasPostRegulations = !cr.postRegulations.empty();

            // Check if there are Schedule trades
            if (!hasScheduleTrades) {
                try {
                    hasScheduleTrades = cr.imModel == CrifRecord::IMModel::Schedule;
                } catch (std::exception&) {
                }
            }
        }

        // Add report headers

        report->addColumn("TradeID", string()).addColumn("PortfolioID", string());

        // Add additional netting set fields if netting set details are being used instead of just the netting set ID
        if (hasNettingSetDetails) {
            for (const string& optionalField : NettingSetDetails::optionalFieldNames())
                report->addColumn(optionalField, string());
        }

        report->addColumn("ProductClass", string())
            .addColumn("RiskType", string())
            .addColumn("Qualifier", string())
            .addColumn("Bucket", string())
            .addColumn("Label1", string())
            .addColumn("Label2", string())
            .addColumn("AmountCurrency", string())
            .addColumn("Amount", double(), 2)
            .addColumn("AmountUSD", double(), 2)
            .addColumn("IMModel", string())
            .addColumn("TradeType", string());

        if (hasScheduleTrades || crif->type() == Crif::CrifType::Frtb)
            report->addColumn("end_date", string());

        if (crif->type() == Crif::CrifType::Frtb) {
            report->addColumn("Label3", string())
                .addColumn("CreditQuality", string())
                .addColumn("LongShortInd", string())
                .addColumn("CoveredBondInd", string())
                .addColumn("TrancheThickness", string())
                .addColumn("BB_RW", string());
        }

        if (hasCollectRegulations)
            report->addColumn("collect_regulations", string());

        if (hasPostRegulations)
            report->addColumn("post_regulations", string());

        // Add additional CRIF fields
        for (const string& f : addFields) {
            report->addColumn(f, string());
        }

        // Write individual CRIF records
        for (const auto& scr : *crif) {
            CrifRecord cr = scr.toCrifRecord();

            report->next().add(cr.tradeId).add(cr.nettingSetDetails.nettingSetId());

            if (hasNettingSetDetails) {
                map<string, string> crNettingSetDetailsMap = NettingSetDetails(cr.nettingSetDetails).mapRepresentation();
                for (const string& optionalField : NettingSetDetails::optionalFieldNames())
                    report->add(crNettingSetDetailsMap[optionalField]);
            }

            report->add(ore::data::to_string(cr.productClass))
                .add(ore::data::to_string(cr.riskType))
                .add(cr.qualifier)
                .add(cr.bucket)
                .add(cr.label1)
                .add(cr.label2)
                .add(cr.amountCurrency)
                .add(cr.amount)
                .add(cr.amountUsd)
                .add(ore::data::to_string(cr.imModel))
                .add(cr.tradeType);

            if (hasScheduleTrades || crif->type() == Crif::CrifType::Frtb)
                report->add(cr.endDate);

            if (crif->type() == Crif::CrifType::Frtb) {
                report->add(cr.label3)
                    .add(cr.creditQuality)
                    .add(cr.longShortInd)
                    .add(cr.coveredBondInd)
                    .add(cr.trancheThickness)
                    .add(cr.bb_rw);
            }

            if (hasCollectRegulations) {
                string regString = escapeCommaSeparatedList(regulationsToString(cr.collectRegulations), '\0');
                report->add(regString);
            }

            if (hasPostRegulations) {
                string regString = escapeCommaSeparatedList(regulationsToString(cr.postRegulations), '\0');
                report->add(regString);
            }

            for (const string& af : addFields) {
                if (cr.additionalFields.find(af) == cr.additionalFields.end())
                    report->add("");
                else
                    report->add(cr.getAdditionalFieldAsStr(af));
            }
        }
    }

    report->end();
}

void ReportWriter::writeScenarioStatistics(const QuantLib::ext::shared_ptr<ScenarioGenerator>& generator,
    const std::vector<RiskFactorKey>& keys, const Size numPaths,
    const std::vector<Date>& dates, ore::data::Report& report) {
    report.addColumn("Date", Date())
        .addColumn("Key", string())
        .addColumn("min", double(), 8)
        .addColumn("mean", double(), 8)
        .addColumn("max", double(), 8)
        .addColumn("stddev", double(), 8)
        .addColumn("skewness", double(), 8)
        .addColumn("kurtosis", double(), 8);

    std::vector<boost::accumulators::accumulator_set<
        double, boost::accumulators::stats<boost::accumulators::tag::min, boost::accumulators::tag::max,
        boost::accumulators::tag::mean, boost::accumulators::tag::variance,
        boost::accumulators::tag::skewness, boost::accumulators::tag::kurtosis>>>
        acc(keys.size() * dates.size());

    for (Size i = 0; i < numPaths; ++i) {
        for (Size d = 0; d < dates.size(); ++d) {
            QuantLib::ext::shared_ptr<Scenario> currentScenario = generator->next(dates[d]);
            for (Size k = 0; k < keys.size(); ++k) {
                acc[d * keys.size() + k](currentScenario->get(keys[k]));
            }
        }
    }
    for (Size d = 0; d < dates.size(); ++d) {
        for (Size k = 0; k < keys.size(); ++k) {
            Size idx = d * keys.size() + k;
            report.next()
                .add(dates[d])
                .add(ore::data::to_string(keys[k]))
                .add(boost::accumulators::min(acc[idx]))
                .add(boost::accumulators::mean(acc[idx]))
                .add(boost::accumulators::max(acc[idx]))
                .add(std::sqrt(boost::accumulators::variance(acc[idx])));
            if (!close_enough(boost::accumulators::variance(acc[idx]), 0.0)) {
                report.add(boost::accumulators::skewness(acc[idx])).add(boost::accumulators::kurtosis(acc[idx]));
            }
            else {
                // avoid ReportWriter::non-sensical output
                report.add(0.0).add(0.0);
            }
        }
    }
    report.end();
}

void ReportWriter::writeScenarioDistributions(const QuantLib::ext::shared_ptr<ScenarioGenerator>& generator,
                                              const std::vector<RiskFactorKey>& keys, const Size numPaths,
                                              const std::vector<Date>& dates, const Size distSteps,
                                              ore::data::Report& report) {
    report.addColumn("Date", Date())
        .addColumn("Key", string())
        .addColumn("Bound", double(), 8)
        .addColumn("Count", Size());

    std::vector<std::vector<std::vector<Real>>> values(
        dates.size(), std::vector<std::vector<Real>>(keys.size(), std::vector<Real>(numPaths, 0.0)));

    for (Size i = 0; i < numPaths; ++i) {
        for (Size d = 0; d < dates.size(); ++d) {
            QuantLib::ext::shared_ptr<Scenario> currentScenario = generator->next(dates[d]);
            for (Size k = 0; k < keys.size(); ++k) {
                values[d][k][i] = currentScenario->get(keys[k]);
            }
        }
    }

    std::vector<Real> bounds;
    std::vector<Size> counts;
    for (Size d = 0; d < dates.size(); ++d) {
        for (Size k = 0; k < keys.size(); ++k) {
            distributionCount(values[d][k].begin(), values[d][k].end(), distSteps, bounds, counts);
            for (Size i = 0; i < distSteps; ++i) {
                report.next().add(dates[d]).add(ore::data::to_string(keys[k])).add(bounds[i]).add(counts[i]);
            }
        }
    }
    report.end();
}

void ReportWriter::writeHistoricalScenarioDetails(
    const QuantLib::ext::shared_ptr<ore::analytics::HistoricalScenarioGenerator>& generator, ore::data::Report& report) {

    report.addColumn("PLDate1", Date())
        .addColumn("PLDate2", Date())
        .addColumn("Key", string())
        .addColumn("BaseValue", double(), 8)
        .addColumn("AdjustmentFactor1", double(), 8)
        .addColumn("AdjustmentFactor2", double(), 8)
        .addColumn("ScenarioValue1", double(), 8)
        .addColumn("ScenarioValue2", double(), 8)
        .addColumn("ShiftType", string())
        .addColumn("Displacement", double(), 8)
        .addColumn("Return", double(), 8)
        .addColumn("ScenarioValue", double(), 8);

    Date asof = generator->baseScenario()->asof();
    for (Size i = 0; i < generator->startDates().size(); ++i) {
        std::ignore = generator->next(asof);
        for (auto const& d : generator->lastHistoricalScenarioCalculationDetails()) {
            report.next()
                .add(d.scenarioDate1)
                .add(d.scenarioDate2)
                .add(ore::data::to_string(d.key))
                .add(d.baseValue)
                .add(d.adjustmentFactor1)
                .add(d.adjustmentFactor2)
                .add(d.scenarioValue1)
                .add(d.scenarioValue2)
                .add(ore::data::to_string(d.returnType))
                .add(d.displacement)
                .add(d.returnValue)
                .add(d.scenarioValue);
        }
    }
    report.end();
}

void ReportWriter::writeStockSplitReport(const QuantLib::ext::shared_ptr<Scenario>& baseScenario,
                                         const QuantLib::ext::shared_ptr<ore::analytics::HistoricalScenarioLoader>& hsloader,
                                         const QuantLib::ext::shared_ptr<ore::data::AdjustmentFactors>& adjFactors,
                                         const QuantLib::ext::shared_ptr<ore::data::Report>& report) {

    report->addColumn("EquityId", string())
        .addColumn("Date", Date())
        .addColumn("Price", double(), 8)
        .addColumn("Factor", double(), 8)
        .addColumn("CumulatedFactor", double(), 8)
        .addColumn("AdjustedPrice", double(), 8);

    if (adjFactors) {
        std::set<std::string> names;
        for (auto const& k : baseScenario->keys()) {
            if (k.keytype == RiskFactorKey::KeyType::EquitySpot) {
                names.insert(k.name);
            }
        }

        std::vector<QuantLib::Date> hsdates = hsloader->dates();

        for (auto const& name : names) {

            std::set<QuantLib::Date> dates = adjFactors->dates(name);
            dates.insert(hsdates.begin(), hsdates.end());

            for (auto const& d : dates) {

                Real price = Null<Real>();
                if (std::find(hsdates.begin(), hsdates.end(), d) != hsdates.end()) {
                    auto scen = hsloader->getScenario(d);
                    RiskFactorKey rf(RiskFactorKey::KeyType::EquitySpot, name);
                    if (scen->has(rf))
                        price = scen->get(rf);
                }
                Real factor = adjFactors->getFactorContribution(name, d);
                Real cumFactor = adjFactors->getFactor(name, d);
                Real adjPrice = price == Null<Real>() ? Null<Real>() : price * cumFactor;

                report->next().add(name).add(d).add(price).add(factor).add(cumFactor).add(adjPrice);
            }
        }
    }
    report->end();
}

void ReportWriter::writeHistoricalScenarioDistributions(
    QuantLib::ext::shared_ptr<HistoricalScenarioGenerator>& hsgen,
    const QuantLib::ext::shared_ptr<ore::analytics::ScenarioSimMarket>& simMarket,
    const QuantLib::ext::shared_ptr<ore::analytics::ScenarioSimMarketParameters>& simMarketParams,
    QuantLib::ext::shared_ptr<ore::data::Report> histScenDetailsReport, QuantLib::ext::shared_ptr<ore::data::Report> statReport,
    QuantLib::ext::shared_ptr<ore::data::Report> distReport, Size distSteps) {

    // Don't leave it up to the caller to do this
    simMarket->scenarioGenerator() = hsgen;
    hsgen->baseScenario() = simMarket->baseScenario();

    // If both report pointers are null, return early
    if (!statReport && !distReport)
        return;

    // Make a transformed generator i.e. discount -> zero etc.
    auto hsgent = QuantLib::ext::make_shared<HistoricalScenarioGeneratorTransform>(hsgen, simMarket, simMarketParams);

    const vector<RiskFactorKey>& keys = hsgen->baseScenario()->keys();
    Size numScen = hsgen->numScenarios();
    Date asof = hsgen->baseScenario()->asof();

    // Write the statistics report if requested
    if (statReport) {
        hsgent->reset();
        writeScenarioStatistics(hsgent, keys, numScen, {asof}, *statReport);
    }

    // Write the distribution report if requested
    if (distReport) {
        QL_REQUIRE(distSteps != Null<Size>(),
                   "When creating a distribution report, a valid distribution step size is required");
        hsgent->reset();
        writeScenarioDistributions(hsgent, keys, numScen, {asof}, distSteps, *distReport);
    }

    // Write the scenario report if requested
    if (histScenDetailsReport) {
        hsgent->reset();
        writeHistoricalScenarioDetails(hsgent, *histScenDetailsReport);
    }
}

void ReportWriter::writeHistoricalScenarios(const QuantLib::ext::shared_ptr<HistoricalScenarioLoader>& hsloader,
                                            const QuantLib::ext::shared_ptr<ore::data::Report>& report) {
    // each scenario might have a different set of keys, so we collect the union of all keys
    // and write them out (missing keys will be written as NA to the report)
    std::set<RiskFactorKey> allKeys;
    for (const auto& s : hsloader->scenarios()[0])
        allKeys.insert(s.second->keys().begin(), s.second-> keys().end());
    ScenarioWriter sw(nullptr, report, std::vector<RiskFactorKey>(allKeys.begin(), allKeys.end()));
    bool writeHeader = true;
    for (const auto& s : hsloader->scenarios()[0]) {
        sw.writeScenario(s.second, writeHeader);
        writeHeader = false;
    }
}

// Ease notation again
typedef CrifRecord::ProductClass ProductClass;
typedef SimmConfiguration::RiskClass RiskClass;
typedef SimmConfiguration::MarginType MarginType;
typedef SimmConfiguration::SimmSide SimmSide;
typedef IMScheduleCalculator::IMScheduleTradeData IMScheduleTradeData;

void ReportWriter::writeIMScheduleSummaryReport(
    const map<SimmSide, map<NettingSetDetails, pair<CrifRecord::Regulation, IMScheduleResults>>>& finalResultsMap,
    const QuantLib::ext::shared_ptr<Report> report, const bool hasNettingSetDetails, const string& simmResultCcy,
    const string& reportCcy, Real fxSpot, Real outputThreshold) {

    LOG("Writing IM Schedule results summary report.");

    // netting set headers
    report->addColumn("Portfolio", string());
    if (hasNettingSetDetails) {
        for (const string& field : NettingSetDetails::optionalFieldNames())
            report->addColumn(field, string());
    }

    report->addColumn("ProductClass", string())
        .addColumn("GrossIM", double(), 2)
        .addColumn("GrossCurrentRC", double(), 2)
        .addColumn("NetCurrentRC", double(), 2)
        .addColumn("NetToGrossRatio", double(), 6)
        .addColumn("Side", string())
        .addColumn("Regulation", string())
        .addColumn("ScheduleIM", double(), 2)
        .addColumn("Currency", string());
    if (!reportCcy.empty()) {
        report->addColumn("ScheduleIM(Report)", double(), 2).addColumn("ReportCurrency", string());
    }

    const vector<SimmSide> sides({SimmSide::Call, SimmSide::Post});
    for (const SimmSide side : sides) {
        const string& sideString = to_string(side);

        // Variable to hold sum of schedule IM over all portfolios
        Real sumSideScheduleIM = 0.0;
        Real sumSideScheduleIMReporting = 0.0;

        std::set<CrifRecord::Regulation> winningRegs;
        if (finalResultsMap.find(side) != finalResultsMap.end()) {
            for (const auto& nv : finalResultsMap.at(side)) {
                const NettingSetDetails& portfolioId = nv.first;
                const CrifRecord::Regulation& regulation = nv.second.first;
                const IMScheduleResults& results = nv.second.second;

                winningRegs.insert(regulation);

                QL_REQUIRE(results.currency() == simmResultCcy,
                           "writeIMScheduleSummaryReport(): IMSchedule results ("
                               << results.currency() << ") should be denominated in the SIMM result currency ("
                               << simmResultCcy << ").");

                // Loop over the results for this portfolio
                for (const auto& imScheduleResult : results.data()) {
                    ProductClass pc = imScheduleResult.first;
                    IMScheduleResult result = imScheduleResult.second;

                    Real im = pc == ProductClass::All ? result.scheduleIM : result.grossIM;

                    report->next();
                    const map<string, string> nettingSetMap = portfolioId.mapRepresentation();
                    for (const string& field : NettingSetDetails::fieldNames(hasNettingSetDetails)) {
                        report->add(nettingSetMap.at(field));
                    }
                    report->add(to_string(pc))
                        .add(result.grossIM)
                        .add(result.grossRC)
                        .add(result.netRC)
                        .add(result.NGR)
                        .add(sideString)
                        .add(ore::data::to_string(regulation))
                        .add(im)
                        .add(results.currency());

                    if (!reportCcy.empty()) {
                        Real scheduleIMReporting = im * fxSpot;
                        report->add(scheduleIMReporting).add(reportCcy);

                        if (pc == ProductClass::All)
                            sumSideScheduleIMReporting += scheduleIMReporting;
                    }

                    if (pc == ProductClass::All)
                        sumSideScheduleIM += result.scheduleIM;
                }
            }
        }

        // Write out a row for the aggregate IM over all portfolios
        // We only write out this row if either reporting ccy was provided or if currency of all the results is the same
        string finalWinningReg =
            winningRegs.size() > 1 || winningRegs.empty() ? "" : ore::data::to_string(*winningRegs.begin());

        // Write out common columns
        report->next();
        Size numNettingSetFields = NettingSetDetails::fieldNames(hasNettingSetDetails).size();
        for (Size t = 0; t < numNettingSetFields; t++)
            report->add("All");
        report->add(to_string(ProductClass::All))
            .add(Null<Real>())
            .add(Null<Real>())
            .add(Null<Real>())
            .add(Null<Real>())
            .add(sideString)
            .add(finalWinningReg)
            .add(sumSideScheduleIM)
            .add(simmResultCcy);

        // Write out schedule IM in reporting currency if we can
        if (!reportCcy.empty())
            report->add(sumSideScheduleIMReporting).add(reportCcy);
    }

    report->end();

    LOG("IM Schedule results summary report written.");
}

void ReportWriter::writeIMScheduleTradeReport(const map<string, vector<IMScheduleTradeData>>& tradeResults,
                                              const QuantLib::ext::shared_ptr<ore::data::Report> report,
                                              const bool hasNettingSetDetails) {

    LOG("Writing IM Schedule trade results report.");

    report->addColumn("TradeId", string());

    // netting set headers
    report->addColumn("Portfolio", string());
    if (hasNettingSetDetails) {
        for (const string& field : NettingSetDetails::optionalFieldNames())
            report->addColumn(field, string());
    }

    report->addColumn("ProductClass", string())
        .addColumn("EndDate", string())
        .addColumn("Maturity", double(), 5)
        .addColumn("Label", string())
        .addColumn("Multiplier", double(), 2)
        .addColumn("Notional", double(), 2)
        .addColumn("NotionalCurrency", string())
        .addColumn("PV", double(), 2)
        .addColumn("PVCurrency", string())
        .addColumn("Notional(Base)", double(), 2)
        .addColumn("PV(Base)", double(), 2)
        .addColumn("BaseCurrency", string())
        .addColumn("GrossIM(Base)", double(), 2)
        .addColumn("CollectRegulations", string())
        .addColumn("PostRegulations", string());

    // Variable to hold sum of schedule IM over all portfolios
    for (const auto& kv : tradeResults) {
        const string& tradeId = kv.first;

        for (const IMScheduleTradeData& tradeData : kv.second) {
            const NettingSetDetails& portfolioId = tradeData.nettingSetDetails;

            // Write row if IM not negligible relative to outputThreshold.
            report->next();
            report->add(tradeId);

            const map<string, string> nettingSetMap = portfolioId.mapRepresentation();
            for (const string& field : NettingSetDetails::fieldNames(hasNettingSetDetails)) {
                report->add(nettingSetMap.at(field));
            }
            string collectRegsString = escapeCommaSeparatedList(regulationsToString(tradeData.collectRegulations), '\0');
            string postRegsString = escapeCommaSeparatedList(regulationsToString(tradeData.postRegulations), '\0');
            
            report->add(to_string(tradeData.productClass))
                .add(to_string(tradeData.endDate))
                .add(tradeData.maturity)
                .add(tradeData.labelString)
                .add(tradeData.multiplier)
                .add(tradeData.notional)
                .add(tradeData.notionalCcy)
                .add(tradeData.presentValue)
                .add(tradeData.presentValueCcy)
                .add(tradeData.notionalCalc)
                .add(tradeData.presentValueCalc)
                .add(tradeData.calculationCcy)
                .add(tradeData.grossMarginCalc)
                .add(collectRegsString)
                .add(postRegsString);
        }
    }

    report->end();

    LOG("IM Schedule trade results report written.");
}

Real aggregateTradeFlow(const std::string& tradeId, const Date& d0, const Date& d1, 
            const ext::shared_ptr<InMemoryReport>& cashFlowReport,
            const ext::shared_ptr<ore::data::Market>& market, const std::string& configuration,
            const std::string& baseCurrency)  {
    Size tradeIdColumn = 0;
    Size dateColumn = 4;
    Size amountColumn = 6;
    Size ccyColumn = 7;
    QL_REQUIRE(cashFlowReport->header(tradeIdColumn) == "TradeId", "incorrect trade id column " << tradeIdColumn);
    QL_REQUIRE(cashFlowReport->header(amountColumn) == "Amount", "incorrect trade id column " << amountColumn);
    QL_REQUIRE(cashFlowReport->header(ccyColumn) == "Currency", "incorrect trade id column " << ccyColumn);
    QL_REQUIRE(cashFlowReport->header(dateColumn) == "PayDate", "incorrect trade id column " << dateColumn);

    Real flow = 0.0;
    for (Size i = 0; i < cashFlowReport->rows(); ++i) {
        string id = boost::get<string>(cashFlowReport->data(tradeIdColumn, i));
    if (id != tradeId)
        continue;
    Date date = boost::get<Date>(cashFlowReport->data(dateColumn, i));
    if (date <= d0 || date > d1)
        continue;
    string ccy = boost::get<string>(cashFlowReport->data(ccyColumn, i));
    Real amount = boost::get<Real>(cashFlowReport->data(amountColumn, i));
    Real fx = 1.0;
    if (ccy != baseCurrency)
        fx = market->fxRate(ccy + baseCurrency, configuration)->value();
    flow += fx * amount; 
    }
    
    return flow;
}

void ReportWriter::writePnlReport(ore::data::Report& report,
            const ext::shared_ptr<InMemoryReport>& t0NpvReport,
            const ext::shared_ptr<InMemoryReport>& t0NpvLaggedReport,
            const ext::shared_ptr<InMemoryReport>& t1NpvLaggedReport,
            const ext::shared_ptr<InMemoryReport>& t1Npvt0PortReport,
            const ext::shared_ptr<InMemoryReport>& t1NpvReport,
            const ext::shared_ptr<InMemoryReport>& t0CashFlowReport,
            const Date& startDate, const Date& endDate,
            const std::string& baseCurrency,
                        const ext::shared_ptr<ore::data::Market>& market,
            const std::string& configuration,
            const ext::shared_ptr<Portfolio>& portfolio) {
  
    LOG("PnL report");

    report.addColumn("TradeId", string())
        .addColumn("TradeType", string())
        .addColumn("Maturity", Date())
        .addColumn("MaturityTime", double(), 6)
        .addColumn("StartDate", Date())
        .addColumn("EndDate", Date())
        .addColumn("NPV(t0)", double(), 6)
        .addColumn("NPV(asof=t0;mkt=t1)", double(), 6)
        .addColumn("NPV(asof=t1;mkt=t0)", double(), 6)
        .addColumn("NPV(t1;portfolio=t0)", double(), 6)
        .addColumn("NPV(t1)", double(), 6)
        .addColumn("PeriodCashFlow", double(), 6)
        .addColumn("New", double(), 6)
        .addColumn("Matured", double(), 6)
        .addColumn("Terminated", double(), 6)
        .addColumn("Amendments", double(), 6)
        .addColumn("Theta", double(), 6)
        .addColumn("HypotheticalCleanPnL", double(), 6)
        .addColumn("CleanPnL", double(), 6)
        .addColumn("DirtyPnL", double(), 6)
        .addColumn("Currency", string());

    Size tradeIdColumn = 0;
    Size tradeTypeColumn = 1;
    Size maturityDateColumn = 2;
    Size maturityTimeColumn = 3;
    Size npvBaseColumn = 6;
    Size baseCcyColumn = 7;
    
    QL_REQUIRE(t0NpvReport->rows() == t0NpvLaggedReport->rows(), "different number of rows in npv reports");
    QL_REQUIRE(t0NpvReport->rows() == t1NpvLaggedReport->rows(), "different number of rows in npv reports");

    QL_REQUIRE(t0NpvReport->header(tradeIdColumn) == "TradeId", "incorrect trade id column " << tradeIdColumn);
    QL_REQUIRE(t0NpvReport->header(tradeTypeColumn) == "TradeType", "incorrect trade type column " << tradeTypeColumn);
    QL_REQUIRE(t0NpvReport->header(maturityDateColumn) == "Maturity", "incorrect maturity date column " << maturityDateColumn);
    QL_REQUIRE(t0NpvReport->header(maturityTimeColumn) == "MaturityTime", "incorrect maturity time column " << maturityTimeColumn);
    QL_REQUIRE(t0NpvReport->header(npvBaseColumn) == "NPV(Base)", "incorrect npv base column " << npvBaseColumn);
    QL_REQUIRE(t0NpvReport->header(baseCcyColumn) == "BaseCurrency", "incorrect base currency column " << baseCcyColumn);

    QL_REQUIRE(t0NpvLaggedReport->header(tradeIdColumn) == "TradeId", "incorrect trade id column " << tradeIdColumn);
    QL_REQUIRE(t0NpvLaggedReport->header(tradeTypeColumn) == "TradeType", "incorrect trade type column " << tradeTypeColumn);
    QL_REQUIRE(t0NpvLaggedReport->header(maturityDateColumn) == "Maturity", "incorrect maturity date column " << maturityDateColumn);
    QL_REQUIRE(t0NpvLaggedReport->header(maturityTimeColumn) == "MaturityTime", "incorrect maturity time column " << maturityTimeColumn);
    QL_REQUIRE(t0NpvLaggedReport->header(npvBaseColumn) == "NPV(Base)", "incorrect npv base column " << npvBaseColumn);
    QL_REQUIRE(t0NpvLaggedReport->header(baseCcyColumn) == "BaseCurrency", "incorrect base currency column " << baseCcyColumn);

    QL_REQUIRE(t1NpvLaggedReport->header(tradeIdColumn) == "TradeId", "incorrect trade id column " << tradeIdColumn);
    QL_REQUIRE(t1NpvLaggedReport->header(tradeTypeColumn) == "TradeType", "incorrect trade type column " << tradeTypeColumn);
    QL_REQUIRE(t1NpvLaggedReport->header(maturityDateColumn) == "Maturity", "incorrect maturity date column " << maturityDateColumn);
    QL_REQUIRE(t1NpvLaggedReport->header(maturityTimeColumn) == "MaturityTime", "incorrect maturity time column " << maturityTimeColumn);
    QL_REQUIRE(t1NpvLaggedReport->header(npvBaseColumn) == "NPV(Base)", "incorrect npv base column " << npvBaseColumn);
    QL_REQUIRE(t1NpvLaggedReport->header(baseCcyColumn) == "BaseCurrency", "incorrect base currency column " << baseCcyColumn);

    QL_REQUIRE(t1NpvReport->header(tradeIdColumn) == "TradeId", "incorrect trade id column " << tradeIdColumn);
    QL_REQUIRE(t1NpvReport->header(tradeTypeColumn) == "TradeType", "incorrect trade type column " << tradeTypeColumn);
    QL_REQUIRE(t1NpvReport->header(maturityDateColumn) == "Maturity", "incorrect maturity date column " << maturityDateColumn);
    QL_REQUIRE(t1NpvReport->header(maturityTimeColumn) == "MaturityTime", "incorrect maturity time column " << maturityTimeColumn);
    QL_REQUIRE(t1NpvReport->header(npvBaseColumn) == "NPV(Base)", "incorrect npv base column " << npvBaseColumn);
    QL_REQUIRE(t1NpvReport->header(baseCcyColumn) == "BaseCurrency", "incorrect base currency column " << baseCcyColumn);

    map<string, Size> t1IdMap;
    for (Size j = 0; j < t1NpvReport->rows(); ++j) {
    string tradeId = boost::get<string>(t1NpvReport->data(tradeIdColumn, j));
        t1IdMap[tradeId] = j;
    }

    std::set<string> t0Ids;
    for (Size i = 0; i < t0NpvReport->rows(); ++i) {
        try {
            string tradeId = boost::get<string>(t0NpvReport->data(tradeIdColumn, i));
            t0Ids.insert(tradeId);
            string tradeId2 = boost::get<string>(t0NpvLaggedReport->data(tradeIdColumn, i));
            string tradeId3 = boost::get<string>(t1NpvLaggedReport->data(tradeIdColumn, i));
            QL_REQUIRE(tradeId == tradeId2 && tradeId == tradeId3, "inconsistent ordering of NPV reports");
            string tradeType = boost::get<string>(t0NpvReport->data(tradeTypeColumn, i));
            Date maturityDate = boost::get<Date>(t0NpvReport->data(maturityDateColumn, i));
            Real maturityTime = boost::get<Real>(t0NpvReport->data(maturityTimeColumn, i));
            string ccy = boost::get<string>(t0NpvReport->data(baseCcyColumn, i));
            QL_REQUIRE(ccy == baseCurrency, "inconsistent NPV and base currencies");
            Real t0Npv = boost::get<Real>(t0NpvReport->data(npvBaseColumn, i));
            Real t0NpvLagged = boost::get<Real>(t0NpvLaggedReport->data(npvBaseColumn, i));
            Real t1NpvLagged = boost::get<Real>(t1NpvLaggedReport->data(npvBaseColumn, i));
            Real t1Npvt0Port = boost::get<Real>(t1Npvt0PortReport->data(npvBaseColumn, i));

            auto it = t1IdMap.find(tradeId);
            Real t1Npv = it == t1IdMap.end() ? 0.0 : boost::get<Real>(t1NpvReport->data(npvBaseColumn, it->second));
            
            Real hypotheticalCleanPnl = t0NpvLagged - t0Npv;
            Real periodFlow = aggregateTradeFlow(tradeId, startDate, endDate, t0CashFlowReport, market, configuration, baseCurrency);
            Real matured =
                (maturityDate <= endDate && close_enough(t1Npvt0Port, 0.0) && close_enough(t1Npv, 0.0)) ? t0Npv : 0.0;
            Real terminated = (close_enough(t1Npv, 0.0) && !close_enough(t1Npvt0Port, 0.0)) ? t0Npv : 0.0;
            Real amendments = (close_enough(matured, 0.0) && close_enough(terminated, 0.0)) ? t1Npv - t1Npvt0Port : 0.0;            
            Real theta = t1NpvLagged - t0Npv + periodFlow;
            Real dirtyPnl = t1Npv - t0Npv;
            Real cleanPnl = dirtyPnl + periodFlow;
            LOG("PnL report, writing line " << i << " tradeId " << tradeId);
        
            report.next()
                .add(tradeId)
                .add(tradeType)
                .add(maturityDate)
                .add(maturityTime)
                .add(startDate)
                .add(endDate)
                .add(t0Npv)
                .add(t0NpvLagged)
                .add(t1NpvLagged)
                .add(t1Npvt0Port)
                .add(t1Npv)
                .add(periodFlow)
                .add(0.0)
                .add(matured)
                .add(terminated)
                .add(amendments)
                .add(theta)
                .add(hypotheticalCleanPnl)
                .add(cleanPnl)
                .add(dirtyPnl)
                .add(ccy);

        } catch (std::exception& e) {
          ALOG("Error writing PnL report line: " << e.what()); 
        }
    }

    for (const auto& tId : t1IdMap) {
        auto it = t0Ids.find(tId.first);
        if (it == t0Ids.end()) {

          string tradeId = tId.first;
          Size loc = tId.second;
          string tradeType = boost::get<string>(t1NpvReport->data(tradeTypeColumn, loc));
          Date maturityDate = boost::get<Date>(t1NpvReport->data(maturityDateColumn, loc));
          Real maturityTime = boost::get<Real>(t1NpvReport->data(maturityTimeColumn, loc));
          string ccy = boost::get<string>(t1NpvReport->data(baseCcyColumn, loc));
          QL_REQUIRE(ccy == baseCurrency, "inconsistent NPV and base currencies");

          Real t1Npv = boost::get<Real>(t1NpvReport->data(npvBaseColumn, loc));
          LOG("PnL report, writing line " << loc << " tradeId " << tradeId);

          report.next()
              .add(tradeId)
              .add(tradeType)
              .add(maturityDate)
              .add(maturityTime)
              .add(startDate)
              .add(endDate)
              .add(0.0)
              .add(0.0)
              .add(0.0)
              .add(0.0)
              .add(t1Npv)
              .add(0.0)
              .add(t1Npv)
              .add(0.0)
              .add(0.0)
              .add(0.0)
              .add(0.0)
              .add(0.0)
              .add(t1Npv)
              .add(t1Npv)
              .add(ccy);
        }
    }

    report.end();

    LOG("PnL report written.");
}

void ReportWriter::writeXvaExplainReport(ore::data::Report& report, const ore::analytics::XvaExplainResults& xvaData) {
    try {
        report.addColumn("RiskFactor", string())
            .addColumn("TradeId", string())
            .addColumn("NettingSetId", string())
            .addColumn("CVA_Base", double(), 4)
            .addColumn("CVA", double(), 4)
            .addColumn("Change", double(), 4);

        // Add full reval report
        for (const auto& [key, baseValue] : xvaData.baseCvaData()) {
            report.next();
            auto it = xvaData.fullRevalCva().find(key);
            auto scenarioValue = it == xvaData.fullRevalCva().end() ? 0.0 : it->second;
            report.add("ALL")
                .add(key.tradeId)
                .add(key.nettingSet)
                .add(baseValue)
                .add(scenarioValue)
                .add(scenarioValue - baseValue);
        }

        for (const auto& [rfKey, scenarioValues] : xvaData.fullRevalScenarioCva()) {
            for (const auto& [key, baseValue] : xvaData.baseCvaData()) {
                report.next();
                auto it = scenarioValues.find(key);
                auto scenarioValue = it == scenarioValues.end() ? 0.0 : it->second;
                report.add(to_string(rfKey))
                    .add(key.tradeId)
                    .add(key.nettingSet)
                    .add(baseValue)
                    .add(scenarioValue)
                    .add(scenarioValue - baseValue);
            }
        }
    } catch (const std::exception& e) {
        ALOG("Failed to write xva explain report, got " << e.what());
    }
}

void ReportWriter::writeXvaExplainSummary(ore::data::Report& report, const ore::analytics::XvaExplainResults& xvaData) {
    try {
        std::map<XvaExplainResults::XvaReportKey, std::map<RiskFactorKey::KeyType, double>> aggData;

        for (auto& [rfKey, scenarioValues] : xvaData.fullRevalScenarioCva()) {
            for (const auto& [key, baseValue] : xvaData.baseCvaData()) {
                auto it = scenarioValues.find(key);
                auto scenarioValue = it == scenarioValues.end() ? 0.0 : it->second;
                auto diff = scenarioValue - baseValue;
                aggData[key][rfKey.keytype] += diff;
            }
        }

        report.addColumn("TradeId", string())
            .addColumn("NettingSet", string())
            .addColumn("CVA_Base", double(), 4)
            .addColumn("CVA", double(), 4)
            .addColumn("Change", double(), 4);

        for (const auto& keyType : xvaData.keyTypes()) {
            report.addColumn(to_string(keyType), double(), 4);
        }

        // Add full reval report
        for (const auto& [key, baseValue] : xvaData.baseCvaData()) {
            report.next();
            auto it = xvaData.fullRevalCva().find(key);
            auto scenarioValue = it == xvaData.fullRevalCva().end() ? 0.0 : it->second;
            report.add(key.tradeId)
                .add(key.nettingSet)
                .add(baseValue)
                .add(scenarioValue)
                .add(scenarioValue - baseValue);
            for (const auto& keyType : xvaData.keyTypes()) {
                report.add(aggData[key][keyType]);
            }
        }
    } catch (const std::exception& e) {
        ALOG("Error during xva explain report generation, got " << e.what());
    }
}
  
void ReportWriter::writeXmlReport(ore::data::Report& report, std::string header, std::string xml) {
    report.addColumn(header, string());
    report.next().add(xml);
    report.end();
}

void ReportWriter::writeBaCvaReport(const QuantLib::ext::shared_ptr<BaCvaCalculator>& baCvaCalculator,
                                    ore::data::Report& reportOut) {

    reportOut.addColumn("Counterparty", string())
        .addColumn("NettingSet", string())
        .addColumn("Analytic", string())
        .addColumn("Value", double(), 6);

    std::map<std::string, std::set<std::string>> counterpartyNettingSets = baCvaCalculator->counterpartyNettingSets();

    reportOut.next().add("All").add("All").add("BA_CVA_CAPITAL").add(baCvaCalculator->cvaResult());

    for (auto it = counterpartyNettingSets.begin(); it != counterpartyNettingSets.end(); it++) {
        reportOut.next().add(it->first).add("All").add("sCVA").add(baCvaCalculator->counterpartySCVA(it->first));
        reportOut.next().add(it->first).add("All").add("RiskWeight").add(baCvaCalculator->riskWeight(it->first));
        for (auto n : it->second) {
            reportOut.next().add(it->first).add(n).add("EAD").add(baCvaCalculator->EAD(n));
            reportOut.next().add(it->first).add(n).add("EffMaturity").add(baCvaCalculator->effectiveMaturity(n));
            reportOut.next().add(it->first).add(n).add("DiscountFactor").add(baCvaCalculator->discountFactor(n));
        }
    }

    reportOut.end();
}

void ReportWriter::writeCvaSensiReport(const QuantLib::ext::shared_ptr<CvaSensitivityCubeStream>& ss,
                                       ore::data::Report& reportOut) {

    LOG("Writing CVA Sensitivity Report");
    reportOut.addColumn("NettingSet", string())
        .addColumn("RiskFactor", string())
        .addColumn("ShiftType", string())
        .addColumn("ShiftSize", double(), 6)
        .addColumn("Currency", string())
        .addColumn("BaseCva", double(), 2)
        .addColumn("Delta", double(), 4);

    ss->reset();
    while (CvaSensitivityRecord sr = ss->next()) {
        reportOut.next();
        reportOut.add(sr.nettingSetId);
        reportOut.add(to_string(sr.key));
        reportOut.add(to_string(sr.shiftType));
        reportOut.add(sr.shiftSize);
        reportOut.add(sr.currency);
        reportOut.add(sr.baseCva);
        reportOut.add(sr.delta);
    }

    reportOut.end();
}

void ReportWriter::writeCvaSensiReport(const std::vector<CvaSensitivityRecord>& records,
                                       ore::data::Report& reportOut) {

    LOG("Writing CVA Sensitivity Report");
    reportOut.addColumn("NettingSet", string())
        .addColumn("RiskFactor", string())
        .addColumn("ShiftType", string())
        .addColumn("ShiftSize", double(), 6)
        .addColumn("Currency", string())
        .addColumn("BaseCva", double(), 2)
        .addColumn("Delta", double(), 4);

    for (auto sr : records) {
        reportOut.next();
        reportOut.add(sr.nettingSetId);
        reportOut.add(to_string(sr.key));
        reportOut.add(to_string(sr.shiftType));
        reportOut.add(sr.shiftSize);
        reportOut.add(sr.currency);
        reportOut.add(sr.baseCva);
        reportOut.add(sr.delta);
    }

    reportOut.end();
}

void ReportWriter::writeSaCvaSensiReport(const SaCvaNetSensitivities& sensis,
                                         ore::data::Report& reportOut) {

    LOG("Writing SA CVA Sensitivity Report");
    reportOut.addColumn("NettingSet", string())
        .addColumn("RiskType", string())
        .addColumn("CvaType", string())
        .addColumn("MarginType", string())
        .addColumn("RiskFactor", string())
        .addColumn("Bucket", string())
        .addColumn("Value", double(), 4);

    for (auto sr : sensis) {
        reportOut.next();
        reportOut.add(sr.nettingSetId);
        reportOut.add(to_string(sr.riskType));
        reportOut.add(to_string(sr.cvaType));
        reportOut.add(to_string(sr.marginType));
        reportOut.add(sr.riskFactor);
        reportOut.add(sr.bucket);
        reportOut.add(sr.value);
    }

    reportOut.end();
}
  
} // namespace analytics
} // namespace ore
