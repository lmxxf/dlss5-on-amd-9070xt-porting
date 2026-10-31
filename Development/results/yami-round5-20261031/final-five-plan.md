# Final five-arm qualification (fixed before data)

This supersedes the prepared but NEVER RUN final-panel-long.json (four arms). Add the missing C256-only composition to complete the constructive2×2, avoiding an assumption that norm900 and C256 gains add.

Arms: stock b60; base=ViTperm+C512clamp; norm=base+guardeddotparONLYnorm900; c256=base+C256one-planeRC; full=bothadditions. Same stock hosts. Sevenprofiles,3rotatedsymmetric10-slotrounds,800framesdiscard160,3840samples/arm/profile,210slots. Inputs/fullassettreespre-pinned; nootherremoteoperationsduringtiming. AllfourcandidatepayloadspasssevenGoldfirst.

Common acceptance:all7pooledmeanregressions<=0.5%,pooledp99regressions<=1%,no3/3roundmeanregression>0.5%;sum7mean deltas<0;Small1080all3mean/p99improvevsstock. Norm-bearingpayloads additionallyrequireOriginal960all3mean/p99improve. C256-bearingpayloads additionallyrequireOriginal1088all3meanimproveandpooledp99notworse.

A norm addition is justified only when its conditionalcomparison(norm-base orfull-c256)winsOriginal960meanall3andpooledp99,andall7noninferioritybounds. A C256addition is justified only when its conditionalcomparison(c256-base orfull-norm)winsmeanall3onBOTHOriginal1088andSmall1080,pooledp99notworseonboth,andall7bounds.

Eligiblefullmustjustifybothconditionaladditions;otherwiseeligiblepartialpayloadsjustifytheirrespectiveaddition. Amongeligiblepayloadschoosemostnegativeequal-weightedsum7mean deltas;preferfewerchangesifwithin1us sum. Ifnoneeligible,retainb60. Keepallrawresults;noidenticalreruntoerasefailures. Reportnoninferiorityasnoninferiority,not7strictwins;theseareengineeringgates,notglobalperformance-ceilingproof.
