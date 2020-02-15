# Statistical SAT Attack - StatSAT

This is a SAT-based attack on probabilistic circuits which have been secured by locking. The code has been developed on top of the PSAT (Probabilistic SAT) attack developed by Satwik Patnaik and others. PSAT is located at https://github.com/DfX-NYUAD/PSAT

The source code is provided in */source/src* and the binary executable is also located there. Some benchmarks in available in the */benchmarks* folder. Subfolder */ORIGINAL* has the original circuit netlist files, and */gate_errors* contains gate-level error probabilities for different circuits. Other subfolders have the locked circuits along with their known correct key (since locked circuit fitted with correct key is used as an oracle). Also, */benchmarks/gate_errors/probabilistic* contains *gen_stoch_file_prob.py* which can be used to create the .stoch files listing the gate-level error probabilities. This file starts with something of this sort:

# OUTPUT_SAMPLING_ON OUTPUT_SAMPLING_ITERATIONS OUTPUT_SAMPLING_FOR_TEST_ON TEST_PATTERNS
true 500 true 2000
# GATE_NAME ERROR_RATE (%)
NEXT_GATE
n1358 1.25
NEXT_GATE
n2903 1.25

Here 500 and 2000 represent parameters Ns and Neval of the attack. And "n1358 1.25" means that gate n1358 has error of 1.25%.


The binary has been tested on 64-bit Ubuntu 16.04 LTS. To run the binary, default C++ libraries are required.

To invoke the attack, go to */source/src* and use the *sld* command with the folllowing syntax:

	./sld  <path_to_locked_circuit> <path_to_original_circuit> <partial_path_to_stoch_file>
	
An example would be 
    ./sld ../../benchmarks/SFLL/b14/b14_sfllhd6_k16.bench ../../benchmarks/ORIGINAL/b14_oracle.bench b14_oracle/error_0_2/b14_sfllhd6_k16.bench.stoch
    
which attacks the b14 circuit locked with SFLL-HD6 and having gate-level error probability of 0.2%

When the command is run, the user is asked whether the attacker should estimate error probabilites. It also provides for the option to modify the default attack parameters. The attack can be run directly or by pausing at the end of every iteration.

The last line displayed in the command line specifies the best key found.


