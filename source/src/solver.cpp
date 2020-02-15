#include "solver.h"
#include "util.h"
#include "sim.h"
#include "sld.h"
#include "fft_ifft.h"

//JOHANN
#include <unordered_set>

//Ankit
#include <unordered_map>
#include <map>
#include <iterator>
#include <algorithm>
#include <boost/lexical_cast.hpp>

#include <ilcplex/ilocplex.h>
ILOSTLBEGIN

solver_t::solver_t(ckt_n::ckt_t& c, ckt_n::ckt_t& s, int verb)
    : ckt(c)
    , simckt(s)
    , sim(s, s.ckt_inputs)
    , dbl(c, ckt_n::dup_allkeys, true)
    , input_values(ckt.num_ckt_inputs(), false)
    , output_values(ckt.num_outputs(), false)
    , uncert_values(ckt.num_outputs(),0)
    , key_found(ckt.num_key_inputs(), false)
    , fixed_keys(ckt.num_key_inputs(), false)
    , verbose(verb)
    , iter(0)
    , backbones_count(0)
    , cube_count(0)
    , fork(false)
    , fork_index(-1)
{
    MAX_VERIF_ITER = 1;
    time_limit = 1e100;

    using namespace ckt_n;
    using namespace sat_n;
    using namespace AllSAT;

    assert(dbl.dbl->num_outputs() == 1);
    assert(dbl.ckt.num_ckt_inputs() == ckt.num_ckt_inputs());

    // std::cout<<"Location 1"<<std::endl;
    dbl.dbl->init_solver(S, cl, lmap, true);
    // std::cout<<"Location 2"<<std::endl;
    node_t* out = dbl.dbl->outputs[0];
    l_out = lmap[out->get_index()];
    cl.verbose = verbose;

    if(verbose) {
        std::cout << dbl.ckt << std::endl;
        std::cout << *dbl.dbl << std::endl;
        std::cout << "DBL MAPPINGS" << std::endl;
        dump_mappings(std::cout, *dbl.dbl, lmap);
    }

    // setup arrays of literals.
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        int idx = ckt.key_inputs[i]->get_index();
        keyinput_literals_A.push_back(lmap[dbl.pair_map[idx].first->get_index()]);
        keyinput_literals_B.push_back(lmap[dbl.pair_map[idx].second->get_index()]);
    }
    for(unsigned i=0; i != ckt.num_ckt_inputs(); i++) {
        int idx = ckt.ckt_inputs[i]->get_index();
        cktinput_literals.push_back(lmap[dbl.pair_map[idx].first->get_index()]);
    }
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        int idx = ckt.outputs[i]->get_index();
        node_t* outA = dbl.pair_map[idx].first;
        node_t* outB = dbl.pair_map[idx].second;
        Lit lA = lmap[outA->get_index()];
        Lit lB = lmap[outB->get_index()];
        output_literals_A.push_back(lA);
        output_literals_B.push_back(lB);
    }

    S.freeze(keyinput_literals_A);
    S.freeze(keyinput_literals_B);
    S.freeze(cktinput_literals);
    S.freeze(output_literals_A);
    S.freeze(output_literals_B);
    S.freeze(l_out);

    dbl_keyinput_flags.resize(S.nVars(), false);
    dbl.dbl->init_keyinput_map(lmap, dbl_keyinput_flags);

    // JOHANN
    //
	// init srand with long and high-resolution timing seed
	//
	auto now = std::chrono::high_resolution_clock::now();
	auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
	srand(nanos);
}

// void solver_t::check_simckt()
// {
//     std::cout<<"Entering check_simckt()\n";
//     //sim.check_c3540();
//     fft_and_ifft* obj = new fft_and_ifft(sim);
//     obj->main_fft_ifft();
//     std::cout<<"Back from main_fft_ifft()\n";
//     delete obj;
// }


void solver_t::addKnownKeys(std::vector<std::pair<int, int> >& values)
{
    // std::cout << "Entering addKnownKeys() of solver.cpp" << std::endl;
    for(unsigned i=0; i != values.size(); i++) {
        using namespace sat_n;
        Lit lA = keyinput_literals_A[values[i].first];
        Lit lB = keyinput_literals_B[values[i].first];
        assert(values[i].second == 0 || values[i].second == 1);
        if(values[i].second) {
            S.addClause(lA);
            S.addClause(lB);
        } else {
            S.addClause(~lA);
            S.addClause(~lB);
        }
    }
}

solver_t::~solver_t()
{
}

void solver_t::_record_sim(
    const std::vector<bool>& input_values, 
    const std::vector<bool>& output_values, 
    std::vector<sat_n::lbool>& values,
    std::vector<bool>& uncert_vec
)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;
    // std::cout << "Entering _record_sim() of solver.cpp" << std::endl;
    // std::cout<<"Values is of size "<<values.size()<<std::endl;
    iovectors.push_back(iovalue_t());
    int last = iovectors.size()-1;
    iovectors[last].inputs = input_values;
    iovectors[last].outputs = output_values;
    iovectors[last].uncertainty = uncert_vec;

    // extract inputs and put them in the array.
    // std::cout<<"Input indices: ";
    for(unsigned i=0; i != input_values.size(); i++) {
        lbool val = input_values[i] ? l_True : l_False;
        int jdx  = dbl.dbl->ckt_inputs[i]->get_index();
        // std::cout<<jdx<<"/";
        assert(var(lmap[jdx]) < values.size());
        assert(var(lmap[jdx]) >= 0);
        values[var(lmap[jdx])] = val;
        // std::cout<<var(lmap[jdx])<<" ";
    }
    // std::cout<<"\n";

    // std::cout<<"Key indices: ";
    for(unsigned i=0; i != ckt.num_key_inputs(); ++i)
    {
        int idx = ckt.key_inputs[i]->get_index();
        int idxA = dbl.pair_map[idx].first->get_index();
        int idxB = dbl.pair_map[idx].second->get_index();
        // std::cout<<"("<<idx<<","<<idxA<<","<<idxB<<"), ";
        Var vA = var(lmap[idxA]);
        Var vB = var(lmap[idxB]);
    }
    // std::cout<<"\n";

    // and then the outputs.
    // std::cout<<"Output indices: ";
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        node_t* n_i = ckt.outputs[i];
        int idx = n_i->get_index();
        int idxA = dbl.pair_map[idx].first->get_index();
        int idxB = dbl.pair_map[idx].second->get_index();
        // std::cout<<"("<<idx<<","<<idxA<<","<<idxB<<"), ";
        Var vA = var(lmap[idxA]);
        Var vB = var(lmap[idxB]);
        // std::cout<<vA<<"-"<<vB<<"; ";
        assert(vA < values.size() && vA >= 0);
        assert(vB < values.size() && vB >= 0);
        if(!uncert_vec[i])
        {
            // std::cout<<"Output "<<i<<" certain\n";
            if(output_values[i] == true)
                values[vA] = values[vB] = sat_n::l_True;
            else
                values[vA] = values[vB] = sat_n::l_False;
        }
        else
        {
            // std::cout<<"Output "<<i<<" NOT certain\n";
            values[vA] = values[vB] = sat_n::l_Undef;
        }
    }
    // std::cout<<"\n";
}

// Ankit - Get l1-norm of the difference between 2 vectors
double solver_t::vec_dbl_diff_avg(const std::vector<double> x, const std::vector<double> y)
{
    assert(x.size()==y.size());
    double diff=0;
    for(int i=0; i<x.size(); ++i)
        diff += std::abs(x[i]-y[i]);
    diff /= x.size();
    return diff;
}

// Evaluates the output for the values stored in input_values and then records
// this in the solver.
void solver_t::_record_input_values(std::vector<double> ydiff_avg, bool allow_fork, bool deadlock)
{
    // argument ydiff_avg is the average (over multiple keys)
    // difference between the response of the locked cirucit
    // with and without considering errors
    using namespace ckt_n;
    //std::cout << "Entering _record_input_values() of solver.cpp" << std::endl;
    std::vector<sat_n::lbool> values(S.nVars(), sat_n::l_Undef);
    std::vector<double> mean_output_values(output_values.size());
    std::vector<bool> uncert(output_values.size(),false);
    bool has_uncert=false;

    // Obtain oracle response by sampling from the locked circuit fitted with the correct key
    mean_output_values = ckt.evaluate_sim(input_values,ckt.known_correct_key,true,false);

    // As in sim.eval(), output_values will store the most likely binary output
    for (int i = 0; i < output_values.size(); ++i)
        output_values[i] = (int)std::round(mean_output_values[i]);

    // Obtain output from oracle if there was no error
    std::vector<double> output_no_error = ckt.evaluate_sim(input_values,ckt.known_correct_key,false,false);
    // std::cout<<"Mean output values = "; dump_vec_dbl(mean_output_values); std::cout<<"\n";
    // std::cout<<"Most likely output = \n"; std::cout<<output_values; std::cout<<"\n";

    std::vector<bool> output_no_error_bool(output_values.size());
    for (int i = 0; i < output_values.size(); ++i)
        output_no_error_bool[i] = (bool)((int)output_no_error[i]);
    // std::cout<<"W/o error, output =  \n"<<output_no_error_bool<<"\n";

    for (int j=0;j<mean_output_values.size();j++)
    {
        //initialize uncertainty-related data 
        uncert_values[j] = std::min(mean_output_values[j],(1-mean_output_values[j]));
        // Assign uncertainty flag as per rule first
        if(uncert_values[j]>ckt.uncert_limit || ydiff_avg[j]>ckt.ydiff_limit)
            uncert[j] = true; 
    }
    // std::cout<<"Uncertainty values: "; dump_vec_dbl(uncert_values); std::cout<<"\n";
    // std::cout<<"Diff between uncertainty values and ydiff satisf = ";
    // std::cout<<vec_dbl_diff_avg(uncert_values,ydiff_avg)<<"\n";
    
    if(fork)
    {
        // When the DI repeats, the fork signal in on. 
        for(int j=0;j<output_values.size();j++)
        {
            if(!iovectors[iovectors.size()-1].uncertainty[j])
                uncert[j] = false; // a bit which was not uncertain previously should 
                                       // not be uncertain now
        }

        for(int j=0;j<output_values.size();j++)
        {
            if(uncert[j])
            {
                has_uncert = true; // has_uncert ensures that we don't fork if there
                    // is no uncertainty when the DI appears for the 2nd time
                break;
            }
        }

        if(!has_uncert)
            fork=false; // clear fork signal if uncertainty has been resolved at the repetition of DI
    }

    
    if(fork)
    {
        // will have to clear uncertainty flag at one position
        // and fork based on that

        if(allow_fork)
        {
            // if there is space for forking, fork at bit which has
            // the highest uncertainty or highest avg. error rate
            double max_unc_value = 0; int max_unc_index = -1;
            double max_ydiff_value = 0; int max_ydiff_index = -1;
            for (int j=0;j<mean_output_values.size();j++)
            {    
                if(uncert_values[j]>max_unc_value)
                {
                    max_unc_value = uncert_values[j]; max_unc_index = j;
                }
                if(ydiff_avg[j]>max_ydiff_value) //look for the output index with the highest ydiff
                {
                    max_ydiff_index = j; max_ydiff_value = ydiff_avg[j];
                }
            }
            std::cout<<"Forking allowed. Max uncertainty value = "<<uncert_values[max_unc_index]<<"\n";
        
            // if max uncertainty value is less than limit, then uncertainty may be
            // because of high values of ydiff
            if(max_unc_value<=ckt.uncert_limit)
                fork_index = max_ydiff_index; 
            else
                fork_index = max_unc_index;
        }
        else if(deadlock)
        {

            // if there is a deadlock, we are proceeding forcefully. Then
            // fork at that uncertain bit which has the smallest value of ydiff
            double min_ydiff_value=1;
            for(int j=0;j<output_values.size();j++)
            {     
                if(uncert[j] && ydiff_avg[j]<min_ydiff_value)
                {
                    fork_index = j; min_ydiff_value = ydiff_avg[j];
                }
            }
            std::cout<<"Deadlock!! Min uncertain ydiff value = "<<ydiff_avg[fork_index]<<"\n";
        } 
        // if neither allow_fork nor deadlock, then fork must have been false   

        std::cout<<"Will have to fork at index: "<<fork_index<<"\n";
        uncert[fork_index] = false;

        if(deadlock && !allow_fork)
        {
            // if the output vector along with the uncertainty vector is same as 
            // that of the previous iteration, further uncertainty needs to be cleared
            bool identical = is_identical(uncert);

            if(identical)
                clear_unc_flag(uncert,ydiff_avg);
        }
    }
    // std::cout<<"Final uncertainty vector: "<<uncert<<std::endl;    

    //for "this" solver itself, record the most likely output as the correct output 
    _record_sim(input_values, output_values, values, uncert);
    dump_iovector(input_values,output_values,uncert);
    int cnt = cl.addRewrittenClauses(values, dbl_keyinput_flags, S);
    __sync_fetch_and_add(&cube_count, cnt);
}

// Ankit - checks if the uncertainty vector in an iteration is 
// same (or less informative) as that of the previous iteration
bool solver_t::is_identical(const std::vector<bool> uncert)
{
    bool identical = true;
    for(int j=0;j<output_values.size();j++)
    { 
        if(!uncert[j] && iovectors[iovectors.size()-1].uncertainty[j])
        {
            identical = false;
            break;
        }
    }
    return identical;
}

// Ankit - simply clear the uncertainty flag at that uncertain position
// which was uncertain even in the previous iteration and has the smallest 
// error rate on an average
void solver_t::clear_unc_flag(std::vector<bool>& uncert, std::vector<double> ydiff)
{
    std::cout<<"Identical output+uncertainty vector - ";
    double min_ydiff_value=1.01; int min_ydiff_index = -1;
    for(int j=0;j<output_values.size();j++)
    {
        if(uncert[j] && !iovectors[iovectors.size()-1].uncertainty[j])
            uncert[j] = false; // a bit which was not uncertain previously should 
                               // not be uncertain now
        if(uncert[j] && iovectors[iovectors.size()-1].uncertainty[j] && ydiff[j]<min_ydiff_value)
        {
            min_ydiff_value = ydiff[j]; 
            min_ydiff_index = j;
        }
    }
    if(min_ydiff_index>=0) // if condition just to be on the safe side
    {
        uncert[min_ydiff_index] = false;
        std::cout<<"Clearing uncertainty flag at bit position "<<min_ydiff_index;
        if(min_ydiff_value>0.7)
        {
            output_values[min_ydiff_index] = !output_values[min_ydiff_index];
            std::cout<<" and flipping likely output value.";
        }
        std::cout<<"\n";
    }
}

bool solver_t::solve(rmap_t& keysFound, bool allow_fork, bool deadlock)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;
    // std::cout << "Entering solve() of solver.cpp" << std::endl;
    // assert( SOLVER_V0 == ver );

    bool done = false;
    bool result = S.solve(l_out);
    __sync_fetch_and_add(&iter, 1);
    if(verbose) {
        dbl.dump_solver_state(std::cout, S, lmap);
        std::cout << std::endl;
    }
    std::cout << "Iteration: " << iter 
              << "; vars: " << S.nVars() 
              << "; clauses: " << S.nClauses() 
              << "; decisions: " << S.getNumDecisions() << std::endl;

    if(false == result) 
    {
        done = true;
        std::cout << "No more satisfiable!!." << std::endl;
        return done;
    }


    // now extract the inputs.
    for(unsigned i=0; i != dbl.dbl->num_ckt_inputs(); i++) {
        int jdx  = dbl.dbl->ckt_inputs[i]->get_index();
        lbool val = S.modelValue(lmap[jdx]);
        assert(val.isDef());
        if(!val.getBool()) {
            input_values[i] = false;
        } else {
            input_values[i] = true;
        }
    }

    std::cout<<"Distinguishing input "; //<<input_values<<" (";
    bool2hex(input_values); std::cout<<"\n";

    bool force_proceed = allow_fork || deadlock;
    bool no_proceed = set_fork(force_proceed);
    if(no_proceed)
        return done;

    // int dummy; std::cout<<"Enter any number to continue - "; std::cin>>dummy;

    // Now obtain average error rate with random or some satisfying keys
    std::vector<double> yerr_avg(ckt.num_outputs(),0);
    if(iter==1)
        yerr_avg = random_error_rate(input_values);
    else
        yerr_avg = satis_error_rate(input_values);   

    _record_input_values(yerr_avg, allow_fork, deadlock);
    
    // _sanity_check_model();

    struct rusage ru_current;
    getrusage(RUSAGE_SELF, &ru_current);
    if(utimediff(&ru_current, &ru_start) > time_limit) {
        std::cout << "timeout in the slice loop." << std::endl;
        //break;
        done = true;
    }

    return done;
#if 0
    _verify_solution_sat();
#endif
}

// Ankit - Decide whether there is a need for forking of instance
bool solver_t::set_fork(bool force_proceed)
{
    int iovecs = iovectors.size();
    bool no_proceed = false;
    if(iovecs>=1 && are_equal(input_values,iovectors[iovecs-1].inputs)) // if this is iteration 2 or more
    {
        std::cout<<"Distinguishing input same as that of previous iteration\n";
        fork = true;
        if(!force_proceed)
        {
            std::cout<<"Forking not allowed at this stage\n";
            no_proceed = true; // no need to proceed with the rest of the iteration 
        }
    }
    else if (iovecs>=2 && are_equal(input_values,iovectors[iovecs-2].inputs)) // if this is iteration 3 or more
    {
        std::cout<<"Distinguishing input same as that of 2 iterations prior\n";
        fork = true;
        if(!force_proceed)
        {
            std::cout<<"Forking not allowed at this stage\n";
            no_proceed = true; // no need to proceed with the rest of the iteration 
        }
    }
    else
        fork = false;

    return no_proceed;
}

// Ankit - Estimate the average error rate with some satisfying keys
std::vector<double> solver_t::satis_error_rate(const std::vector<bool> dist_input)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;

    solver_t *Sdup = new solver_t(ckt,simckt,0); 
    Sdup->feed_iovectors(iovectors,false);
    // std::cout<<"After duplicating, I/O vectors of Sdup:\n";
    // Sdup->dump_iovectors(-1);
    std::vector<double> yerr_avg(ckt.num_outputs(),0);
    int count=0;
    while(count<ckt.trials_error)
    {
        bool result = Sdup->fake_solve(false);
        if(!result)
            break;
        std::vector<bool> key1(ckt.num_key_inputs());
        std::vector<bool> key2(ckt.num_key_inputs());
        // std::cout<<"With Sdup -- ";
        Sdup->get_distinguishing_keys(key1,key2);

        std::pair<std::vector<double>,std::vector<double>> output_err = ckt.evaluate_probs(dist_input,to_double(key2),true,ckt.estimated_error); // with considering error
        std::vector<double> yprobs = output_err.first;
        std::vector<double> yerr = output_err.second;

        for (int i = 0; i < ckt.num_outputs(); ++i)
            yerr_avg[i] += yerr[i];

        Sdup->add_forbidden_key(key2);
        count++;
    }
    if(count>0)
    {
        for (int i = 0; i < ckt.num_outputs(); ++i)
        {
            yerr_avg[i] /= count;
        }
    }

    std::cout<<"Average error with "<<count<<" satisfying keys: ";
    // dump_vec_dbl(yerr_avg); 
    std::cout<<"\n";

    delete Sdup;
    return yerr_avg;
}


// Ankit - Get the 2 keys which can distinguish with the set of recorded DIPs
void solver_t::get_distinguishing_keys(std::vector<bool>& key1, std::vector<bool>& key2)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;

    // std::cout<<"2 Keys may be: ";
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) 
    {
        int idx = ckt.key_inputs[i]->get_index();
        lbool val1 = S.modelValue(lmap[dbl.pair_map[idx].first->get_index()]);
        lbool val2 = S.modelValue(lmap[dbl.pair_map[idx].second->get_index()]);
        assert(val1.isDef()); assert(val2.isDef());
        key1[i] = val1.getBool(); key2[i] = val2.getBool();
    }
    //std::cout<<key1<<" ("; 
    // bool2hex(key1); std::cout<<" and ";
    //std::cout<<") and "<<key2<<" ("; 
    // bool2hex(key2); std::cout<<"\n";
}

// Ankit - Add forb_key as a key value that should not be returned as a 
// key which satisfies the recorded DIPs 
void solver_t::add_forbidden_key(std::vector<bool> forb_key)
{
    // std::cout<<"Entering add_forbidden_key()\n";
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;

    vec_lit_t newClause;
    // std::cout<<"Vars are: ";
    for(unsigned i=0; i != ckt.num_key_inputs(); ++i) 
    {
        int idx = ckt.key_inputs[i]->get_index();
        int idxB = dbl.pair_map[idx].second->get_index();
        Var vB = var(lmap[idxB]);
        // std::cout<<vB<<" ";
        Lit l_ij = mkLit(vB);
        if(forb_key[i]==true)
            newClause.push(~l_ij);
        else
            newClause.push(l_ij);
    }
    // std::cout<<"\n";
    S.addClause(newClause);
}

// Ankit - Estimate the average error rate with some random keys
std::vector<double> solver_t::random_error_rate(const std::vector<bool> dist_input)
{
    std::vector<double> yerr_avg(ckt.num_outputs(),0);
    for(int trial = 0; trial < ckt.trials_error; trial++)
    {
        std::vector<bool> k(ckt.num_key_inputs());
        std::vector<double> yprobs(ckt.num_outputs());
        std::vector<double> yerr(ckt.num_outputs());
        
        for (int i = 0; i < ckt.num_key_inputs(); ++i)
            k[i] = (bool)std::round((double)rand() / RAND_MAX);

        std::pair<std::vector<double>,std::vector<double>> output_err = ckt.evaluate_probs(dist_input,to_double(k),true,ckt.estimated_error);
        yprobs = output_err.first;
        yerr = output_err.second;
        for (int i = 0; i < ckt.num_outputs(); ++i)
            yerr_avg[i] += yerr[i]/ckt.trials_error;
    }

    std::cout<<"Average error with "<<ckt.trials_error<<" random keys: ";
    // dump_vec_dbl(yerr_avg); 
    std::cout<<"\n";
    return yerr_avg;
}

// Ankit - Before the attack starts, the attacker tries to estimate the error rate in the circuit
double solver_t::estimate_error_rate()
{
    using namespace ckt_n;
    std::cout<<"Attacker estimating error rate... \n";
    double err_step = 0.1; 
    int num_rand_inputs = 100;
    int num_rand_keys=10;
    double avg_err_cross = 0;
    double actual_error_max=0, actual_error_avg=0;
    for(int inp=0; inp<num_rand_inputs; inp++)
    {
        std::vector<bool> input(ckt.num_ckt_inputs(),false);
        for(int i=0; i<input.size(); i++)
            if((static_cast<double>(rand()) / RAND_MAX)<0.5)
                input[i] = true;  // 50% chance of being 1

        // generate keys randomly
        std::vector<std::vector<bool>> random_keys(num_rand_keys,std::vector<bool>(ckt.num_key_inputs()));
        for(int i=0; i<num_rand_keys; i++)
        {
            for(int j=0;j<ckt.num_key_inputs(); j++)
            {
                random_keys[i][j] = bool(rand()%2);
            }
        }

        std::vector<double> mean_actual_output(ckt.num_outputs());
        mean_actual_output = ckt.evaluate_sim(input,ckt.known_correct_key,true,false); 
            // 4th argument false because use actual error rates here

        std::vector<double> apparent_error(ckt.num_outputs());
        for(int i=0; i<ckt.num_outputs(); i++)
            apparent_error[i] = std::min(mean_actual_output[i], 1-mean_actual_output[i]);

        // std::cout<<"Input: "; bool2hex(input); std::cout<<"\n";
        
        std::vector<double> output_no_error(ckt.num_outputs());
        output_no_error = ckt.evaluate_sim(input,ckt.known_correct_key,false,false);            
        // std::cout<<"Output w/o error: "; dump_vec_dbl(output_no_error); std::cout<<"\n";

        std::vector<double> actual_error(ckt.num_outputs());
        for(int i=0; i<ckt.num_outputs();i++)
            actual_error[i] = std::abs(output_no_error[i] - mean_actual_output[i]);
      
        // std::cout<<"Oracle Output: "; dump_vec_dbl(mean_actual_output); std::cout<<"\n"; 
        // std::cout<<"Apparent error = "; dump_vec_dbl(apparent_error); std::cout<<"\n"; 
        // std::cout<<"Actual error = "; dump_vec_dbl(actual_error); std::cout<<"\n";

        // get maximum and average of actual error
        for(int i=0; i<ckt.num_outputs();i++)
        {
            actual_error_avg += actual_error[i]/ckt.num_outputs() ;
            actual_error_max = std::max(actual_error[i],actual_error_max);
        }


        int trials = 100; double err = 0.0;
        double min_err_cross = -0.01;
        for(int trial=0;trial<trials;trial++)
        {
            // std::cout<<"Gate error-rate of "<<err<<"\n"; 

            // initialize error rate
            ckt.est_error_rate = err; // for now set estimated error rate to err
     
            std::vector<double> avg_error_est(ckt.num_outputs(),0);
            std::vector<double> avg_error_calc(ckt.num_outputs(),0);
            for(int j=0;j<num_rand_keys;j++)
            { 
                std::pair<std::vector<double>,std::vector<double>> output_no_err = ckt.evaluate_probs(input,to_double(random_keys[j]),false,false); // w/o considering error
                std::vector<double> y = output_no_err.first;
                std::pair<std::vector<double>,std::vector<double>> output_err = ckt.evaluate_probs(input,to_double(random_keys[j]),true,true); 
                // with considering error, 4th argument true because use ckt.est_error_rate
                std::vector<double> yprobs = output_err.first;
                std::vector<double> yerr = output_err.second;

                std::vector<double> y_diff(ckt.num_outputs());
                for (int i = 0; i < ckt.num_outputs(); ++i)
                {
                    y_diff[i] = std::abs(y[i]-yprobs[i]);
                    avg_error_est[i] += y_diff[i];
                    avg_error_calc[i] += yerr[i];
                }
                // std::cout<<"Key: "; bool2hex(k_exp); 
                // std::cout<<" Estimated error: "; dump_vec_dbl(y_error); std::cout<<"\n";
            }
            for(int i = 0; i < ckt.num_outputs(); ++i)
            {
                avg_error_est[i] /= num_rand_keys;
                avg_error_calc[i] /= num_rand_keys;
            }

            // std::cout<<"Average output error rates over "<<num_rand_keys;
            // std::cout<<" random keys = "; dump_vec_dbl(avg_error_calc); std::cout<<"\n";

            std::vector<int> crossers;
            for(int i = 0; i < ckt.num_outputs(); ++i)
            {
                if(avg_error_calc[i]>=(apparent_error[i])) // avg_error_est[i]>=(apparent_error[i])
                {
                    crossers.push_back(i);
                    // std::cout<<crossers[crossers.size()-1]<<" ";
                    if(crossers.size()>=std::floor(0.5*ckt.num_outputs()))
                    {
                        min_err_cross = err; 
                        // std::cout<<"output bits, avg error crosses apparent error at err="<<err<<"\n";
                        break;
                    }
                }
            }
            if(min_err_cross>=0)
                break;
            
            err += err_step;
            // std::cout<<"\n";
        }
        avg_err_cross += min_err_cross/num_rand_inputs;
        // std::cout<<"Error cross at "<<min_err_cross<<"\n";
        // int dummy; std::cout<<"Enter any number "; cin>>dummy;
    }
    actual_error_avg /= num_rand_inputs;
    // std::cout<<"Actual error average = "<<actual_error_avg<<", maximum = "<<actual_error_max<<"\n";
    avg_err_cross = std::max(avg_err_cross,0.0);
    std::cout<<"Average error cross (estimated error rate) = "<<avg_err_cross<<"\n";
    return avg_err_cross;
}

double solver_t::get_norm(std::vector<double> x, std::vector<double> y, int n)
{
    double norm = 0;
    assert(x.size()==y.size());
    for(int i=0; i<x.size(); i++)
        norm += std::pow(std::abs(x[i]-y[i]),n);
    norm = std::pow(norm,1.0/n);
    return norm;
}

bool solver_t::verify_solution_sim(rmap_t& keysFound)
{
    using namespace sat_n;
    using namespace ckt_n;
    // std::cout << "Entering verify_solution_sim() of solver.cpp" << std::endl;

    // JOHANN
    //
    int steps = 1;
    bool key_extracted = false;
    std::vector<double> ydiff_avg(ckt.num_outputs(),0);
    std::vector<double> ydiff_max(ckt.num_outputs(),0);
    final_avg_ydiff=0; final_max_ydiff=0;
    MAX_VERIF_ITER = ckt.test_patterns;

    // also consider how many patterns can be investigated at all, considering the number of PIs
    unsigned long possible_patterns = std::pow(2, cktinput_literals.size());
    // however, ignore overflows (would be zero for unsigned)
    if (possible_patterns > 0)
        MAX_VERIF_ITER = std::min(MAX_VERIF_ITER, possible_patterns);

    std::cout << "Verifying key for " << MAX_VERIF_ITER << " test patterns, (";
    std::cout<<ckt.IO_sampling_iter<<" samples for each pattern) ..." << std::endl;

    if (possible_patterns > 0) {
        std::cout << " (Max possible patterns: " << possible_patterns << ")" << std::endl;
    }

    for(int iter=0; iter < MAX_VERIF_ITER;  iter++) 
    {
        vec_lit_t assumps;
        std::vector<bool> input(ckt.num_ckt_inputs());
        std::vector<bool> output(ckt.num_outputs());

        // generate random test pattern
        for(unsigned i=0; i != cktinput_literals.size(); i++) 
        {
            bool vi = bool(rand() % 2);
            input[i] = vi;
        }

        for(unsigned i=0; i != cktinput_literals.size(); i++) {
            // modified from original line above
            assumps.push( input[i] ? cktinput_literals[i] : ~cktinput_literals[i]);
        }
        
        if(verbose) {
            std::cout << "input: " << input 
                      << "; output: " << output << std::endl;
            dump_clause(std::cout << "assumps: ", assumps) << std::endl;
        }
        //std::cout << "input: " << input_values << "; Oracle output: " << output_values;

        if(S.solve(assumps) == false) {
            std::cout << "UNSAT model!" << std::endl;
            return false;
        }

        if(verbose) {
            dump_clause(std::cout << "sat input: ", assumps) << std::endl;
            std::cout << "sat output: ";
        }

        if (!key_extracted) 
        {
            _extractSolution(keysFound);
            key_extracted = true;
            std::cout<<"Key found = "<<key_found<<" (";
            bool2hex(key_found); std::cout<<")\n";
        }

        // evaluate response of unlocked circuit with the key found using the estimated error rate
        std::vector<double> y1 = ckt.evaluate_sim(input,key_found,true,ckt.estimated_error);

        // observe response from oracle - the locked ckt fitted with correct key
        std::vector<double> y2(ckt.num_outputs());
        y2 = ckt.evaluate_sim(input,ckt.known_correct_key,true,false);
        
        std::vector<double> ydiff(ckt.num_outputs());
        for (int i = 0; i < ydiff.size(); ++i)
        {
            ydiff[i] = std::abs(y1[i]-y2[i]);
            ydiff_avg[i] += ydiff[i];
            if(ydiff[i]>ydiff_max[i])
                ydiff_max[i] = ydiff[i];
        }

        // JOHANN
        if ((iter + 1) % (MAX_VERIF_ITER / 20) == 0) {
            std::cout << steps * 5 << " \% done ..." << std::endl;
            steps++;
        }
    }

    double ydiff_avg_all = 0;
    std::cout<<"Average output probs difference in oracle and unlocked circuit:\n";
    for (int i = 0; i < ydiff_avg.size(); ++i)
    {
        ydiff_avg[i] /= MAX_VERIF_ITER;
        ydiff_avg_all += ydiff_avg[i];
    }
    dump_vec_dbl(ydiff_avg); std::cout<<"\n";
    final_avg_ydiff = ydiff_avg_all/ckt.num_outputs(); 
    std::cout<<"Average of average = "<<final_avg_ydiff<<"\n";

    std::cout<<"Maximum output probs difference in oracle and unlocked circuit:\n";
    dump_vec_dbl(ydiff_max);
    for(int i=0;i<ydiff_max.size();i++)
        final_max_ydiff += ydiff_max[i]/ydiff_max.size();
    std::cout<<"Average of maximum = "<<final_max_ydiff<<"\n";

    return true;
}

// Ankit - This function compares the response of the oracle circuit 
// to that of the unlocked circuit having the actual error rates (and not the estimated ones)
void solver_t::final_solution(std::vector<bool> key)
{
    using namespace sat_n;
    using namespace ckt_n;
    int steps=1;
    std::vector<double> ydiff_avg(output_values.size());
    MAX_VERIF_ITER = ckt.test_patterns;

    // also consider how many patterns can be investigated at all, considering the number of PIs
    unsigned long possible_patterns = std::pow(2, cktinput_literals.size());
    // however, ignore overflows (would be zero for unsigned)
    if (possible_patterns > 0)
        MAX_VERIF_ITER = std::min(MAX_VERIF_ITER, possible_patterns);

    std::cout << "Verifying key for " << MAX_VERIF_ITER << " test patterns, (";
    std::cout<<ckt.IO_sampling_iter<<" samples for each pattern) ..." << std::endl;

    if (possible_patterns > 0) {
        std::cout << " (Max possible patterns: " << possible_patterns << ")" << std::endl;
    }

    std::vector<bool> input(ckt.num_ckt_inputs());
    std::vector<bool> output(ckt.num_outputs());

    for(int iter=0; iter < MAX_VERIF_ITER;  iter++) 
    {
        // generate random test pattern
        for(unsigned i=0; i != cktinput_literals.size(); i++) 
            input[i] = bool(rand() % 2);
        
        // evaluate response of unlocked circuit with the key found with the actual error rate
        std::vector<double> y1 = ckt.evaluate_sim(input,key,true,false);

        std::vector<double> y2(ckt.num_outputs());
        // observe response from oracle - the locked ckt fitted with correct key
        y2 = ckt.evaluate_sim(input,ckt.known_correct_key,true,false);

        std::vector<double> ydiff(output.size());
        for (int i = 0; i < ydiff.size(); ++i)
        {
            ydiff[i] = std::abs(y1[i]-y2[i]);
            ydiff_avg[i] += ydiff[i];
        }
        if ((iter + 1) % (MAX_VERIF_ITER / 20) == 0) {
            std::cout << steps * 5 << " \% done ..." << std::endl;
            steps++;
        }
    }

    double ydiff_avg_all = 0;
    std::cout<<"Average output probs difference in oracle and unlocked circuit:\n";
    for (int i = 0; i < ydiff_avg.size(); ++i)
    {
        ydiff_avg[i] /= MAX_VERIF_ITER;
        ydiff_avg_all += ydiff_avg[i];
    }
    dump_vec_dbl(ydiff_avg); std::cout<<"\n";
    final_avg_ydiff = ydiff_avg_all/output.size(); 
    std::cout<<"Average of average = "<<final_avg_ydiff<<"\n";

}

void solver_t::_extractSolution(rmap_t& keysFound)
{
    using namespace sat_n;
    // std::cout << "Entering _extractSolution() of solver.cpp" << std::endl;

    for(unsigned i=0; i != keyinput_literals_A.size(); i++) 
    {
        lbool v = S.modelValue(keyinput_literals_A[i]);
        if(!v.getBool()) {
            keysFound[ckt.key_inputs[i]->name] = 0;
            key_found[i] = false;
        } else {
            keysFound[ckt.key_inputs[i]->name] = 1;
            key_found[i] = true;
        }
    }
}

// Ankit - check if 2 boolean vectors are equal
bool solver_t::are_equal(std::vector<bool> x, std::vector<bool> y)
{
    assert(x.size()==y.size());
    bool equal = true;
    for (int i = 0; i < x.size(); ++i)
    {
        if(x[i]!=y[i])
        {
            equal = false;
            break;
        }
    }
    return equal;
}

//Ankit - conversion from vector of bool to vector of double
std::vector<double> solver_t::to_double(std::vector<bool> key)
{
    std::vector<double> key_dbl(key.size());
    for (int i = 0; i < key.size(); ++i)
        key_dbl[i] = (double)key[i];
    return key_dbl;
}

void solver_t::dump_vec_dbl(std::vector<double> x)
{
    std::cout.precision(3);
    for (int i = 0; i < x.size(); ++i)
        std::cout<<x[i]<<", ";
}

//Ankit - feed_iovectors() manually provides a set of I/O pairs to a SAT solver
// mostly the same set of actions as that performed in _record_input_values()
void solver_t::feed_iovectors(iovalue_vector_t iovectors, bool skip_last) 
{
    int last;
    if(skip_last)
    {
        last = iovectors.size()-2; // all but the last 2 I/O vectors to be fed
        // std::cout<<"last = "<<last<<"\n";
    }
    else
        last = iovectors.size();
    for (int pair = 0; pair < last; ++pair)
    {   
        input_values = iovectors[pair].inputs;
        if((pair+1)<last && are_equal(input_values,iovectors[pair+1].inputs))
            continue;
            // if the distinguishing input is same as that of the next iteration
            // then skip this one because the next I/O pair has more information

        output_values = iovectors[pair].outputs;
        feed_iovector(input_values,output_values,iovectors[pair].uncertainty);
    }
}

void solver_t::feed_iovector(std::vector<bool> input, std::vector<bool> output, std::vector<bool> uncert)
{
    // fake_solve(true);
    std::vector<sat_n::lbool> values(S.nVars(), sat_n::l_Undef);
    _record_sim(input,output,values,uncert);
    int cnt = cl.addRewrittenClauses(values, dbl_keyinput_flags, S);
    __sync_fetch_and_add(&cube_count, cnt);
}

bool solver_t::fake_solve(bool show_alert)
{
    bool result = S.solve(l_out);
    if(show_alert && !result)
        std::cout<<"ALERT: Unsolvable !!!"<<std::endl;
    return result;
}

// Ankit - print num_vecs I/O pairs
void solver_t::dump_iovectors(int num_vecs)
{
    int start;
    if(num_vecs<0)
        start=0;
    else
        start = std::max(0,(int)iovectors.size() - num_vecs);

    for (int i = start; i < iovectors.size(); ++i)
    {
        std::cout<<(i+1)<<") ";
        dump_iovector(iovectors[i].inputs,iovectors[i].outputs,iovectors[i].uncertainty);
    }
}

// Ankit - print the DIP
void solver_t::dump_iovector(std::vector<bool> inp,std::vector<bool> out,std::vector<bool> uncert)
{
    using namespace sat_n;
    using namespace ckt_n;
    using namespace AllSAT;
    std::cout << "input: ";
    bool2hex(inp);
    std::cout<<";output: ";
    for (int j = 0; j < out.size(); ++j)
    {
        if (uncert[j])
            std::cout<<'x';
        else
            std::cout<<out[j];
    }
    std::cout<<std::endl;
}

void solver_t::_sanity_check_model()
{
    using namespace sat_n;
    using namespace ckt_n;
    std::cout << "Entering _sanity_check_model() of solver.cpp" << std::endl;
    bool pass = true;
    vec_lit_t assumps;
    std::vector<bool> actual_output_values;

    for(unsigned i=0; i != cktinput_literals.size(); i++) {
        bool vi = input_values[i];
        assumps.push( vi ? cktinput_literals[i] : ~cktinput_literals[i]);
    }
    if(verbose) dump_clause(std::cout << "assumps: ", assumps) << std::endl;
    if(S.solve(assumps) == false) {
        std::cout << "UNSAT result during sanity check." << std::endl;
        std::cout << "result of no-assumption solve: " << S.solve() << std::endl;
        exit(1);
    }
    if(verbose) {
        std::cout << "[expected] input: " << input_values 
            << "; output: " << output_values << std::endl;
    }

    if(verbose) {
        dump_clause(std::cout << "sat input: ", assumps) << std::endl;
        std::cout << "sat output: ";
    }
    for(unsigned i=0; i != output_values.size(); i++) {
        bool vi = output_values[i];
        lbool ri = S.modelValue(output_literals_A[i]);
        if(verbose) {
            std::cout << (ri.isUndef() ? "-" : (ri.getBool() ? "1" : "0"));
        }
        if(!(ri.isDef() && ri.getBool() == vi)) { 
            pass = false;
        }
    }
    if(verbose) std::cout << std::endl;

    if(pass) {
        if(verbose) {
            std::cout << "simulation sanity check passed." << std::endl;
        }
    } else {
        std::cout << "simulation sanity check failed." << std::endl;
        exit(1);
    }
}

void solver_t::blockKey(rmap_t& keysFound)
{
    using namespace sat_n;
    std::cout << "Entering blockKey() of solver.cpp" << std::endl;

    vec_lit_t bc_A(keyinput_literals_A.size());
    vec_lit_t bc_B(keyinput_literals_A.size());
    for(unsigned i=0; i != keyinput_literals_A.size(); i++) {
        auto &&name_i = ckt.key_inputs[i]->name;
	// JOHANN
	if (keysFound.find(name_i) == keysFound.end())
		return;
//        assert (keysFound.find(name_i)  != keysFound.end());
        auto ki = keysFound[ckt.key_inputs[i]->name];
        bc_A[i] = ki ? ~keyinput_literals_A[i] : keyinput_literals_A[i];
        bc_B[i] = ki ? ~keyinput_literals_B[i] : keyinput_literals_B[i];
    }
    S.addClause(bc_A);
    S.addClause(bc_B);
}

bool solver_t::getNewKey(rmap_t& keysFound)
{
    std::cout << "Entering getNewKey() of solver.cpp" << std::endl;
    keysFound.clear();
    return verify_solution_sim(keysFound);
}

unsigned int solver_t::bool2value(std::vector<bool> x)
{
    unsigned int y = 0, base=1;
    for (int i = x.size()-1; i >= 0 ; --i)
    {
        y = y + x[i]*base;
        base *= 2;
    }
    return y;
}

std::vector<double> solver_t::get_uncert_values()
{
    return uncert_values;
}

int solver_t::get_IO_sampling_iter()
{
    using namespace ckt_n;
    return simckt.IO_sampling_iter;
}

void solver_t::bool2hex(std::vector<bool> y)
{
    std::vector<bool> x = y;
    // pad zeros at the end of x to make size multiple of 4
    for (int i = 0; i < (4 - (y.size()%4))%4; ++i)
        x.push_back(false);
    int n = x.size()/4; 
    for (int i = 0; i < n; ++i)
    {
        std::vector<bool> chunk_bool = {x[4*i],x[4*i+1],x[4*i+2],x[4*i+3]};
        int chunk = bool2value(chunk_bool);
        std::cout<<std::hex<<chunk;
    }
    std::cout<<std::dec<<"";
}

bool solver_t::_verify_solution_sat()
{
    using namespace sat_n;
    std::cout << "Entering _verify_solution_sat() of solver.cpp" << std::endl;
    vec_lit_t c1, c2;

    assert(keyinput_literals_A.size() == keyinput_literals_B.size());
    c1.growTo(keyinput_literals_A.size()+1);
    c2.growTo(keyinput_literals_B.size()+1);

    for(unsigned i=0; i != keyinput_literals_A.size(); i++) {
        c1[i+1] = ~keyinput_literals_A[i];
        c2[i+1] = ~keyinput_literals_B[i];
    }
    c1[0] = l_out;
    c2[0] = l_out;
    if(S.solve(c1) == false && S.solve(c2) == false) {
        return true;
    } else {
        return true;
    }
}

int solver_t::sliceAndSolve( rmap_t& keysFoundMap, int maxKeys, int maxNodes )
{
    using namespace ckt_n;
    using namespace sat_n;
    std::cout << "Entering sliceAndSolve() of solver.cpp" << std::endl;
    if(maxNodes != -1) {
        assert(maxNodes <= (int) ckt.nodes.size());
        assert(maxNodes > 0);
    }
    if(maxKeys != -1) {
        assert(maxKeys <= (int) ckt.num_key_inputs());
        assert(maxKeys > 0);
    }

    // initialize the maps.
    assert(ckt.gates.size() == ckt.gates_sorted.size());
    assert(ckt.nodes.size() == ckt.nodes_sorted.size());

    std::vector< std::vector<bool> > keymap, outputmap;
    ckt.init_keymap(keymap);
    ckt.init_outputmap(outputmap);

    IloEnv env;

    int numNodeVars = ckt.num_nodes();
    int numOutputVars = ckt.num_outputs();
    int numKeyVars = ckt.num_key_inputs();
    int totalVars = numNodeVars + numOutputVars + numKeyVars;

    auto outVarIndex = [numNodeVars](int i) -> int 
        { return numNodeVars + i; };
    auto keyVarIndex = [numNodeVars, numOutputVars](int i) -> int 
        { return numNodeVars + numOutputVars + i; };


    // first create the variables.
    IloNumVarArray vars(env);
    int i;
    for(i=0; i != numNodeVars; i++) {
        char name[64];
        sprintf(name, "n_%s_%d", ckt.nodes[i]->name.c_str(), i);
        vars.add(IloNumVar(env, 0, 1, ILOINT, name));
    }
    for(; i < numNodeVars+numOutputVars; i++) {
        char name[64];
        sprintf(name, "o_%s_%d", ckt.outputs[i-numNodeVars]->name.c_str(), i);
        vars.add(IloNumVar(env, 0, 1, ILOINT, name));
        assert(outVarIndex(i-numNodeVars) == i);
    }
    for(; i < numNodeVars+numOutputVars+numKeyVars; i++) {
        char name[64];
        sprintf(name, "k_%s_%d", ckt.key_inputs[i-numNodeVars-numOutputVars]->name.c_str(), i);
        vars.add(IloNumVar(env, 0, 1, ILOINT, name));
        assert(keyVarIndex(i-numNodeVars-numOutputVars) == i);
    }
    assert(i == totalVars);

    // and then the constraints.
    IloRangeArray cnst(env);
    int ccPos=0; // current constraint position.

    // for each key ki: ki => oj for each ouput j that is affected bby ki
    // ki => oj translates to -ki + oj >= 0
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        std::vector<int> output_indices;
        ckt.get_indices_in_bitmap(outputmap, ckt.key_inputs[i], output_indices);
        for(auto it = output_indices.begin(); it != output_indices.end(); it++) {
            unsigned j=*it;
            int vi = keyVarIndex(i);
            int vj = outVarIndex(j);
            char name[16]; sprintf(name, "c%d", ccPos);
            cnst.add(IloRange(env, 0, IloInfinity, name));
            cnst[ccPos].setLinearCoef(vars[vi], -1);
            cnst[ccPos].setLinearCoef(vars[vj], 1);
            ccPos += 1;
        }
    }

    // for each output oi, and each node nj in the fanin cone of that output:
    // oi => nj <=> -oi + nj >= 0
    ckt.check_sanity();
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        std::vector<bool> fanin(ckt.num_nodes());
        ckt.compute_transitive_fanin(ckt.outputs[i], fanin);

        for(int j=0; j != (int)fanin.size(); j++) {
            if(fanin[j]) {
                node_t* nj = ckt.nodes[j];
                assert(nj->get_index() == j);
                int vi = outVarIndex(i);
                int vj = j;
                char name[16]; sprintf(name, "c%d", ccPos);
                cnst.add(IloRange(env, 0, IloInfinity, name));
                cnst[ccPos].setLinearCoef(vars[vi], -1);
                cnst[ccPos].setLinearCoef(vars[vj], 1);
                ccPos += 1;
            }
        }
    }

    // now create the objective function
    IloObjective obj;
    IloNumArray coeffs(env, totalVars);
    if(maxKeys == -1) {
        // maximize the number of selected keys.
        obj = IloMaximize(env);
        for(int i=0; i != numKeyVars; i++) {
            coeffs[keyVarIndex(i)] = 1;
        }
    } else {
        // minimize: number of selected nodes.
        obj = IloMinimize(env);
        for(int i=0; i != numNodeVars; i++) {
            coeffs[i] = 1;
        }
    }
    obj.setLinearCoefs(vars, coeffs);

    if(maxNodes != -1) {
        // n1 + n2 + ... + nk <= maxNodes
        char name[16]; sprintf(name, "c%d", ccPos);
        cnst.add(IloRange(env, 0, maxNodes, name));
        int nodeCnstPos = ccPos++;
        for(int i=0; i != numNodeVars; i++) {
            cnst[nodeCnstPos].setLinearCoef(vars[i], 1);
        }
    }
    if(maxKeys != -1) {
        // k1 + k2 + ... + kl >= maxKeys
        char name[16]; sprintf(name, "c%d", ccPos);
        cnst.add(IloRange(env, maxKeys, IloInfinity, name));
        int keyCnstPos = ccPos++;
        for(int i=0; i != numKeyVars; i++) {
            int ki = keyVarIndex(i);
            cnst[keyCnstPos].setLinearCoef(vars[ki], 1);
        }
    } else {
        // k1 + k2 + ... + kl >= 1
        char name[16]; sprintf(name, "c%d", ccPos);
        cnst.add(IloRange(env, 1, IloInfinity, name));
        int keyCnstPos = ccPos++;
        for(int i=0; i != numKeyVars; i++) {
            int ki = keyVarIndex(i);
            cnst[keyCnstPos].setLinearCoef(vars[ki], 1);
        }
    }

    IloModel model(env);
    model.add(obj);
    model.add(cnst);
    IloCplex cplex(model);
    cplex.setOut(env.getNullStream());
    cplex.setParam(IloCplex::TiLim, 1);
    cplex.setParam(IloCplex::EpGap, 0.25);

    static int cnt=0;
    char buf[24]; sprintf(buf, "model%d.lp", cnt++);
    cplex.exportModel(buf);

    if(!cplex.solve()) {
        return 0;
    }

    IloNumArray vals(env);
    cplex.getValues(vals, vars);

    slice_t slice(ckt, simckt);
    for(int i=0; i != numOutputVars; i++) {
        int index = outVarIndex(i);
        int vi = (int) vals[index];
        if(vi) {
            slice.outputs.push_back(i);
        }
    }

    //std::cout << "selected keys: ";
    for(int i =0; i != numKeyVars; i++) {
        int index = keyVarIndex(i);
        int vi = (int) vals[index];
        if(vi) {
            slice.keys.push_back(i);
            //std::cout << i << "/" << index << " ";
        }
    }

    assert(maxNodes < (int) ckt.num_nodes() || slice.keys.size() == ckt.num_key_inputs());
    //std::cout << std::endl;

    std::cout << "# of outputs: " << slice.outputs.size() << std::endl;
    std::cout << "# of keys: " << slice.keys.size() << std::endl;
    std::cout << "objective : " << cplex.getObjValue() << std::endl;

    std::map<int, int> keysFound;
    rmap_t allKeys;
    this->solveSlice(slice, keysFound, allKeys);
    for(auto it = keysFound.begin(); it != keysFound.end(); it++) {
        int keyIndex = it->first;
        int keyValue = it->second;
        const std::string& keyName = ckt.key_inputs[keyIndex]->name;
        keysFoundMap[keyName] = keyValue;
    }
    if(ckt.num_key_inputs() == slice.cktslice->num_key_inputs()) {
        for(auto it = allKeys.begin(); it != allKeys.end(); it++) {
            int keyValue = it->second;
            const std::string& keyName = it->first;
            keysFoundMap[keyName] = keyValue;
        }
    }

    std::vector<lbool> keyValues(ckt.num_key_inputs(), sat_n::l_Undef);
    for(unsigned ki=0; ki != ckt.num_key_inputs(); ki++) {
        auto pos = keysFound.find(ki);
        if(pos != keysFound.end()) {
            keyValues[ki] = (pos->second ? sat_n::l_True : sat_n::l_False);
        }
        else if(allKeys.size() != 0 && ckt.num_key_inputs() == slice.cktslice->num_key_inputs()) {
            auto mapPos = allKeys.find(ckt.key_inputs[ki]->name);
            assert(mapPos != allKeys.end());
            keyValues[ki] = (mapPos->second ? sat_n::l_True : sat_n::l_False);
        }
    }
    ckt.rewrite_keys(keyValues);
    return slice.outputs.size();
}

void solver_t::sliceAndDice(
    ckt_n::ckt_t& ckt, 
    ckt_n::ckt_t& sim, 
    std::vector<slice_t*>& slices
)
{
    using namespace ckt_n;
    std::cout << "Entering sliceAndDice() of solver.cpp" << std::endl;
    // initialize the maps.
    std::vector< std::vector<bool> > keymap, outputmap;
    ckt.init_keymap(keymap);
    ckt.init_outputmap(outputmap);

    IloEnv env;

    int numNodeVars = ckt.num_nodes();
    int numOutputVars = ckt.num_outputs();
    int numKeyVars = ckt.num_key_inputs();
    int totalVars = numNodeVars + numOutputVars + numKeyVars;

    // std::cout << "# of outputs: " << numOutputVars << std::endl;
    // std::cout << "# of nodes: " << numNodeVars << std::endl;
    // std::cout << "# of keys: " << numKeyVars << std::endl;

    auto outVarIndex = [numNodeVars](int i) -> int 
        { return numNodeVars + i; };
    auto keyVarIndex = [numNodeVars, numOutputVars](int i) -> int 
        { return numNodeVars + numOutputVars + i; };


    // first create the variables.
    IloNumVarArray vars(env);
    int i;
    for(i=0; i != numNodeVars; i++) {
        char name[32];
        sprintf(name, "n%d", i);
        vars.add(IloNumVar(env, 0, 1, ILOINT, name));
    }
    for(; i < numNodeVars+numOutputVars; i++) {
        char name[32];
        sprintf(name, "o%d", i);
        vars.add(IloNumVar(env, 0, 1, ILOINT, name));
        assert(outVarIndex(i-numNodeVars) == i);
    }
    for(; i < numNodeVars+numOutputVars+numKeyVars; i++) {
        char name[32];
        sprintf(name, "k%d", i);
        vars.add(IloNumVar(env, 0, 1, ILOINT, name));
        assert(keyVarIndex(i-numNodeVars-numOutputVars) == i);
    }
    assert(i == totalVars);

    // and then the constraints.
    IloRangeArray cnst(env);
    int ccPos=0; // current constraint position.

    // for each key ki: ki => oj for each ouput j that is affected bby ki
    // ki => oj translates to -ki + oj >= 0
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        std::vector<int> output_indices;
        ckt.get_indices_in_bitmap(outputmap, ckt.key_inputs[i], output_indices);
        for(auto it = output_indices.begin(); it != output_indices.end(); it++) {
            unsigned j=*it;
            int vi = keyVarIndex(i);
            int vj = outVarIndex(j);
            cnst.add(IloRange(env, 0, IloInfinity));
            cnst[ccPos].setLinearCoef(vars[vi], -1);
            cnst[ccPos].setLinearCoef(vars[vj], 1);
            ccPos += 1;
        }
    }
    // for each output oi, and each node nj in the fanin cone of that output:
    // oi => nj <=> -oi + nj >= 0
    for(unsigned i=0; i != ckt.num_outputs(); i++) {
        std::vector<bool> fanin(ckt.num_nodes());
        ckt.compute_transitive_fanin(ckt.outputs[i], fanin);

        for(int j=0; j != (int)fanin.size(); j++) {
            if(fanin[j]) {
                node_t* nj = ckt.nodes[j];
                assert(nj->get_index() == j);
                int vi = outVarIndex(i);
                int vj = j;
                cnst.add(IloRange(env, 0, IloInfinity));
                cnst[ccPos].setLinearCoef(vars[vi], -1);
                cnst[ccPos].setLinearCoef(vars[vj], 1);
                ccPos += 1;
            }
        }
    }
    // now we need to create the objective function
    // minimize: number of selected nodes.
    IloObjective obj = IloMinimize(env);
    IloNumArray coeffs(env, totalVars);
    for(int i=0; i != numNodeVars; i++) {
        coeffs[i] = 1;
    }
    obj.setLinearCoefs(vars, coeffs);

    std::set<int> notYetSelectedKeys;
    for(int i=0; i != numKeyVars; i++) {
        notYetSelectedKeys.insert(i);
    }

    cnst.add(IloRange(env, 1, IloInfinity));
    int keyCnstPos = ccPos++;
    // k1 + k2 + ... + kn >= 1
    while(notYetSelectedKeys.size() > 0) {
        for(int i=0; i != numKeyVars; i++) {
            if(notYetSelectedKeys.find(i) != notYetSelectedKeys.end()) {
                int varNum = keyVarIndex(i);
                cnst[keyCnstPos].setLinearCoef(vars[varNum], 1);
            } else {
                int varNum = keyVarIndex(i);
                cnst[keyCnstPos].setLinearCoef(vars[varNum], 0);
            }
        }

        IloModel model(env);
        model.add(obj);
        model.add(cnst);
        IloCplex cplex(model);
        cplex.setOut(env.getNullStream());
        // cplex.exportModel("model.lp");
        if(!cplex.solve()) {
            std::cerr << "Error: unable to solve cplex model." << std::endl;
            exit(1);
        }
        // std::cout << "status: " << cplex.getStatus() << std::endl;
        // std::cout << "objval: " << cplex.getObjValue() << std::endl;
        IloNumArray vals(env);
        cplex.getValues(vals, vars);

        slice_t *slice = new slice_t(ckt, sim);
        for(int i=0; i != numOutputVars; i++) {
            int index = outVarIndex(i);
            int vi = (int) vals[index];
            if(vi) {
                slice->outputs.push_back(i);
            }
        }
        for(int i =0; i != numKeyVars; i++) {
            int index = keyVarIndex(i);
            int vi = (int) vals[index];
            if(vi) {
                slice->keys.push_back(i);
                notYetSelectedKeys.erase(i);
            }
        }

        std::sort(slice->outputs.begin(), slice->outputs.end());
        std::sort(slice->keys.begin(), slice->keys.end());

        slices.push_back(slice);
    }
}

void solver_t::slice_t::createCkts()
{
    using namespace ckt_n;
    std::cout << "Entering createCkts() of solver.cpp" << std::endl;
    assert(sim.num_outputs() == ckt.num_outputs());

    // create output list.
    nodelist_t cktOutputs, simOutputs;
    for(unsigned i=0; i != outputs.size(); i++) {
        int index = outputs[i];
        assert(index >= 0 && index < (int) ckt.num_outputs());

        node_t* out_i = ckt.outputs[index];
        cktOutputs.push_back(out_i);

        node_t* out_j = sim.outputs[index];
        simOutputs.push_back(out_j);
    }

    // now construct slice.
    cktslice = new ckt_t(ckt, cktOutputs, ckt_nmfwd, ckt_nmrev);
    simslice = new ckt_t(sim, simOutputs, sim_nmfwd, sim_nmrev);
    // make sure the inputs and outputs match up.
    if(!cktslice->compareIOs(*simslice, 1)) {
        std::cerr << "CKT SLICE" << std::endl << cktslice << std::endl;
        std::cerr << "SIM SLICE" << std::endl << simslice << std::endl;
        std::cerr << "Error. Slices are not comparable." << std::endl;
        exit(1);
    }

    //std::cout << "# of nodes: " << ckt.nodes.size() << std::endl;
    //std::cout << "# of fwd mapped nodes: " << ckt_nmfwd.size() << std::endl;

    // create the maps between the key inputs in the original and new circuit.
    // key input map.
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        node_t* ki = ckt.key_inputs[i];
        auto pos = ckt_nmfwd.find(ki);
        if(pos != ckt_nmfwd.end()) {
            node_t* kj = pos->second;
            int index = cktslice->get_key_input_index(kj);
            assert(index != -1);
            cktm_kfwd[i] = index;
            cktm_krev[index] = i;
        }
    }

    // ckt input map.
    for(unsigned i=0; i != ckt.num_ckt_inputs(); i++) {
        node_t* ci = ckt.ckt_inputs[i];
        auto pos = ckt_nmfwd.find(ci);
        if(pos != ckt_nmfwd.end()) {
            node_t* cj = pos->second;
            int index = cktslice->get_ckt_input_index(cj);
            assert(index != -1);
            cktm_cfwd[i] = index;
            cktm_crev[index] = i;
        }
    }
}

void solver_t::solveSlice(
    slice_t& slice, 
    std::map<int, int>& fixedKeys, 
    rmap_t& allKeys 
)
{
    // first create the slice that is requested.
    using namespace ckt_n;
    std::cout << "Entering solveSlice() of solver.cpp" << std::endl;
    assert(slice.sim.num_outputs() == slice.ckt.num_outputs());
    slice.createCkts();

    //std::cout << "slice: outputs=" << slice.cktslice->num_outputs()
    //          << "; ckt_inputs=" << slice.cktslice->num_ckt_inputs()
    //          << "; key_inputs=" << slice.cktslice->num_key_inputs()
    //          << "; gates=" << slice.cktslice->num_gates() << std::endl;

    // std::cout << *slice.cktslice << std::endl;
    // std::cout << *slice.simslice << std::endl;

    // make sure we don't have any known keys.
    // (the caller must've simplified them away).
    for(auto it=fixedKeys.begin();it != fixedKeys.end(); it++) {
        int i1 = it->first;
        int i2 = slice.mapKeyIndexFwd(i1);
        assert(i2 == -1);
    }

    // actual solving.
    solver_t S(*slice.cktslice, *slice.simslice, 0);
    S.MAX_VERIF_ITER = 1;

    S.time_limit = 60;
    getrusage(RUSAGE_SELF, &S.ru_start);
    bool finished = S.solve(allKeys, false, false); // Ankit - modified, do not use 

    if(true||finished) {
        assert(slice.ckt.num_key_inputs() >= slice.cktslice->num_key_inputs());
        if(slice.ckt.num_outputs() > slice.cktslice->num_outputs()) {
            std::map<int, int> sliceKeys;
            S.findFixedKeys(sliceKeys);

            // now translate the keys back to big node.
            for(auto it=sliceKeys.begin(); it != sliceKeys.end(); it++) {
                int i1 = it->first;
                int i2 = slice.mapKeyIndexRev(i1);
                fixedKeys[i2] = it->second;
            }
        }
    }
}

void solver_t::findFixedKeys(std::map<int, int>& backbones)
{
    using namespace ckt_n;
    using namespace sat_n;
    std::cout << "Entering findFixedKeys() of solver.cpp" << std::endl;
    if(iovectors.size() == 0) return;

    Solver Sckt;
    AllSAT::ClauseList cktCl;
    index2lit_map_t cktmap;
    std::vector<bool> keyinputflags;

    ckt.init_solver(Sckt, cktCl, cktmap, true /* don't care. */);
    keyinputflags.resize(Sckt.nVars(), false);
    ckt.init_keyinput_map(cktmap, keyinputflags);

    std::vector<lbool> values(Sckt.nVars(), sat_n::l_Undef);

    for(unsigned i=0; i != iovectors.size(); i++) {
        const std::vector<bool>& inputs = iovectors[i].inputs;
        const std::vector<bool>& outputs = iovectors[i].outputs;

        for(unsigned i=0; i != inputs.size(); i++) {
            int idx = ckt.ckt_inputs[i]->get_index();
            values[var(cktmap[idx])] = inputs[i] ? sat_n::l_True : sat_n::l_False;
        }

        for(unsigned i=0; i != outputs.size(); i++) {
            int idx = ckt.outputs[i]->get_index();
            values[var(cktmap[idx])] = outputs[i] ? sat_n::l_True : sat_n::l_False;
        }
        cktCl.addRewrittenClauses(values, keyinputflags, Sckt);
    }
    // now freeze the ckt inputs.
    for(unsigned i=0; i != ckt.num_ckt_inputs(); i++) {
        Lit li = cktmap[ckt.ckt_inputs[i]->get_index()];
        Sckt.freeze(li);
    }
    // and then freeze the key inputs.
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        Lit li = cktmap[ckt.key_inputs[i]->get_index()];
        Sckt.freeze(li);
    }

    // get an assignment for the keys.
    std::cout << "finding initial assignment of keys." << std::endl;
    bool result = Sckt.solve();
    assert(result);
    std::vector<Lit> keys;
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        int idx = ckt.key_inputs[i]->get_index();
        lbool value = Sckt.modelValue(var(lmap[idx]));
        keys.push_back(value == sat_n::l_True ? lmap[idx] : ~lmap[idx]);
    }
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        //std::cout << "key: " << i << std::endl;
        if(Sckt.solve(~keys[i]) == false) {
            // can't satisfy these I/O combinations with this key value.
            if(sign(keys[i])) {
                backbones[i] = 0;
            } else {
                backbones[i] = 1;
            }
            Sckt.addClause(keys[i]);
        }
    }

#if 0
    for(unsigned i=0; i != iovectors.size(); i++) {
        //std::cout << "iovector: " << i << std::endl;

        const std::vector<bool>& inputs = iovectors[i].inputs;
        _testBackbones(inputs, Sckt, cktmap, backbones);
    }
#endif
    //std::cout << "# of backbones found: " << backbones.size() << std::endl;
}

void solver_t::_testBackbones(
    const std::vector<bool>& inputs, 
    sat_n::Solver& S, ckt_n::index2lit_map_t& lmap,
    std::map<int, int>& backbones)
{
#if 0
    using namespace sat_n;
    using namespace ckt_n;

    assert(inputs.size() == ckt.num_ckt_inputs());

    vec_lit_t assumps;
    for(unsigned i=0; i != inputs.size(); i++) {
        int idx = ckt.ckt_inputs[i]->get_index();
        assumps.push(inputs[i] ? lmap[idx] : ~lmap[idx]);
    }

    assert(assumps.size() == ckt.num_ckt_inputs());
    assumps.growTo(ckt.num_ckt_inputs() + 1);
    assert(assumps.size() == ckt.num_ckt_inputs()+1);

    int last = assumps.size()-1;
    for(unsigned i=0; i != ckt.num_key_inputs(); i++) {
        assumps[last] = ~keys[i];
        //std::cout << "key: " << i << std::endl;
        if(S.solve(assumps) == false) {
            // can't satisfy these I/O combinations with this key value.
            if(sign(keys[i])) {
                backbones[i] = 0;
            } else {
                backbones[i] = 1;
            }
            S.addClause(keys[i]);
        }
    }
#endif
}
