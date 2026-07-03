* A BPP control file constructed to exercise the migration (MSC-M) block
* rules (codes BPP140-BPP145). Running:
*   ./bpp-lint --codes examples/migration.bpp.ctl
* should report errors:
*   [142] migration to 'Z', which is not a species-tree population
*   [143] migration from 'A' to itself
*   [144] three populations on one row ('A B C')
*   [145] migration between 'A' and its ancestor 'S' (never coexist)
*   [146] a duplicate 'A -> C' band (warning)
*   [141] a row missing its target population
*   [140] migration = 9 declared, but only 8 band rows are present
*
* The species tree labels the internal nodes S, T and R; ancestral nodes used
* in migration must be labelled (the auto-generated 'A,B'-style labels cannot
* be referenced because ',' is a token delimiter).

         seqfile = frogs.txt
        imapfile = frogs.imap.txt
         jobname = frogs

    species&tree = 4  A  B  C  D
                      5  4  3  2
                   ((A, B)S, (C, D)T)R;

           nloci = 5
         nsample = 100
      thetaprior = invgamma 3 0.002
        tauprior = invgamma 3 0.04
          wprior = 2 200

* Each row is 'source target' plus optional numeric rate parameters. The
* first two rows are valid; the rest each trip one rule.
       migration = 9
                   A C          * valid: tip A -> tip C
                   S C          * valid: labelled ancestor S -> tip C
                   A Z          * [142] Z is not in the species tree
                   A A          * [143] self-migration
                   A B C        * [144] three populations on one row
                   A S          * [145] A and its ancestor S never coexist
                   A C          * [146] duplicate of the first band (warning)
                   A            * [141] missing target population
* only eight band rows above for a declared count of 9 -> [140]
