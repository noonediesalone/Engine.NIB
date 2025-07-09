/*
 Copyright (C) 2022 Quaternion Risk Management Ltd
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

/*! \file orea/app/marketcalibrationreport.hpp
    \brief ORE Analytics Manager
*/
#pragma once

#include <ored/marketdata/market.hpp>
#include <ored/marketdata/todaysmarketcalibrationinfo.hpp>
#include <ored/marketdata/todaysmarketparameters.hpp>
#include <ored/report/report.hpp>

namespace ore {
namespace analytics {
class MarketCalibrationReportBase {
public:

    struct CalibrationFilters {
        CalibrationFilters() {};        
        CalibrationFilters(const std::string& calibrationFilter) {
            if (!calibrationFilter.empty()) {
                std::string tmp = calibrationFilter;
                boost::algorithm::to_upper(tmp);
                std::vector<std::string> tokens;
                boost::split(tokens, tmp, boost::is_any_of(","), boost::token_compress_on);
                mdFilterFixings = std::find(tokens.begin(), tokens.end(), "FIXINGS") != tokens.end();
                mdFilterMarketData = std::find(tokens.begin(), tokens.end(), "MARKETDATA") != tokens.end();
                mdFilterCurves = std::find(tokens.begin(), tokens.end(), "CURVES") != tokens.end();
                mdFilterInfCurves = std::find(tokens.begin(), tokens.end(), "INFLATIONCURVES") != tokens.end();
                mdFilterCommCurves = std::find(tokens.begin(), tokens.end(), "COMMODITYCURVES") != tokens.end();
                mdFilterFxVols = std::find(tokens.begin(), tokens.end(), "FXVOLS") != tokens.end();
                mdFilterEqVols = std::find(tokens.begin(), tokens.end(), "EQVOLS") != tokens.end();
                mdFilterIrVols = std::find(tokens.begin(), tokens.end(), "IRVOLS") != tokens.end();
                mdFilterCommVols = std::find(tokens.begin(), tokens.end(), "COMMVOLS") != tokens.end();
                mdFilterCpiVols = std::find(tokens.begin(), tokens.end(), "CPIVOLS") != tokens.end();
            }
        }

        bool mdFilterFixings = true;
        bool mdFilterMarketData = true;
        bool mdFilterCurves = true;
        bool mdFilterInfCurves = true;
        bool mdFilterCommCurves = true;
        bool mdFilterFxVols = true;
        bool mdFilterEqVols = true;
        bool mdFilterIrVols = true;
        bool mdFilterCommVols = true;
        bool mdFilterCpiVols = true;
    };

    MarketCalibrationReportBase(const std::string& calibrationFilter);
    virtual ~MarketCalibrationReportBase() = default;

    virtual void initialise(const std::string& label) {};

    // Add yield curve data to array
    void addYieldCurve(const QuantLib::Date& refdate,
                       QuantLib::ext::shared_ptr<ore::data::YieldCurveCalibrationInfo> yts, const std::string& name,
                       bool isDiscount, const std::string& label, QuantLib::Handle<QuantLib::IborIndex> iborIndex = QuantLib::Handle<QuantLib::IborIndex>());
    virtual void
    addYieldCurveImpl(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::YieldCurveCalibrationInfo> yts, const std::string& name,
                      bool isDiscount, const std::string& label, const std::string& type,
                      QuantLib::Handle<QuantLib::IborIndex> iborIndex = QuantLib::Handle<QuantLib::IborIndex>()) = 0;

    // Add inflation curve data to array
    void addInflationCurve(const QuantLib::Date& refdate,
                                   QuantLib::ext::shared_ptr<ore::data::InflationCurveCalibrationInfo> yts,
                                   const std::string& name, const std::string& label);
    virtual void addInflationCurveImpl(const QuantLib::Date& refdate,
                                   QuantLib::ext::shared_ptr<ore::data::InflationCurveCalibrationInfo> yts,
                                       const std::string& name, const std::string& label, const std::string& type) = 0;

    void addCommodityCurve(const QuantLib::Date& refdate,
                                   QuantLib::ext::shared_ptr<ore::data::CommodityCurveCalibrationInfo> yts,
                                   std::string const& name, std::string const& label);
    virtual void addCommodityCurveImpl(const QuantLib::Date& refdate,
                                   QuantLib::ext::shared_ptr<ore::data::CommodityCurveCalibrationInfo> yts,
                                    std::string const& name, std::string const& label, const std::string& type) = 0;

    // Add fx/eq/comm vol curve data to array
    void addFxVol(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::FxEqCommVolCalibrationInfo> vol,
                            const std::string& name, const std::string& label);
    void addEqVol(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::FxEqCommVolCalibrationInfo> vol,
                            const std::string& name, const std::string& label);
    void addCommVol(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::FxEqCommVolCalibrationInfo> vol,
                            const std::string& name, const std::string& label);

    void addEqFxVol(const std::string& type, QuantLib::ext::shared_ptr<ore::data::FxEqCommVolCalibrationInfo> vol,
                    const std::string& id, const std::string& label);
    virtual void addEqFxVolImpl(const std::string& type, QuantLib::ext::shared_ptr<ore::data::FxEqCommVolCalibrationInfo> vol,
                                const std::string& id, const std::string& label) = 0;

    // Add ir vol curve data to array
    void addIrVol(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::IrVolCalibrationInfo> vol,
                          const std::string& name, const std::string& label);
    virtual void addIrVolImpl(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::IrVolCalibrationInfo> vol, const std::string& name,
                              const std::string& label, const std::string& type) = 0;

    // Add cpi vol curve data to array
    void addCpiVol(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::CpiVolCalibrationInfo> vol,
        const std::string& name, const std::string& label);

    virtual void addCpiVolImpl(const QuantLib::Date& refdate,
                           QuantLib::ext::shared_ptr<ore::data::CpiVolCalibrationInfo> vol, const std::string& name,
                           const std::string& label, const std::string& type) = 0;

    // populate the calibration reports
    virtual void populateReport(const QuantLib::ext::shared_ptr<ore::data::Market>& market,
                                const QuantLib::ext::shared_ptr<ore::data::TodaysMarketParameters>& todaysMarketParams,
                                const std::string& label = std::string());

    // write out to file, should be overwritten in derived classes
    virtual QuantLib::ext::shared_ptr<ore::data::Report> outputCalibrationReport() = 0;

private:
    CalibrationFilters calibrationFilters_;

    // a map of already reported calibrations
    const bool checkCalibrations(std::string label, std::string type, std::string id) const;
    std::map<std::string, std::map<std::string, std::set<std::string>>> calibrations_;
};

class MarketCalibrationReport : public MarketCalibrationReportBase {
public:
    MarketCalibrationReport(const std::string& calibrationFilter,
                            const QuantLib::ext::shared_ptr<ore::data::Report>& report);
    
    QuantLib::ext::shared_ptr<ore::data::Report> outputCalibrationReport() override;

    void addYieldCurveImpl(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::YieldCurveCalibrationInfo> yts, const std::string& name, bool isDiscount, 
        const std::string& label, const std::string& type,
         QuantLib::Handle<QuantLib::IborIndex> iborIndex = QuantLib::Handle<QuantLib::IborIndex>()) override;

    // Add inflation curve data to array
    void addInflationCurveImpl(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::InflationCurveCalibrationInfo> yts, 
                               const std::string& name, const std::string& label, const std::string& type) override;

    // Add commodity curve data to array
    void addCommodityCurveImpl(const QuantLib::Date& refdate,
                           QuantLib::ext::shared_ptr<ore::data::CommodityCurveCalibrationInfo> yts, const std::string& name, 
                           const std::string& label, const std::string& type) override;

    // Add fx/eq/comm vol curve data to array
    void addEqFxVolImpl(const std::string& type,
                        QuantLib::ext::shared_ptr<ore::data::FxEqCommVolCalibrationInfo> vol,
                        const std::string& id, const std::string& label) override;

    // Add ir vol curve data to array
    void addIrVolImpl(const QuantLib::Date& refdate, QuantLib::ext::shared_ptr<ore::data::IrVolCalibrationInfo> vol,
                      const std::string& name, const std::string& label, const std::string& type) override;

    // Add cpi vol curve data to array
    virtual void addCpiVolImpl(const QuantLib::Date& refdate,
                               QuantLib::ext::shared_ptr<ore::data::CpiVolCalibrationInfo> vol, const std::string& name,
                               const std::string& label, const std::string& type) override;

private:
     QuantLib::ext::shared_ptr<ore::data::Report> report_;

     void addRowReport(const std::string& moType, const std::string& moId, const std::string& resId,
                       const std::string& key1, const std::string& key2, const std::string& key3,
                       const boost::any& value);
};

} // namespace analytics
} // namespace ore
