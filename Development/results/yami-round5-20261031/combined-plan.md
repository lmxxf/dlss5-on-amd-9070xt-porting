Stock-host three-arm held-out test (fixed before measurements)

A=stockb60;B=C512probability-clamp-only;C=B+fixedViTperm. C512+denominator was rejected (+2.900usmean,+18.833usp99 vsB,3/3meansregress),excluded. All arms use identical stock hosts. No new export, host selector, function-map key or runtime structure. B has passed independentSmalllong;C has no priorcombinedperformance data. Never add historical timings to predictC.

Sevenprofiles,three symmetric six-slot rounds with rotations:A B C C B A;B C A A C B;C A B B A C.800frames/slot,discard160,3840samples/arm/profile.126slots total. This supersedes the prepared but NEVER EXECUTED two-arm combined-long.json; do not run both.15swatchdogretained; nootherremoteoperationsduringtiming.

Each candidate vsA benefit gate:Small1080all3mean/p99negative,Original1088pooledmeannegative,andsumofsevenpooledmean deltasnegative. Allseven:pooledmeanregression<=0.5%,pooledp99regression<=1%,notall3roundmeansregress>0.5%. These are engineering noninferiority thresholds, not sevenstrictwins or proof ofzero performance regression. Prefer simplerB ifbothpass,unlessC additionallywinsSmall1080meanall3rounds andpooledp99 vsB with nootherprofileexceedingthesamemean/tailbounds. Ifgatesfail,do notpromote orrepeatidenticalbinary toselectawinner.
