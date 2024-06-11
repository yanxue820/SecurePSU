The implementation of \[[Scalable Private Set Union, with Stronger Security](https://eprint.iacr.org/2024/922)\], accepted at USENIX Security 2024.

Code based on the implementation of 2-Party Circuit-PSI available at \[[https://github.com/shahakash28/2PC-Circuit-PSI](https://github.com/shahakash28/2PC-Circuit-PSI)\] and Oblivious Shuffle Network from \[[https://github.com/dujiajun/PSU](https://github.com/dujiajun/PSU)\].

## Required packages:
 - g++ (version >=8)
 - libboost-all-dev (version >=1.74)
 - libgmp-dev
 - libssl-dev
 - libntl-dev
 - pkg-config
 - libglib2.0-dev

## Compilation
```
mkdir build
cd build
cmake ..
make
```

## Run
Run from `build` directory.
Example:
```
Server: bin/gcf_psi -r 0 -p 31000 -n 1048576
Client: bin/gcf_psi -r 1 -p 31000 -n 1048576 
```
Description of Parameters:
```
-r: role (0: Server/1: Client)
-p: port number
-n: number of elements in input set
```

## Execution Environment
The code was tested on Ubuntu 22.04.3 LTS.

