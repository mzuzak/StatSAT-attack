import time
import sys
import os
import re


def parse(directory,input_file, gates_count, error_rate):
    with open(directory) as f:
        lines = f.readlines()
    inputs = list()
    outputs = list()
    input_pattern = re.compile('INPUT\s*\((\w+)\s*\)')
    op_pattern = re.compile('OUTPUT\s*\((\w+)\s*\)') # OUTPUT(po2)
    gates = list();
    for line in lines:
        grp1 = input_pattern.search(line)
        grp2 = op_pattern.search(line)
        if grp1:
            inputs.append(grp1.group(1))
        elif grp2:
            outputs.append(grp2.group(1))
        elif outputs:
            gate = line.split('=')[0].strip()
            if gate not in inputs: # commenting this - and gate not in outputs
                gates.append(gate)

    # print gates
    for i in range(len(gates)):
        if gates[i] == '':
            del gates[i]

    # print gates
    # gates.pop(0)            
    #print '--' * 100
    print 'Inputs = {}, Outputs = {}'.format(len(inputs),len(outputs))
    print 'Total number of gates {}'.format(len(gates))
    # print gates
    #print '--' * 100
    import random

    # print gates_count

    if(gates_count<=1.0):
        gates_count = int(round(gates_count*len(gates))) # Converting a specified fraction to a number of gates
    else:
        gates_count = int(round(gates_count))

    print('No. of probabilistic gates = ',gates_count)

    text = """# OUTPUT_SAMPLING_ON OUTPUT_SAMPLING_ITERATIONS OUTPUT_SAMPLING_FOR_TEST_ON TEST_PATTERNS\ntrue 500 true 2000\n# GATE_NAME ERROR_RATE (%)\n"""
    next_gate = "NEXT_GATE\n"
    output_file = input_file + '.stoch'
    f = open(output_file, 'w')
    f.write(text)
    for i in range(gates_count):
        f.write(next_gate)
        random_gate = random.choice(gates)
        gates.remove(random_gate)
        f.write('{} {}\n'.format(random_gate, error_rate))
    f.close()


def main():
    start_time = time.time()
    if len(sys.argv) < 4:
        print "Usage python script.py 'input-file-name' 'gates_count' 'error_rate' ['specific_directory']"
        exit(0)

    if len(sys.argv)==5:
        spec_dir = sys.argv[4]
    else:
        spec_dir = ""  

    print 'Specific dir = ',spec_dir  

    input_file = sys.argv[1]
    # directory = "../../../ORIGINAL/" + input_file
    # directory = "../../../SFLL/" + spec_dir + input_file # Use in case of SFLL to have stochastic gates in the locked circuit
    # directory = "../../../toc13xor/" + input_file # Use in case of non sfll to have stochastic gates in the locked circuit
    directory = "../../../dac12/" + input_file # Use in case of non sfll to have stochastic gates in the locked circuit
    # directory = "../../../StatSAT/probabilistic/c17/" + input_file # Miscellaneous

    if not os.path.exists(directory):
        print 'Incorrect path specified. Please check!'
        exit(0)
    gates_count = float(sys.argv[2])
    error_rate = float(sys.argv[3])
    # read_data_and_sort(input_file, gates_count)
    parse(directory,input_file, gates_count, error_rate)
    print "Total Time taken", time.time() - start_time
    return 0


if __name__ == "__main__":
    sys.exit(main())
