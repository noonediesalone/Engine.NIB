set datafile sep ","
set decimal locale
set format y "%'.0f"

set title "Example 2"
set xlabel "Time / Years"
set ylabel "Exposure"

set style line 1 linecolor rgb '#0060ad' linetype 1 linewidth 3
set style line 2 linecolor rgb '#dd181f' linetype 1 linewidth 3
set style line 3 linecolor rgb '#008000' linetype 1 linewidth 3
set style line 4 linecolor rgb '#990099' linetype 1 linewidth 3

set autoscale

set term aqua
plot "exposure_trade_FXFWD_EURUSD_10Y.csv" us 3:4 title "EPE" w l ls 1
replot "exposure_trade_FXFWD_EURUSD_10Y.csv" us 3:5 title "ENE" w l ls 2
replot "call.csv" us 1:2 title "Call Price" w l ls 3
replot "put.csv" us 1:2 title "Put Price" w l ls 4

