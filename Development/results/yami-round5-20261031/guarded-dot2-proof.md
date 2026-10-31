Guarded Softmax denominator experiment

Swin R4 exponent map yields positive normalF16 with low5fractionbitszero,fields1<=E<=18 (max0x48e0). Each value is an integer multiple of2^(Emin-20); sumof64positivevalues can be accumulated exactly inFP32 if below2^24suchunits, i.e.den<2^(Emin+4). A cheap sufficientguard isden<32 (globalEmin>=1).

Compute16dot2F16-with-ones perlane over32keys,then add lane^16 partialsum. For positive lattice inputs, if the computed sum isbelowthreshold, it cannot have crossedthresholdandroundedbackbelow:FP32positiveadditionis monotone andthresholdispoweroftwo. Thus everypartialsum isexact. WMMAwithoneshasthesamesumunderthisguard regardless ofitsreductionorder. Ifden>=32,compute packed-halfminimum andtestthelargerper-querylatticebound. All32lanes mustpass viawaveballot; otherwise executeoriginal4WMMAdenominator. Never change the squared-norm reductions, whose inputs do not have this latticeprecondition.

Needactualgfx1201probe first:2^20lanes,4patternfamilies includingfullrange/adversariallarge+tiny,poisonedoutput+per-indexstamps;compareguardedresultagainstoriginalWMMA,verifyallthreetiersandreplicatedsums. ThenGold7beforeFrame. Do not assume a faster orcorrectintrinsicfrominstructioncountalone.
