# Statistical SAT Attack - StatSAT

This is a SAT-based attack on probabilistic circuits which have been secured by logic locking. The code has been developed on top of the PSAT (Probabilistic SAT) attack developed by Satwik Patnaik and others. PSAT is located at https://github.com/DfX-NYUAD/PSAT

The source code is provided in */source/src* and the binary executable is also located there. Some benchmarks are available in the */benchmarks* folder. Subfolder */ORIGINAL* has the original circuit netlist files, and */gate_errors* contains gate-level error probabilities for different circuits. Other subfolders have the locked circuits along with their known correct key (since locked circuit fitted with correct key is used as an oracle). Also, */benchmarks/gate_errors/probabilistic* contains *gen_stoch_file_prob.py* which can be used to create the .stoch files listing the gate-level error probabilities. This file starts with something of this sort:

	OUTPUT_SAMPLING_ON OUTPUT_SAMPLING_ITERATIONS OUTPUT_SAMPLING_FOR_TEST_ON TEST_PATTERNS
	true 500 true 2000
	GATE_NAME ERROR_RATE (%)
	NEXT_GATE
	n1358 1.25
	NEXT_GATE
	n2903 1.25

Here 500 and 2000 represent parameters Ns and Neval of the attack. And "n1358 1.25" means that gate n1358 has error of 1.25%.


The binary has been tested on 64-bit Ubuntu 16.04 LTS. To run the binary, default C++ libraries are required.

To invoke the attack, go to */source/src* and use the *sld* command with the following syntax:

	./sld  <path_to_locked_circuit> <path_to_original_circuit> <partial_path_to_stoch_file>
	
An example would be 

    ./sld ../../benchmarks/SFLL/b14/b14_sfllhd6_k16.bench ../../benchmarks/ORIGINAL/b14_oracle.bench b14_oracle/error_0_2/b14_sfllhd6_k16.bench.stoch
    
which attacks the b14 circuit locked with SFLL-HD6 and having gate-level error probability of 0.2%

When the command is run, the user is asked whether the attacker should estimate error probabilites. It also provides for the option to modify the default attack parameters. The attack can be run directly or by pausing at the end of every iteration.

The last line displayed in the command line specifies the best key found.

# High Error Rate Keys - HERK

In order to mitigate SAT-based attacks against probablistic circuits, the High Error Rate Keys (HERK) technique was developed. HERKs function by hiding the correct obfuscation key under regions/nodes of high stochastic noise within the circuit. By doing so, it becomes extremely hard for the attacker to infer the correct function of the circuit, thwarting SAT-based attacks on probablistic circuits, such as Stat-SAT or PSAT. This work has been published in the TCAD manuscript cited below. We direct the interested reader to this manuscript for additional details and theory regarding the HERK technique.


	Zuzak, M., Mondal, A., & Srivastava, A. (2021). Evaluating the Security of Logic-Locked Probabilistic Circuits. IEEE Transactions on Computer-Aided Design of Integrated Circuits and Systems.

This work can also be found online at: https://ieeexplore.ieee.org/abstract/document/9512283

The StatSAT Attack engine has been updated to automatically identify HERK insertion points within benchmark circuits. In order to do so, the -H flag has been added. This flag can be used with the ./sld command, which is located in */source/src*. This command must be run with the following arguments in order to identify HERK insertion locations within a given benchmark circuit:

	./sld -H 1 <path_to_locked_circuit> <path_to_original_circuit> <partial_path_to_stoch_file>
	
Note that HERKs are intended as a compound approach to locking, therefore we consider an already obfuscated circuit in order to find HERK insertion locations (rather than the oracle circuit). Upon running this command, the user will be provided the ability to edit the number of random inputs used to identify HERK insertion locations as well as the number of HERK insertion locations to identify in the provided benchmark circuit through on screen prompts. An overview of the procedure to identify these HERK insertion locations as well as definitions of what each of these parameters mean in practice can be found in the TCAD manuscript linked above.

An example command to perform HERK insertion is as follows:

	./sld -H 1 ../../benchmarks/SFLL/b14/b14_sfllhd6_k16.bench ../../benchmarks/ORIGINAL/b14_oracle.bench b14_oracle/error_0_2/b14_sfllhd6_k16.bench.stoch

This command, by default, would identify 10 HERK insertion locations in the b14 benchark circuit obfuscated with SFLL using 100 random input patterns to identify these locations. The output of this command would appear as follows:

	HERK Insertion Locations -- Post-sort: 
	HERK 1 at output of n4086 with estimated error rate 0.242699
	HERK 2 at output of n4085 with estimated error rate 0.242699
	...

Each of these lines prints the name of a gate in the .bench file provided to the tool where a HERK should be inserted as well as the error estimated at this location using Boolean Difference Calculus. For example, HERK 1 should be inserted at the output of n4086. With this data, a HERK can be manually inserted at this location by editing the bench file to add a XOR gate driven simultaneously by a key input and the output of the listed gate. This gate is known as a HERK. The output of the HERK gate would fan-out to every gate that was previously connected to the output of the gate identified as the HERK insertion point. Once again, a more deailed overview of the HERK insertion procedure can be found in the cited TCAD manuscript above.
