* A clean BPP 4.x control file. Running this through the linter should
* produce no errors.

          seed = -1
       seqfile = frogs.txt
      imapfile = frogs.imap.txt
       jobname = frogs

  speciesdelimitation = 0
          speciestree = 0
    speciesmodelprior = 1

       species&tree = 4  K  L  N  M
                          5  4  3  2
                       (K, (L, (N, M)));

         phase = 0 0 0 0

       usedata = 1
         nloci = 5
     cleandata = 0

* thetaprior using invgamma with alpha > 2
    thetaprior = invgamma 3 0.002

* tauprior with explicit distribution
      tauprior = invgamma 3 0.04

      finetune = 1 Gage:5 Gspr:0.001 tau:0.001 mix:0.3 lrht:0.33

         print = 1 0 0 0 0
        burnin = 8000
      sampfreq = 2
       nsample = 100000
