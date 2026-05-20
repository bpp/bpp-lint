* A BPP control file written for BPP 3.x — used here as a fixture for
* the linter. Running:  ./bpp-lint examples/legacy-3x.bpp.ctl

          seed =  -1

       seqfile = frogs.txt
       Imapfile = frogs.imap.txt
       outfile = out.txt
      mcmcfile = mcmc.txt

* species delimitation + species-tree estimation flags
  speciesdelimitation = 0
          speciestree = 0
   uniformrootedtrees = 1   * removed in BPP 4.x — should become speciesmodelprior

       species&tree = 4  K  L  N  M
                          5  4  3  2
                       (K, (L, (N, M)));

      diploid = 0 0 0 0   * renamed to phase

        usedata = 1
          nloci = 5
      cleandata = 0

* the BPP-3.x bare-numeric thetaprior with alpha <= 2 (will be rejected
* by BPP 4.8.2+; the linter should flag it)
     thetaprior = 2 1000

* BPP-3.x tauprior with three numeric tokens (third is silently ignored
* in 4.x)
       tauprior = 3 0.002 0.0001

* legacy single-bit print
          print = 1
         burnin = 8000
       sampfreq = 2
        nsample = 100000

* a removed feature: the genotyping-error block (one matrix row per species)
   sequenceerror = 4
                  0.01 0 0 0
                  0 0.01 0 0
                  0 0 0.01 0
                  0 0 0 0.01
