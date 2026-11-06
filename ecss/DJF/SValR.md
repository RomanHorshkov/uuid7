# Software validation report with respect to the TS {#UUID7_DOC_SVALR}

<!-- ecss:docinfo id=UUID7-DOC-SVALR -->

The validation of uuid7 against its technical specification, as the \ref UUID7_DOC_SVALP "SValP" plans and the \ref UUID7_DOC_SVS "SVS" specifies, for the commit above (ECSS-E-ST-40 5.6.3.2a; no DRD). It is not a certificate of conformance (ECSS-Q-ST-80 7.3.7): that is issued for a released version after its AR (\ref UUID7_DOC_SDP "SDP" <4.6>, OI-03).

## Summary

<!-- ecss:summary -->

## Results per requirement

<!-- ecss:req-results -->

## Performance

<!-- ecss:budgets -->

## Installation {#svalr_installation}

The installation report (ECSS-E-ST-40 5.7.2.4): the pipeline stage `package` builds both debs, the stage `package-smoke` extracts them into `build/smoke` and compiles and runs `tests/package/smoke.c` against the extracted header and libraries only, never the build tree.

<!-- ecss:install -->

## Gates of the pipeline

<!-- ecss:pipeline -->
