# Notes on Response code

## main

File name: `adrianxy.cc`

### Programming Steps

```pseudocode
Init MADNESS MPI
READ INPUT
   Throw error if not found
CREATE TDHF object by passing madness world and reading input
    TDHF my_calc(&: world, input);
if property
    if polarizability --mycalc.solve_polarizability
    ---future properties
else 
    solve response states
FINALIZE 
```



## class TDHF { 

### Public:

    
ResponseParameters : Rparams hold all the user inputs

### Private:

Ground Parameters: Gparams object hold all variables needed from ground state calculation

Tensor<double> omega $\omega$
Tensor<double> e_residuals
    

    



