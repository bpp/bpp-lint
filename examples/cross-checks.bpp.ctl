* A BPP control file constructed to exercise the cross-keyword consistency
* rules (codes BPP120-BPP127). Running:
*   ./bpp-lint --codes examples/cross-checks.bpp.ctl
* should report errors:
*   [120] speciesmodelprior = 2 in A10 mode
*   [122] bayesfactorbeta = 0.5 with usedata = 0
*   [123] cleandata = 1 with an unphased species
*   [124] datefile combined with speciesdelimitation = 1

         seqfile = frogs.txt
        imapfile = frogs.imap.txt
         jobname = frogs

* A10 analysis: delimitation only, but speciesmodelprior = 2 (uniformSLH) is
* only legal in A11. Expected: [120].
  speciesdelimitation = 1 0 0.0001
          speciestree = 0
    speciesmodelprior = 2

       species&tree = 4  K  L  N  M
                          5  4  3  2
                       (K, (L, (N, M)));

* usedata = 0 disables the data likelihood; a non-default bayesfactorbeta
* has nothing to scale. Expected: [122].
         usedata = 0
 bayesfactorbeta = 0.5

* cleandata strips ambiguity codes; species 3 is unphased and relies on
* them. Expected: [123].
       cleandata = 1
           phase = 0 0 1 0

* datefile (tip dating) is not compatible with species delimitation, but
* the locusrate = 3 parameterisation correctly pairs with the datefile so
* BPP125/BPP126 are silent. Expected: [124] only.
        datefile = dates.txt
       locusrate = 3 20 1000000

           nloci = 5
         nsample = 100
      thetaprior = invgamma 3 0.002
        tauprior = invgamma 3 0.04
