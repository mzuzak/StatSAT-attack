#include "sim.h"
#include "util.h"
#include <unordered_map>
#include <map>

// JOHANN
#include <chrono>

namespace ckt_n {

    eval_t::eval_t(ckt_t& c) 
        : ckt(c)
    {
    	// std::cout<<"Location 9"<<std::endl;
        ckt.init_solver(S, mappings);
        // std::cout<<"Location 10"<<std::endl;

	// JOHANN
	//
	for (auto* gate : ckt.gates) {

		// sanity check for inputs; boolean evaluation in eval() implemented only for 2 inputs
		assert(gate->inputs.size() <= 2);

		// assign more efficient unsigned enum to gates; avoids repetitive calls to str::compare in eval()
		if (gate->func == "and") {
			gate->function = fct::AND;
		}
		else if (gate->func == "nand") {
			gate->function = fct::NAND;
		}
		else if (gate->func == "or") {
			gate->function = fct::OR;
		}
		else if (gate->func == "nor") {
			gate->function = fct::NOR;
		}
		else if (gate->func == "xor") {
			gate->function = fct::XOR;
		}
		else if (gate->func == "xnor") {
			gate->function = fct::XNOR;
		}
		else if (gate->func == "not") {
			gate->function = fct::INV;
		}
		else if (gate->func == "buf") {
			gate->function = fct::BUF;
		}
		else {
			gate->function = fct::UNDEF;
		}
	}

	// init srand with long and high-resolution timing seed
	//
	auto now = std::chrono::high_resolution_clock::now();
	auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	srand(nanos);
    }

    void eval_t::set_cnst(node_t* n, int val)
    {
        using namespace sat_n;

        assert(n->is_keyinput());

        assert(val == 0 || val == 1);

        int idx = n->get_index();
        assert(idx < (int) mappings.size());

        Lit v = (val == 0) ? ~mappings[idx] : mappings[idx];
        S.addClause(v);
    }

    void eval_t::eval(nodelist_t& input_nodes, const bool_vec_t& input_values, bool_vec_t& outputs, bool error)
    {
    	// Ankit - bool error says whether the error in the circuit is to be considered

	// JOHANN
	//
    //std::cout << "Entering eval()A of sim.cpp" << std::endl;
	if (ckt_n::DBG_VERBOSE) {
		std::cout << "Inputs: " << input_values << std::endl;
	}

	// assign input values to input nodes
	//
	for (std::size_t i = 0; i < ckt.ckt_inputs.size(); i++) {
		ckt.ckt_inputs[i]->output_bit = input_values[i];
	}

	// now, evaluate all gates' outputs, by traversing the sorted netlist graph
	//
	for (auto* gate : ckt.gates_sorted) {

		fct function;
		std::string function_name;

		// for regular (not polymorphic) gates, the function is fixed (but the function can stochastically fail, see below)
		if (gate->polymorphic_fcts.empty()) {
			function = gate->function;
			function_name = gate->func;
		}

		// for polymorphic gates, the function is flexible; here for simplicity we select randomly from one of the available functions, but according to the probability of that function
		else {
			function = gate->function;
			function_name = gate->func;

			// random double between 0.0 and 100.0, excluding 100.0 itself
			double r = (100.0 * (static_cast<double>(rand()) / RAND_MAX));

			// sum up the probabilities of all polymorphic functions; map the random value into the resulting 0--100% range and pick the one function which is in the
			// respective part of the range
			double range = 0.0;
			for (poly_fct const& fct : gate->polymorphic_fcts) {

				range += fct.probability;

				if (r < range) {
					function = fct.function;
					function_name = fct.name;
					break;
				}
			}

			// sanity check: for probabilistic functions with two inputs (any other than INV/BUF), make sure that the actual underlying gate supports this, i.e., is not
			// an INV or BUF
			//
			if (!(function == fct::INV || function == fct::BUF)) {
				if (gate->inputs.size() == 1) {
					std::cout << "ERROR: gate " << gate->name << " cannot support the probabilistic function ";
					std::cout << function_name << " as the underlying gate was original a \"" << gate->func << "\" gate" << std::endl;
					exit(1);
				}
			}
		}

		if (function == fct::AND) {
			gate->output_bit = gate->inputs[0]->output_bit && gate->inputs[1]->output_bit;
		}
		else if (function == fct::NAND) {
			gate->output_bit = !(gate->inputs[0]->output_bit && gate->inputs[1]->output_bit);
		}
		else if (function == fct::OR) {
			gate->output_bit = gate->inputs[0]->output_bit || gate->inputs[1]->output_bit;
		}
		else if (function == fct::NOR) {
			gate->output_bit = !(gate->inputs[0]->output_bit || gate->inputs[1]->output_bit);
		}
		else if (function == fct::XOR) {
			gate->output_bit = gate->inputs[0]->output_bit ^ gate->inputs[1]->output_bit;
		}
		else if (function == fct::XNOR) {
			gate->output_bit = !(gate->inputs[0]->output_bit ^ gate->inputs[1]->output_bit);
		}
		else if (function == fct::INV) {
			gate->output_bit = !gate->inputs[0]->output_bit;
		}
		else if (function == fct::BUF) {
			gate->output_bit = gate->inputs[0]->output_bit;
		}
		else {
			std::cout << "ERROR: unsupported function for gate " << gate->name << ": \"" << function_name << "\"" << std::endl;
			exit(1);
		}

		// post-process stochastic gates: they may experience flipping of their outputs depending on their error rate
		//
		if (error && gate->error_rate > 0.0) {

			if (
					// random double between 0.0 and 100.0, excluding 100.0 itself
					(100.0 * (static_cast<double>(rand()) / RAND_MAX))
					// chances that the random value falls below error_rate are approx error_rate %
					< gate->error_rate
			   ) {
				gate->output_bit = !gate->output_bit;
			}
		}

		if (ckt_n::DBG_VERBOSE) {
			std::cout << "Gate " << gate->name << std::endl;
			std::cout << " function: " << function_name << std::endl;
			std::cout << " inputs[0]: " << gate->inputs[0]->output_bit << std::endl;
			if (gate->inputs.size() > 1) {
				std::cout << " inputs[1]: " << gate->inputs[1]->output_bit << std::endl;
			}
			std::cout << " output_bit: " << gate->output_bit << std::endl;
		}
	}

	// memorize output values in vector
        outputs.resize(ckt.num_outputs());
	for (std::size_t i = 0; i < ckt.outputs.size(); i++) {
		outputs[i] = ckt.outputs[i]->output_bit;
	}

	if (ckt_n::DBG_VERBOSE) {
		std::cout << "Outputs (graph evaluation): " << outputs << std::endl;
	}

	// original function; run for DBG mode only
	//
	if (ckt_n::DBG_SAT) {

	bool_vec_t outputs_SAT;

        using namespace sat_n;

        assert(input_nodes.size() == input_values.size());

        vec_lit_t assump;
        for(unsigned i=0; i != input_values.size(); i++) {
            Lit l = input_values[i] ? 
                mappings[input_nodes[i]->get_index()] : 
                ~mappings[input_nodes[i]->get_index()];
            assump.push(l);
        }

        bool result = S.solve(assump);
        if(!result) {
		if (ckt_n::DBG_VERBOSE) {
		    std::cout << "inputs: ";
		}
            for(unsigned i=0; i != input_nodes.size(); i++) {
                std::cout << input_nodes[i]->name << " ";
            }
		if (ckt_n::DBG_VERBOSE) {
		    std::cout << std::endl;
		}
        }
        assert(true == result);

        outputs_SAT.resize(ckt.num_outputs());
        for(unsigned i=0; i != outputs_SAT.size(); i++) {
            int idx = ckt.outputs[i]->get_index();
            lbool vi = S.modelValue(var(mappings[idx]));
            assert(vi.isDef());
            outputs_SAT[i] = (vi.getBool());
        }

	if (ckt_n::DBG_VERBOSE) {
		std::cout << "Outputs (SAT evaluation): " << outputs_SAT << std::endl;
	}
	assert(outputs == outputs_SAT);
	}
    }

    std::vector<double> ckt_eval_t::eval(
        const std::vector<bool>& input_values,
        std::vector<bool>& output_values,
        bool sampling_flag,
        bool print_info
    )
    {
	    // JOHANN
	    //
	    // sample the output for the input several times, and pick only the most common observation as ground truth to be used for further SAT solving
	    //
	    // std::cout << "Entering eval()B of sim.cpp" << std::endl;
	    std::vector<double> mean_output_values;
	    if (sampling_flag)
	    {
		    // sample outputs N times (for the same input), track the counts of the different observed output patterns, select the most promising one as ground truth for
		    // this input pattern
		    //
		    //std::cout << " sim ckt IO sampling iter = "<<sim.ckt.IO_sampling_iter<<std::endl; // ANkit
		    
		    for (int j=0;j<output_values.size();j++)
		    	mean_output_values.push_back(0);

	    	// std::cout<<std::endl;
		    for (unsigned i = 0; i < sim.ckt.IO_sampling_iter; i++) 
		    {
			    sim.eval(inputs, input_values, output_values,true); 
			    // "true" because we are considering error in the circuit
			    for (int j=0;j<output_values.size();j++)
			    	mean_output_values[j] = mean_output_values[j] + output_values[j];
		    }

		    //set output_values to be the most likely output
		    for (int j=0;j<mean_output_values.size();j++)
		    {
		    	mean_output_values[j] /= sim.ckt.IO_sampling_iter ;
	    		output_values[j] = (int)std::round(mean_output_values[j]);
		    }

		    if(print_info)
		    {
			    std::cout<<"Mean output values =            ";
			    for (int j=0;j<mean_output_values.size();j++)
			    	std::cout<<mean_output_values[j]<<", ";
			    std::cout<<"\n";
			    std::cout<<"Seemingly most likely output =  ";
			    for (int j=0;j<output_values.size();j++)
			    	std::cout<<output_values[j]<<", ";
			    std::cout<<std::endl;
			}

		    std::vector<bool> output_no_error(output_values.size()); // if circuit error is not considered
		    sim.eval(inputs,input_values,output_no_error,false);
		    if(print_info)
		    {
			    std::cout<<"W/o considering error, output = ";
			    for (int j=0;j<output_no_error.size();j++)
			    	std::cout<<output_no_error[j]<<", ";
			    std::cout<<std::endl;
			}

		}
	    // JOHANN
	    // original code, calls evaluation only once
	    //
	    else {
		    sim.eval(inputs, input_values, output_values,false); // false because we would not consider error if sampling once
		    for (int j=0;j<output_values.size();j++)
		    	mean_output_values.push_back(output_values[j]);
	    }
	    return mean_output_values;
    }

    // to check the operation of circuit c3540
    void ckt_eval_t::check_c3540()
    {
	    // checking c3540
	    // A[7:0] - 50, 58, 68, 77, 87, 97, 107, 116
	    // B[7:0] - 226, 232, 238, 244, 250, 257, 264, 270
	    // Q[7:0] - 124, 125, 128, 132, 137, 143, 150, 159
	    // R[7:0] - 283, 294, 303, 311, 317, 322, 326, 329
	    // T[1:0] - 222, 223
	    // Cin - 330
	    // K - 274
	    // Cont[13:0] - 2897, 200, 190, 179, 343, 213, 169, 45, 41, 1698, 33, 20, 13, 1

	    // Z[7:0] - 375, 378, 381, 384, 387, 390, 393, 396
	    // OddParZ, OddParZ_Cont - 402, 405
	    // OddParA - 351
	    // OddParB - 358
	    // NotZeroZ, ZeroZ_Cont - 407, 409
	    // XCarry2, Cout_in0 - 399, 369
	    // PropThru - 372
	    // MiscOuts[4:0] - 353, 355, 361, 364, 367

	    std::vector<bool> A(8), B(8), Z(8); 
	    // A[7] is the MSB of A and so on
	    bool Cin=true;
	    A[7] = true; A[5] = true; A[4] = true; A[0] = true; 
	    B[7] = true; B[5] = true; B[2] = true;
	    

	    std::cout<<"Circuit inputs: ";
	    for(auto inp: sim.ckt.ckt_inputs)
	        std::cout<<inp->name<<", ";
	    std::cout<<"\n";

	    std::cout<<"Circuit outputs: ";
	    for(auto out: sim.ckt.outputs)
	        std::cout<<out->name<<", ";
	    std::cout<<"\n";

	    bool Cout = add_c3540(A,B,Cin,Z,false);
	    
	}


	// Ankit - x[0] is the LSB. Function prints MSB to LSB
	void ckt_eval_t::print_bool_vec(std::vector<bool> x)
	{
	    for(int i=0;i<x.size();i++)
	        std::cout<<x[x.size()-1-i];
	}

	// add 2 8-bit numbers using c3540
	bool ckt_eval_t::add_c3540(std::vector<bool> A, std::vector<bool> B, bool Cin, std::vector<bool>& Z, bool error)
	{
		// Add A and B with Cin as carry, output is Z, error says if we should evaluate
		// considering the error in the circuit or not
		// A[7] is the MSB of A and so on

		// create the other input data
	    std::vector<bool> Q(8), R(8), T(2), Cont(14);
	    bool K=false; 
	    Cont[7] = true; Cont[12] = true; // for addition C7 = C12 = 1

	    // std::cout<<"A = ";print_bool_vec(A); std::cout<<" B = "; print_bool_vec(B);
	    // std::cout<<" Q = "; print_bool_vec(Q); std::cout<<" R = "; print_bool_vec(R);
	    // std::cout<<"\nT  = "; print_bool_vec(T); std::cout<<" Cont = "; print_bool_vec(Cont); 
	    // std::cout<<" K = "<<K;
	    // std::cout<<" Cin = "<<Cin;
	    // std::cout<<"\n";

	    std::vector<bool> input_vec;
	    for(int i=0;i<4;i++)
	        input_vec.push_back(Cont[i]); // Cont 0-3: 1,13,20,33
	    input_vec.push_back(Cont[5]); input_vec.push_back(Cont[6]); //41, 45
	    for(int i=0;i<8;i++)
	        input_vec.push_back(A[7-i]); // 50, 58, 68, 77, 87, 97, 107, 116
	    for(int i=0;i<8;i++)
	        input_vec.push_back(Q[7-i]); // 124, 125, 128, 132, 137, 143, 150, 159
	    input_vec.push_back(Cont[7]); // 169
	    for(int i=10;i<13;i++)
	        input_vec.push_back(Cont[i]); // COnt 10-12: 179, 190, 200
	    input_vec.push_back(Cont[8]); // 213
	    for(int i=0;i<2;i++)
	        input_vec.push_back(T[2-i]); // 222, 223
	    for(int i=0;i<8;i++)
	        input_vec.push_back(B[7-i]); // 226, 232, 238, 244, 250, 257, 264, 270
	    input_vec.push_back(K); // 274
	    for(int i=0;i<8;i++)
	        input_vec.push_back(R[8-i]); // 283, 294, 303, 311, 317, 322, 326, 329
	    input_vec.push_back(Cin); // 330
	    input_vec.push_back(Cont[9]); // 343
	    input_vec.push_back(Cont[4]); // 1698
	    input_vec.push_back(Cont[13]); // 2897

	    // std::cout<<"Input_vec = "; print_bool_vec(input_vec); std::cout<<"\n";

	    unsigned samp_iters = sim.ckt.IO_sampling_iter;
	    sim.ckt.IO_sampling_iter = 1;

	    std::vector<bool> output_vec(sim.ckt.num_outputs());
	    eval(input_vec, output_vec, error, false);

	    // int dummy; std::cout<<"Enter any number "; std::cin>>dummy;

	    // std::vector<bool> Z(8); // Z[7] is the MSB
	    Z[0] = output_vec[9]; //396
	    Z[1] = output_vec[13]; //393
	    Z[2] = output_vec[14]; //390
	    Z[3] = output_vec[12]; //387
	    Z[4] = output_vec[10]; //384
	    Z[5] = output_vec[17]; //381
	    Z[6] = output_vec[15]; //378
	    Z[7] = output_vec[16]; //375
	    bool XCarry2 = output_vec[7]; // 399
	    bool Cout_in0 = output_vec[6]; // 369

	    // std::cout<<"Z = ";print_bool_vec(Z);
	    // std::cout<<" XCarry2 = "<<XCarry2
	    // std::cout<<" Cout_in0 = "<<Cout_in0<<"\n";

	    sim.ckt.IO_sampling_iter = samp_iters; // restore the value
	    return Cout_in0;
	}


    void convert(uint64_t v, bool_vec_t& result)
    {
        for(unsigned i = 0; i < result.size(); i++) {
            uint64_t mask = (uint64_t)((uint64_t)1 << i);
            if((v & mask) != 0) {
                result[i] = true;
            } else {
                result[i] = false;
            }
        }
    }

    uint64_t convert(const bool_vec_t& v) {
        uint64_t r = 0;
        for(unsigned i=0; i < v.size(); i++) {
            uint64_t mask = (uint64_t)((uint64_t)1 << i);
            if(v[i]) r |= mask;
        }
        return r;
    }

    void dump_mappings(std::ostream& out, ckt_t& ckt, index2lit_map_t& mappings)
    {
        for(unsigned i=0; i != ckt.num_inputs(); i++) {
            std::cout << "input " << ckt.inputs[i]->name << " --> " 
                      << mappings[ckt.inputs[i]->get_index()] << std::endl;
        }
        for(unsigned i=0; i != ckt.num_outputs(); i++) {
            std::cout << "output " << ckt.outputs[i]->name << " --> " 
                      << mappings[ckt.outputs[i]->get_index()] << std::endl;
        }
        for(unsigned i=0; i != ckt.num_gates(); i++) {
            std::cout << "gate " << ckt.gates[i]->name << " --> " 
                      << mappings[ckt.gates[i]->get_index()] << std::endl;
        }
    }
}
