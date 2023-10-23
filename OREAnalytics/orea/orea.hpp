// Autogenerated by cmake
// Do not edit

#ifdef BOOST_MSVC
#include <orea/auto_link.hpp>
#endif

#include <orea/aggregation/collateralaccount.hpp>
#include <orea/aggregation/collatexposurehelper.hpp>
#include <orea/aggregation/creditmigrationcalculator.hpp>
#include <orea/aggregation/creditmigrationhelper.hpp>
#include <orea/aggregation/creditsimulationparameters.hpp>
#include <orea/aggregation/cvaspreadsensitivitycalculator.hpp>
#include <orea/aggregation/dimcalculator.hpp>
#include <orea/aggregation/dimregressioncalculator.hpp>
#include <orea/aggregation/dynamiccreditxvacalculator.hpp>
#include <orea/aggregation/exposureallocator.hpp>
#include <orea/aggregation/exposurecalculator.hpp>
#include <orea/aggregation/nettedexposurecalculator.hpp>
#include <orea/aggregation/postprocess.hpp>
#include <orea/aggregation/staticcreditxvacalculator.hpp>
#include <orea/aggregation/xvacalculator.hpp>
#include <orea/app/analytic.hpp>
#include <orea/app/analytics/parconversionanalytic.hpp>
#include <orea/app/analytics/pricinganalytic.hpp>
#include <orea/app/analytics/scenariostatisticsanalytic.hpp>
#include <orea/app/analytics/simmanalytic.hpp>
#include <orea/app/analytics/varanalytic.hpp>
#include <orea/app/analytics/xvaanalytic.hpp>
#include <orea/app/analyticsmanager.hpp>
#include <orea/app/inputparameters.hpp>
#include <orea/app/marketcalibrationreport.hpp>
#include <orea/app/marketdatacsvloader.hpp>
#include <orea/app/marketdatainmemoryloader.hpp>
#include <orea/app/marketdataloader.hpp>
#include <orea/app/oreapp.hpp>
#include <orea/app/parameters.hpp>
#include <orea/app/reportwriter.hpp>
#include <orea/app/sensitivityrunner.hpp>
#include <orea/app/structuredanalyticserror.hpp>
#include <orea/app/structuredanalyticswarning.hpp>
#include <orea/app/xvarunner.hpp>
#include <orea/app/zerosensitivityloader.hpp>
#include <orea/cube/cube_io.hpp>
#include <orea/cube/cubecsvreader.hpp>
#include <orea/cube/cubeinterpretation.hpp>
#include <orea/cube/cubewriter.hpp>
#include <orea/cube/inmemorycube.hpp>
#include <orea/cube/jaggedcube.hpp>
#include <orea/cube/jointnpvcube.hpp>
#include <orea/cube/jointnpvsensicube.hpp>
#include <orea/cube/npvcube.hpp>
#include <orea/cube/npvsensicube.hpp>
#include <orea/cube/sensicube.hpp>
#include <orea/cube/sensitivitycube.hpp>
#include <orea/cube/sparsenpvcube.hpp>
#include <orea/engine/amcvaluationengine.hpp>
#include <orea/engine/bufferedsensitivitystream.hpp>
#include <orea/engine/cptycalculator.hpp>
#include <orea/engine/filteredsensitivitystream.hpp>
#include <orea/engine/historicalpnlgenerator.hpp>
#include <orea/engine/historicalsensipnlcalculator.hpp>
#include <orea/engine/historicalsimulationvar.hpp>
#include <orea/engine/mporcalculator.hpp>
#include <orea/engine/multistatenpvcalculator.hpp>
#include <orea/engine/multithreadedvaluationengine.hpp>
#include <orea/engine/npvrecord.hpp>
#include <orea/engine/observationmode.hpp>
#include <orea/engine/parametricvar.hpp>
#include <orea/engine/parsensitivityanalysis.hpp>
#include <orea/engine/parsensitivitycubestream.hpp>
#include <orea/engine/riskfilter.hpp>
#include <orea/engine/sensitivityaggregator.hpp>
#include <orea/engine/sensitivityanalysis.hpp>
#include <orea/engine/sensitivitycubestream.hpp>
#include <orea/engine/sensitivityfilestream.hpp>
#include <orea/engine/sensitivityinmemorystream.hpp>
#include <orea/engine/sensitivityrecord.hpp>
#include <orea/engine/sensitivitystream.hpp>
#include <orea/engine/stresstest.hpp>
#include <orea/engine/valuationcalculator.hpp>
#include <orea/engine/valuationengine.hpp>
#include <orea/engine/varcalculator.hpp>
#include <orea/engine/zerotoparcube.hpp>
#include <orea/scenario/aggregationscenariodata.hpp>
#include <orea/scenario/clonedscenariogenerator.hpp>
#include <orea/scenario/clonescenariofactory.hpp>
#include <orea/scenario/crossassetmodelscenariogenerator.hpp>
#include <orea/scenario/csvscenariogenerator.hpp>
#include <orea/scenario/deltascenario.hpp>
#include <orea/scenario/deltascenariofactory.hpp>
#include <orea/scenario/historicalscenariofilereader.hpp>
#include <orea/scenario/historicalscenariogenerator.hpp>
#include <orea/scenario/historicalscenarioloader.hpp>
#include <orea/scenario/historicalscenarioreader.hpp>
#include <orea/scenario/lgmscenariogenerator.hpp>
#include <orea/scenario/scenario.hpp>
#include <orea/scenario/scenariofactory.hpp>
#include <orea/scenario/scenariofilter.hpp>
#include <orea/scenario/scenariogenerator.hpp>
#include <orea/scenario/scenariogeneratorbuilder.hpp>
#include <orea/scenario/scenariogeneratordata.hpp>
#include <orea/scenario/scenariogeneratortransform.hpp>
#include <orea/scenario/scenarioshiftcalculator.hpp>
#include <orea/scenario/scenariosimmarket.hpp>
#include <orea/scenario/scenariosimmarketparameters.hpp>
#include <orea/scenario/scenariowriter.hpp>
#include <orea/scenario/sensitivityscenariodata.hpp>
#include <orea/scenario/sensitivityscenariogenerator.hpp>
#include <orea/scenario/shiftscenariogenerator.hpp>
#include <orea/scenario/simplescenario.hpp>
#include <orea/scenario/simplescenariofactory.hpp>
#include <orea/scenario/stressscenariodata.hpp>
#include <orea/scenario/stressscenariogenerator.hpp>
#include <orea/simm/crifloader.hpp>
#include <orea/simm/crifrecord.hpp>
#include <orea/simm/simmbasicnamemapper.hpp>
#include <orea/simm/simmbucketmapper.hpp>
#include <orea/simm/simmbucketmapperbase.hpp>
#include <orea/simm/simmcalculator.hpp>
#include <orea/simm/simmconcentration.hpp>
#include <orea/simm/simmconcentrationisdav1_3.hpp>
#include <orea/simm/simmconcentrationisdav1_3_38.hpp>
#include <orea/simm/simmconcentrationisdav2_0.hpp>
#include <orea/simm/simmconcentrationisdav2_1.hpp>
#include <orea/simm/simmconcentrationisdav2_2.hpp>
#include <orea/simm/simmconcentrationisdav2_3.hpp>
#include <orea/simm/simmconcentrationisdav2_3_8.hpp>
#include <orea/simm/simmconcentrationisdav2_5.hpp>
#include <orea/simm/simmconcentrationisdav2_5a.hpp>
#include <orea/simm/simmconcentrationisdav2_6.hpp>
#include <orea/simm/simmconfiguration.hpp>
#include <orea/simm/simmconfigurationbase.hpp>
#include <orea/simm/simmconfigurationisdav1_0.hpp>
#include <orea/simm/simmconfigurationisdav1_3.hpp>
#include <orea/simm/simmconfigurationisdav1_3_38.hpp>
#include <orea/simm/simmconfigurationisdav2_0.hpp>
#include <orea/simm/simmconfigurationisdav2_1.hpp>
#include <orea/simm/simmconfigurationisdav2_2.hpp>
#include <orea/simm/simmconfigurationisdav2_3.hpp>
#include <orea/simm/simmconfigurationisdav2_3_8.hpp>
#include <orea/simm/simmconfigurationisdav2_5.hpp>
#include <orea/simm/simmconfigurationisdav2_5a.hpp>
#include <orea/simm/simmconfigurationisdav2_6.hpp>
#include <orea/simm/simmnamemapper.hpp>
#include <orea/simm/simmresults.hpp>
#include <orea/simm/utilities.hpp>
#include <orea/simulation/fixingmanager.hpp>
#include <orea/simulation/simmarket.hpp>
#include <orea/version.hpp>
