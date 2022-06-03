#include <iostream>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <cstring>
#include <sys/time.h>
#include <sys/resource.h>
#include <algorithm>
#include <boost/algorithm/string/predicate.hpp>
#include "ast.h"
#include "ckt.h"
#include "dbl.h"
#include "sim.h"
#include "sld.h"
#include "util.h"
#include "delist.h"
#include "solver.h"
#include "tvsolver.h"
#include "fft_ifft.h"
#include <cudd.h>
#include <cuddObj.hh>
#include <unordered_map>
#include<queue>

int verbose = 0;
int progress = 0;
int allsat_max_iter = -1;
int backbones = 1;
int PRINT_INTERVAL= 5;
int tvs_en = 0;
int print_info = 0;
int slice = 0;
int tv_quit = 0;
int more_keys = 1;
int herk_insert = 0;

volatile solver_t* solver = NULL;
std::string known_keystring;

int sld_main(int argc, char* argv[]) 
{
    char c;
    int cpu_limit = -1;
    int64_t data_limit = -1;

    while ((c = getopt (argc, argv, "ihvptTcH:m:k:sN:")) != -1) {
        switch (c) {
            case 'h':
                return print_usage(argv[0]);
                break;
            case 's':
                slice = 1;
                break;
            case 't':
                tvs_en = 1;
                break;
            case 'T':
                tv_quit = 1;
                break;
            case 'i':
                print_info = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'c':
                cpu_limit = atoi(optarg);
                break;
            case 'm':
                data_limit = ((int64_t) atoi(optarg)) * ((int64_t)1048576);
                break;
            case 'p':
                progress = 1;
                break;
            case 'k':
                known_keystring = optarg;
                break;
            case 'N':
                more_keys = atoi(optarg);
                break;
            case 'H':
	      herk_insert = 1;
	      break;
            default:
                break;
        }
    }


    // check if we got a test article.
    if(optind >= argc-1) {
        return print_usage(argv[0]);
    }

    if(cpu_limit != -1) {
        setup_cpux_handler();

        struct rlimit rl;
        rl.rlim_cur = cpu_limit;
        setrlimit(RLIMIT_CPU, &rl);
    }

    if(data_limit != -1) {
        struct rlimit rl;
        rl.rlim_cur = rl.rlim_max = data_limit;
        if(setrlimit(RLIMIT_AS, &rl) != 0) {
            std::cerr << "trouble setting data limit." << std::endl;
        }
    }

    // try to open the verilog file.
    //
    // JOHANN
    // the encrypted benchfile
    yyin = fopen(argv[optind], "rt");
    if(yyin == NULL) {
        perror(argv[optind]);
        return 1;
    }

    if(yyparse() == 0) 
    {
        using namespace ast_n;
        ckt_n::ckt_t ckt(*statements);
        delete statements;
        fclose(yyin);

        
        ckt.read_known_key(std::string(argv[optind])+".key");
        std::cout<<"Obtained known correct key for simulating oracle: ";
        for (int i = 0; i < ckt.known_correct_key.size(); ++i)
            std::cout<<ckt.known_correct_key[i];
        std::cout<<"\n";


        // read the circuit for simulation - irrespective of using SFLL or not
        yyin = fopen(argv[optind+1], "rt");
        if(yyin == NULL) {
            perror(argv[optind+1]);
            return 1;
        }
        if(yyparse() != 0) {
            return 1;
        }
        ckt_n::ckt_t simckt(*statements);
        delete statements;
        fclose(yyin);

    	


        // also read in the .stoch file, which defines the stochastic gates and their error rate
        // stoch file is based on the locked netlist
        std::string direc_sp = std::string(argv[optind+2]);
        std::cout<<"Specific directory: "<<direc_sp<<"\n";
        std::string stoch_file = "../../benchmarks/gate_errors/probabilistic/" + direc_sp;
        ckt.readStochFile(stoch_file); 

        if(simckt.num_key_inputs() != 0) {
            std::cout << "Error. Circuit for simulation musn't have key inputs." << std::endl;
            return 1;
        }

        if(!simckt.compareIOs(ckt, 1)) {
            std::cout << "Error. Original (simulation) and encrypted designs don't match." << std::endl;
            return 1;
        }

        // test_ckt(ckt);
        if(progress) {
            setup_timer();
        }

        if(print_info) {
            std::cout << argv[optind] << " " << ckt.num_ckt_inputs() << " " << ckt.num_outputs()
                      << " " << ckt.num_gates() <<  " " << ckt.num_key_inputs() << std::endl;
        } else {
            solve(ckt, simckt);
        }
    }

    return 0;
}

void debug_g311(ckt_n::ckt_t& ckt)
{
    using namespace ckt_n;
    using namespace sat_n;
    std::cout << "Entering debug_g311() of sld.cpp" << std::endl;

    Solver S;
    index2lit_map_t lmap;
    ckt.init_solver(S, lmap);

    int out_index = -1;
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        if(ckt.outputs[i]->name == "G311") {
            out_index = i;
            break;
        }
    }
    assert(out_index != -1);
    node_t* out = ckt.nodes[out_index];
    Lit l_out = ckt.getLit(lmap, out);

    bool result = S.solve(l_out);
    assert(result);

    for(unsigned i=0; i != ckt.num_ckt_inputs(); i++) {
        Lit l_inp = ckt.getLit(lmap, ckt.ckt_inputs[i]);
        std::cout << std::setw(6) << ckt.ckt_inputs[i]->name << ":" << S.modelValue(l_inp).getBool() << " ";
        if((i+1) % 8 == 0) std::cout << std::endl;
    }
    std::cout << std::endl;
}

int tv_solve(std::map<std::string, int>& keysFound, ckt_n::ckt_t& ckt, ckt_n::ckt_t& simckt)
{
    using namespace ckt_n;
    std::cout << "Entering tv_solve() of sld.cpp" << std::endl;
    ckt_eval_t sim(simckt, simckt.ckt_inputs);
    int newlyFound = 0, iter = 0, foundCnt = 0;
    do {

        // find some new keys.
        tv_solver_t tvs(ckt, sim);
        std::map<std::string, int> newKeysFound;
        tvs.solveSingleKeys(newKeysFound);
        // count how many we found.
        newlyFound = newKeysFound.size();
        // get rid of them in the ckt.
        ckt.rewrite_keys(newKeysFound);

        // now copy them to the result map.
        for(auto it=newKeysFound.begin(); it != newKeysFound.end(); it++) {
            assert(keysFound.find(it->first) == keysFound.end());
            keysFound[it->first] = it->second;
        }
        iter += 1;
        foundCnt += newlyFound;
    } while(newlyFound > 0);
    std::cout << "tv_solve iter=" << iter << "; cnt=" << foundCnt << std::endl;
    return foundCnt;
}

int slice_solve(std::map<std::string, int>& keysFound, ckt_n::ckt_t& ckt, ckt_n::ckt_t& simckt)
{
    std::cout << "Entering slice_solve() of sld.cpp" << std::endl;
    int iter=0;
    int maxNodes = ckt.num_nodes() / 10 + 1;
    int maxKeys = -1;
    int cnt = 0;
    while(true) {
        std::cout << "slice iteration #" << iter++ << "; keysFound=" << keysFound.size() 
            << "; keysTotal=" << ckt.num_key_inputs() 
            << std::endl;

        // do the sliced solving.
        std::map<std::string, int> newKeysFound;
        solver_t S(ckt, simckt, verbose, herk_insert);
        if(S.sliceAndSolve(newKeysFound, maxKeys, maxNodes) == 0) {
            std::cout << "infeasible cplex model. " << std::endl;
            break;
        }
        cnt += newKeysFound.size();

        // copy the keys into the result map.
        for(auto it=newKeysFound.begin(); it != newKeysFound.end(); it++) {
            assert(keysFound.find(it->first) == keysFound.end());
            keysFound[it->first] = it->second;
        }

        // if necessary do the test vector solver.
        int tv_found=0;
        if(tvs_en) {
            tv_found = tv_solve(keysFound, ckt, simckt);
            cnt += tv_found;
        }
        if(newKeysFound.size() == 0 && tv_found == 0) {
            std::cout << "no new keys found." << std::endl;
            break;
        }
    }
    return cnt;
}
void solve(ckt_n::ckt_t& ckt, ckt_n::ckt_t& simckt) 
{
    using namespace ckt_n;
    using namespace AllSAT;
    std::cout << "inputs=" << ckt.num_ckt_inputs() 
        << " keys=" << ckt.num_key_inputs() 
        << " outputs=" << ckt.num_outputs()
        << " gates=" << ckt.num_gates()
        << std::endl;

    ckt.cleanup();

    // create an array of key names.
    std::vector<std::string> keyNames(ckt.num_key_inputs());
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        keyNames[i] = ckt.key_inputs[i]->name;
    }


    if(known_keystring.size()) {
        // TODO: refactor this to use maps.
        if(known_keystring.size() != ckt.num_key_inputs()) {
            std::cerr << "Error. Expected the known key string to have same size as number of key inputs." << std::endl;
            exit(1);
        }
        std::map<std::string, int> knownKeys;
        for(unsigned i=0; i != known_keystring.size(); i++) {
            if(known_keystring[i] == 'x') { continue; }
            else {
                if(known_keystring[i] == '1') {
                    knownKeys[ckt.key_inputs[i]->name] = 1;
                } else if(known_keystring[i] == '0') {
                    knownKeys[ckt.key_inputs[i]->name] = 0;
                } else {
                    std::cerr << "Error. Unexpected character: '" << known_keystring[i] << "'." << std::endl;
                    exit(1);
                }
            }
        }
        ckt.rewrite_keys(knownKeys);
    }

    simckt.split_gates();

    // Only go into StatSAT parameter loop if we are running Statsat (not inserting herks)
    int num_instances=2;
    int num_iovecs = 1;   
    if(herk_insert){

      std::cout<<"Identifying locations for High Error Rate Key (HERK) insertion within the circuit: \n";
      std::cout<<"Parameters: \n";
      std::cout<<"1) Number of HERK insertion locations to identify = "<<ckt.num_herks<<"\n";
      std::cout<<"2) Number of random inputs to use to identify HERK insertion locations = "<<ckt.trials_error<<"\n";
      std::cout<<"Enter 0 to proceed with these options. Or the option number to change them\n";
      
      int action; std::cin>>action;
      do
	{
	  switch(action)
	    {
            case 0:
	      std::cout<<"Proceeding with default\n";
	      break;
            case 1:
	      std::cout<<"Enter number of HERK insertion locations to identify: ";
	      std::cin>>ckt.num_herks;
	      break;
            case 2:
	      std::cout<<"Enter number of random inputs to use to identify HERK insertion locations: ";
	      std::cin>>ckt.trials_error;
	      break;
            default:
	      std::cout<<"Wrong option. Please try again: ";
	    }
	  if(action==0)
            break;
	  std::cout<<"Enter 0 to proceed, or option number to change: ";
	  std::cin>>action;
	}while(action>0);

      
    } else {
      if(!ckt.estimated_error)
        std::cout<<"Attacker using actual error rates. \nEnter 1 to switch to estimate error rates, ";
      else
        std::cout<<"Attacker estimates circuit error rate. \nEnter 1 to switch to using actual error rates, ";
      std::cout<<"else enter any other number: ";
      int dummy; std::cin>>dummy;
      if(dummy==1)
        ckt.estimated_error = !ckt.estimated_error;
      
      std::cout<<"Parameters: \n";
      std::cout<<"1) Number of satisfying keys to estimate output BERs (N_satis) = "<<ckt.trials_error<<"\n";
      std::cout<<"2) Threshold above which to declare uncertainty from oracle response (U_lambda) = "<<ckt.uncert_limit<<"\n";
      std::cout<<"3) Threshold above which to declare uncertainty from suspected error rate (E_lambda) = "<<ckt.ydiff_limit<<"\n";
      std::cout<<"4) Number of SAT solver instances (N_inst)= "<<num_instances<<"\n";
      std::cout<<"5) Number of distinguishing I/O vectors to print = "<<num_iovecs<<"\n";
      std::cout<<"6) Number of samples per input pattern (during and after attack, but not in final evaluation) (N_s) = "<<ckt.IO_sampling_iter<<"\n";
      std::cout<<"7) Number of input patters when verifying solution (during and after attack, but not in final evaluation) (N_eval) = "<<ckt.test_patterns<<"\n\n";

      std::cout<<"Enter 0 to proceed with these options. Or the option number to change them\n";
      
      int action; std::cin>>action;
      do
	{
	  switch(action)
	    {
            case 0:
	      std::cout<<"Proceeding with default\n";
	      break;
            case 1:
	      std::cout<<"Enter no. of satisfying keys to estimate output BERs: ";
	      std::cin>>ckt.trials_error;
	      break;
            case 2:
	      std::cout<<"Enter uncertainty threshold from oracle response: ";
	      std::cin>>ckt.uncert_limit;
	      break;
            case 3:
	      std::cout<<"Enter uncertainty threshold from suspected error rate: ";
	      std::cin>>ckt.ydiff_limit;
	      break;
            case 4:
	      std::cout<<"Enter Number of SAT solver instances: ";
	      std::cin>>num_instances;
	      break;
            case 5:
	      std::cout<<"Enter Number of I/O vectors to print: ";
	      std::cin>>num_iovecs;
	      break;
            case 6:
	      std::cout<<"Enter number of samples of output per input pattern: ";
	      std::cin>>ckt.IO_sampling_iter;
	      break;
            case 7:
	      std::cout<<"Enter number of input patterns: ";
	      std::cin>>ckt.test_patterns;
	      break;
            default:
	      std::cout<<"Wrong option. Please try again: ";
	    }
	  if(action==0)
            break;
	  std::cout<<"Enter 0 to proceed, or option number to change: ";
	  std::cin>>action;
	}while(action>0);
    }
    
    std::vector<bool> done(num_instances,false);
    std::vector<std::map<std::string, int>> keysFound(num_instances);
    std::vector<solver_t*> S(num_instances); // make space for SAT instances
    solver_t *Sptr;
    std::vector<bool> active(num_instances,false);
    std::vector<bool> sat(num_instances,false);
    std::vector<double> time_taken(num_instances,0);
    std::queue<int> available; // queue of SAT instances available for use
    for (int i = 0; i < num_instances; ++i)
    {
      Sptr = new solver_t(ckt, simckt, verbose, herk_insert);  // create multiple SAT instances
        S[i] = Sptr;
        if (i>0)
            available.push(i);
    } 
    solver = S[0]; active[0] = true;
    int iter_count = 0;

    ckt.split_gates();
    std::cout<<"Ckt info: Num gates = "<<ckt.num_gates()<<"\n";

    // If we are trying to find herk insertion locations, go ahead and create a set of random inputs and eval.
    if(herk_insert) {

      // Calculate highest error region for n random inputs
      S[0]->estimate_error_rate_herk(); // estimate error rate in oracle or unlocked circuit

      exit(0);
    }
    
    if(ckt.estimated_error)
        ckt.est_error_rate = S[0]->estimate_error_rate(); // estimate error rate in oracle or unlocked circuit
    else
    {       
        // std::cout<<"ckt->gate->error rates will be used always \n";
    }

    // ckt.dump_all_gates_info();
    // ckt.get_num_error_gates();
    
    
    // int dummy; std::cout<<"Enter any number to continue- "; std::cin>>dummy;


    int interactive;
    std::cout<<"Enter 1 for an interactive session, 0 otherwise\n";
    std::cin>>interactive;
    
    while(get_active_instances(active)>0)
    {
        iter_count++; // global iteration count, different from instances' iteration
        std::cout<<"************* Starting iteration "<<iter_count<<" ******"<<std::endl;
        bool deadlock;
        for (int i = 0; i < num_instances; ++i)
        {
            if(!active[i] || (active[i] && done[i]))
                continue;

            // find if there are inactive instances that will allow forking
            bool allow_fork; 
            if(available.size()>0)
                allow_fork = true;
            else
                allow_fork = false;

            deadlock = true;
            for (int j = 0; j < num_instances; ++j)
            {
                if(i==j) continue;

                // if any instance other than i is still active, and doesn't want to fork
                // there is no deadlock
                if(active[j] && !done[j] && !S[j]->fork) 
                {
                    deadlock = false;
                    break;
                }                    
            }

            // if there's no space for forking, but there is a deadlock,
            // then proceed nevertheless 
            bool force_proceed = allow_fork || deadlock;

            if(!done[i])
            {
                std::cout<<"\n-----Sample "<<i<<"------"<<std::endl;
                // std::cout<<"Deadlock = "<<deadlock<<" and allow_fork = "<<allow_fork<<"\n";
                if(S[i]->fork && !force_proceed)
                {
                    // instance i wanted to fork but is still not allowed to,
                    // just move on to the next instance
                    std::cout<<"Sample "<<i<<": Forking required but not allowed\n";
                    continue;
                }
                //solve again all active instances               
                done[i] = S[i]->solve(keysFound[i], allow_fork, deadlock);
            }

            if(done[i])
            {
                // if no more distinguishing inputs can be found, try to get a key
                std::cout<<"Sample "<<i<<" is done"<<std::endl;
                active[i] = false; // Deactivate instances which are done
                solver = S[i];
                time_taken[i] = dump_status(false);
                sat[i] = S[i]->verify_solution_sim(keysFound[i]);
                if (!sat[i]) // UNSAT model
                {
                    delete S[i]; // kill this instance and make space for a new one
                    available.push(i);
                    S[i] = new solver_t(ckt, simckt, verbose, herk_insert); 
                    done[i] = false;
                    std::cout<<"\n $$$$ Sample "<<i<<" is reborn $$$$"<<"\n";
                }
            }
            else
            {    
                int inact = available.size();
                std::cout<<"No. of instances unactivated = "<<(inact)<<std::endl;
                // check if a new instance has to created and if there is space for one
            
                if(inact>0 && S[i]->fork)
                {   
                    // get the next available instance
                    int to = available.front(); available.pop();
                    int from = i;
                    std::cout<<"Copying instance "<<from<<" to instance "<<to<<"\n"; 
                    // activate a new instance

                    active[to] = true;
                    //now feed previous I/O vectors to new inactive instances
                    S[to]->feed_iovectors(S[from]->iovectors,true);

                    //then extract the following - 
                    // a) the last distinguising input, b) most likely output, 
                    // c) uncertainty vector and d) the index at which we forked
                    int iovecs = S[from]->iovectors.size();
                    std::vector<bool> last_in = (S[from]->iovectors[iovecs-1]).inputs;
                    std::vector<bool> last_out = (S[from]->iovectors[iovecs-1]).outputs;
                    std::vector<bool> unc_vector = (S[from]->iovectors[iovecs-1]).uncertainty;
                    int flip_ind = S[from]->fork_index;
                    // std::cout<<"Flipping bit "<<flip_ind<<"\n";

                    // flip the index of output vector at which we forked
                    last_out[flip_ind] = !last_out[flip_ind];
                    // std::cout<<"New instance output: "<<last_out;
                    // std::cout<<" Uncertainty: "<<unc_vector;
                    S[to]->feed_iovector(last_in,last_out,unc_vector);
                    S[to]->iter = S[from]->iter; // assign same iteration number
                    S[i]->fork = false; // deactivate fork signal    
                }
                // std::cout<<"\n";

                if(deadlock)
                    S[i]->fork = false; // If there was a deadlock, instance
                // i was forced to proceed, and has hopefully obtained enough 
                // information to not produce the same distinguishing input
                // in the next iteration. This prevents the other instances from 
                // forcefully proceeding in a wrong direction
                
            }
        }

        std::cout<<"\n------- End of iteration "<<iter_count<<"-----"<<std::endl;
        if(num_iovecs>0)
        {
            for (int i = 0; i < num_instances; ++i)
            {
                if(active[i])
                {
                    std::cout<<"I/O vectors of instance "<<i<<" are:"<<std::endl;
                    S[i]->dump_iovectors(num_iovecs);
                }
            }
            std::cout<<std::endl;
        }

        if(get_active_instances(active)==0)
        {
            std::cout<<" No more active instances left"<<std::endl;
        }
        if(interactive>0)
        {
            std::cout<<"Press 0 to exit interactive mode, any other key to continue as is\n";
            std::cin>>interactive;

        }
    }

    // print all keys found 
    double least_max_ydiff = 1; int best_solver_index = -1;
    for (int i = 0; i < num_instances; ++i)
    {
        if(sat[i])
        {
            std::cout<<"Dumping key found by instance "<<i<<std::endl;
            solver = S[i];
            dump_keys(keyNames, keysFound[i]); 
            std::cout<<" ("; S[i]->bool2hex(S[i]->key_found); std::cout<<")\n";
            std::cout<<"Avg diff between oracle and unclocked circuit o/p probs (HD) = ";
            std::cout<<S[i]->final_avg_ydiff<<"\n";
            std::cout<<"Max diff between oracle and unclocked circuit o/p probs (FM) = "; // figure of merit
            std::cout<<S[i]->final_max_ydiff<<"\n";
            if(S[i]->final_max_ydiff < least_max_ydiff)
            {
                least_max_ydiff = S[i]->final_max_ydiff; best_solver_index = i;
            }
            std::cout<<"Time taken without evaluation = "<<time_taken[i]<<"\n";
            //JOHANN
            dump_status(true);
            solver = NULL;
        }
    }

    if(best_solver_index==-1)
        std::cout<<"Attack failed !! No key found\n";
    else
    {
        std::cout<<"Best key: "; 
        S[best_solver_index]->bool2hex(S[best_solver_index]->key_found); 
        std::cout<<" was found by instance "<<best_solver_index<<"\n";
        if(ckt.estimated_error)
        {
            std::cout<<"\nEvaluating actual ydiff (i.e ydiff using actual error rates)...\n";
            ckt.IO_sampling_iter = 500; // manually set sampling iterations to a large number
            ckt.test_patterns=2000;
            S[best_solver_index]->final_solution(S[best_solver_index]->key_found);
        }
    }
}

int get_active_instances(std::vector<bool> active)
{
    int num_active=0;
    for (int i = 0; i < active.size(); ++i)
        num_active += active[i]?1:0;
    return num_active;
}

void dump_keys(std::vector<std::string>& keyNames, std::map<std::string, int>& keysFound)
{
    // std::cout << "Entering dump_keys() of sld.cpp" << std::endl;
    std::cout << "key=";
    bool alert=false;
    for(unsigned i=0; i != keyNames.size(); i++) {
        auto pos = keysFound.find(keyNames[i]);
        if(pos != keysFound.end()) {
            std::cout << pos->second;
        } else {
            std::cout << 'x';
            alert=true;
        }
    }
    if(alert)
        std::cout<<" ALERT: At least 1 key bit undecided ";
    // std::cout << std::endl;
}

int print_usage(const char* progname)
{
    std::cout << "Usage: " << progname << " [options] <encrypted-bench-file> <original-bench-file>" 
              << std::endl;
    std::cout << "Options may be one of the following." << std::endl;
    std::cout << "    -h            : this message." << std::endl;
    std::cout << "    -t            : enable test vector solver." << std::endl;
    std::cout << "    -v            : verbose output." << std::endl;
    std::cout << "    -p            : report progress." << std::endl;
    std::cout << "    -c            : CPU time limit (s)." << std::endl;
    std::cout << "    -m            : mem usage limit (MB)." << std::endl;
    std::cout << "    -k <keystr>   : provide known keys." << std::endl;
    std::cout << "    -s            : enable slicing and dicing." << std::endl;
    std::cout << "    -N            : extract N keys (default=1)." << std::endl;

    return 0;
}

double dump_status(bool print_info)
{
    // std::cout << "Entering dump_status() of sld.cpp" << std::endl;
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);

    double cpu_time = 
        (double) (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) + 
        (double) (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) * 1e-6;
    double rss = ((double) ru.ru_maxrss) / 1024.0;

    int iter = 0, backbones_count = 0, cube_count = 0;
    if(solver != NULL) {
        iter = solver->iter;
        backbones_count = solver->backbones_count;
        cube_count = solver->cube_count;
    }

    if(print_info)
    {
        std::cout << "iteration=" << iter
                  << "; backbones_count=" << backbones_count
                  << "; cube_count=" << cube_count
                  << "; cpu_time=" << cpu_time
                  << "; maxrss=" << rss << std::endl<<std::endl;
              }

    return cpu_time;  
}

void alarm_handler(int signum)
{
    std::cout << "Entering alarm_handler() of sld.cpp" << std::endl;
    dump_status(true);
    alarm(PRINT_INTERVAL);
}

void setup_timer(void)
{
    std::cout << "Entering setup_timer() of sld.cpp" << std::endl;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = alarm_handler;
    if(sigaction(SIGALRM, &sa, NULL) != 0) {
        std::cerr << "Unable to set up alarm signal." << std::endl;
        return;
    }
    alarm(PRINT_INTERVAL);
}

void cpux_handler(int signum)
{
    std::cout << "Entering cpux_handler() of sld.cpp" << std::endl;
    std::cout << "timeout" << std::endl;
    dump_status(true);
    exit(1);
}

void setup_cpux_handler(void)
{
    std::cout << "Entering setup_cpux_handler() of sld.cpp" << std::endl;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = cpux_handler;
    if(sigaction(SIGXCPU, &sa, NULL) != 0) {
        std::cerr << "Unable to set up CPUX signal." << std::endl;
        return;
    }
}

void bdd_test()
{
    std::cout << "Entering bdd_test() of sld.cpp" << std::endl;
    Cudd mgr;
    BDD x1 = mgr.bddVar(0), x2 = mgr.bddVar(1), k1 = mgr.bddVar(2), k2 = mgr.bddVar(3);
    BDD fn = !((x1 ^ k1) + (!(x2 ^ k2)));

    BDD c0 = !k1 & !k2;
    BDD c1 = !k1 & k2;
    BDD c2 = k1 & !k2;
    BDD c3 = k1 & k2;

    BDD fn0 = fn.Cofactor(c0);
    BDD fn1 = fn.Cofactor(c1);
    BDD fn2 = fn.Cofactor(c2);
    BDD fn3 = fn.Cofactor(c3);

    printBDD(stdout, fn);
    printBDD(stdout, fn0);
    printBDD(stdout, fn1);
    printBDD(stdout, fn2);
    printBDD(stdout, fn3);
}

void printBDD(FILE* out, BDD bdd)
{
    std::cout << "Entering printBDD() of sld.cpp" << std::endl;
    DdNode* node = bdd.getNode();
    Cudd_DumpFactoredForm(bdd.manager(), 1, &node, NULL, NULL, out); 
    fprintf(out, "\n");
}

double utimediff(struct rusage* end, struct rusage* start)
{
    // std::cout << "Entering utimediff() of sld.cpp" << std::endl;
    double tend = end->ru_utime.tv_usec*1e-6 + end->ru_utime.tv_sec;
    double tstart = start->ru_utime.tv_usec*1e-6 + start->ru_utime.tv_sec;
    return tend-tstart;
}

