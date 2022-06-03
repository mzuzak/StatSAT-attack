#ifndef _SOLVER_H_DEFINED_
#define _SOLVER_H_DEFINED_

#include <vector>
#include "ckt.h"
#include "sim.h"
#include "dbl.h"
#include "sat.h"
#include "sim.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <list>

// JOHANN
#include <chrono>

struct keyset_value_t
{
    std::vector<unsigned> keys;
    std::vector<int> values;

    keyset_value_t(const std::vector<unsigned>& ks, int vs)
        : keys(ks)
    {
        assert(keys.size() < 32);
        for(unsigned i=0; i != keys.size(); i++) {
            int v = (vs>>i)&1;
            values.push_back(v);
        }
    }
};

typedef std::list<keyset_value_t> keyset_list_t;

class solver_t {
public:
    // types
    enum solver_version_t { SOLVER_V0=0, SOLVER_V1=1, SOLVER_V2=2, SOLVER_V4=4 /* v2 without cube finding. */ };
    static bool is_valid_version(int ver) {
        return SOLVER_V0 == ver; 
    }
    typedef std::map<std::string, int> rmap_t;

    struct slice_t {
        ckt_n::ckt_t& ckt;
        ckt_n::ckt_t& sim;

        std::vector<int> outputs;
        std::vector<int> keys;

        AllSAT::ClauseList cl;
        ckt_n::ckt_t* cktslice;
        ckt_n::ckt_t* simslice;
        ckt_n::ckt_t::node_map_t ckt_nmfwd, ckt_nmrev;
        ckt_n::ckt_t::node_map_t sim_nmfwd, sim_nmrev;

        std::map<int,int> cktm_kfwd, cktm_krev;
        std::map<int,int> cktm_cfwd, cktm_crev;

        slice_t(ckt_n::ckt_t& cktin, ckt_n::ckt_t& simin) 
            : ckt(cktin), sim(simin) 
            , cktslice(NULL), simslice(NULL)
        {}
        ~slice_t() {
            if(cktslice) delete cktslice;
            if(simslice) delete simslice;
        }

        void createCkts();

        ckt_n::node_t* getSliceKey(int i) { 
            assert(i >= 0 && i < (int) cktslice->num_key_inputs());
            return cktslice->key_inputs[i];
        }
        ckt_n::node_t* getSliceInp(int i) {
            assert(i >= 0 && i < (int) cktslice->num_ckt_inputs());
            return cktslice->ckt_inputs[i];
        }

        int mapKeyIndexFwd(int i) {
            assert(i >= 0 && i < (int) ckt.num_key_inputs());
            auto pos = cktm_kfwd.find(i);
            if(pos != cktm_kfwd.end()) { return pos->second; }
            else { return -1; }
        }

        int mapCktIndexFwd(int i) {
            assert(i >= 0 && i < (int) cktslice->num_ckt_inputs());
            auto pos = cktm_cfwd.find(i);
            if(pos != cktm_cfwd.end()) { return pos->second; }
            else { return -1; }
        }

        int mapKeyIndexRev(int i) {
            assert(i >= 0 && i < (int) cktslice->num_key_inputs());
            auto pos = cktm_krev.find(i);
            if(pos != cktm_krev.end()) { return pos->second; }
            else { assert(false); return -1; }
        }

        int mapCktIndexRev(int i) {
            assert(i >= 0 && i < (int) cktslice->num_ckt_inputs());
            auto pos = cktm_crev.find(i);
            if(pos != cktm_crev.end()) { return pos->second; }
            else { assert(false); return -1; }
        }
    };

    struct iovalue_t {
        std::vector<bool> inputs;
        std::vector<bool> outputs;
        std::vector<bool> uncertainty;
    };
    typedef std::vector<iovalue_t> iovalue_vector_t;

private:
    ckt_n::ckt_t& ckt;
    ckt_n::ckt_t& simckt;
    ckt_n::ckt_eval_t sim;
    ckt_n::dblckt_t dbl;

    std::vector<bool> input_values;
    std::vector<bool> output_values;
    std::vector<double> uncert_values; //Ankit

    unsigned long MAX_VERIF_ITER;
    sat_n::Solver S;              // "doubled-ckt" solver.
    AllSAT::ClauseList cl;        // solver with the initial list of clauses.

    ckt_n::index2lit_map_t lmap;  // map for S

    // dbl_keyinput_flags[var(l)] tells us whether l is a key input
    std::vector<bool>           dbl_keyinput_flags;

    std::map<sat_n::Var, std::pair<sat_n::Lit, sat_n::Lit> > keyinput_vars;

    sat_n::Lit l_out; // output literal of dbl.
    std::vector<sat_n::Lit> keyinput_literals_A, keyinput_literals_B;
    std::vector<sat_n::Lit> cktinput_literals;
    std::vector<sat_n::Lit> output_literals_A, output_literals_B;

    std::vector<bool> fixed_keys;
    

    // methods.
    void _sanity_check_model();
    bool _verify_solution_sat();

    // Evaluates the output for the values stored in input_values and then
    // records this in the solver.
    void _record_input_values(std::vector<double> ydiff_avg, bool allow_fork, bool deadlock);
    void _record_sim(
        const std::vector<bool>& input_values, 
        const std::vector<bool>& output_values, 
        std::vector<sat_n::lbool>& values,
        std::vector<bool>& uncert
    );

    //bool _solve_v0(rmap_t& keysFound, bool quiet, int dlimFactor);
    void _testBackbones(
        const std::vector<bool>& inputs, 
        sat_n::Solver& S, ckt_n::index2lit_map_t& lmap,
        std::map<int, int>& backbones);
    void _extractSolution(rmap_t& keysFound);

public:
    // flags and limits.
    int verbose;
    int herk_insert;
    double time_limit;
    struct rusage ru_start;
    double final_avg_ydiff; //Ankit
    double final_max_ydiff;
    // double coverage, HD_output; // Ankit
    bool fork;
    int fork_index; //Ankit
    std::vector<bool> key_found;

    // counters.
    volatile int iter;
    volatile int backbones_count;
    volatile int cube_count;

    iovalue_vector_t iovectors;

    solver_t(ckt_n::ckt_t& ckt, ckt_n::ckt_t& sim, int verbose, int herk_insert);
    // solver_t();
    ~solver_t();

    // find slices to cut the circuit into.
    static void sliceAndDice(
        ckt_n::ckt_t& ckt, 
        ckt_n::ckt_t& sim, 
        std::vector<slice_t*>& slices
    );

    unsigned int bool2value(std::vector<bool> x); // Ankit
    void bool2hex(std::vector<bool> x); //Ankit
    int get_IO_sampling_iter(); //Ankit
    // void check_simckt(); //Ankit

    void feed_iovectors(iovalue_vector_t iovectors, bool skip_last); //Ankit
    void feed_iovector(std::vector<bool> input, std::vector<bool> output, std::vector<bool> uncert); //Ankit
    bool fake_solve(bool show_alert);//Ankit
    void dump_iovectors(int num_vecs); //Ankit
    void dump_iovector(std::vector<bool> inp,std::vector<bool> out,std::vector<bool> uncert); //Ankit
    std::vector<double> to_double(std::vector<bool> key); //Ankit
    void dump_vec_dbl(std::vector<double> x); //Ankit
    bool are_equal(std::vector<bool> x, std::vector<bool> y); //Ankit
    std::vector<double> get_uncert_values(); //Ankit
    bool is_identical(const std::vector<bool> uncert); //Ankit
    void clear_unc_flag(std::vector<bool>& uncert, std::vector<double> ydiff); //Ankit
    void get_distinguishing_keys(std::vector<bool>& key1, std::vector<bool>& key2); //Ankit
    void add_forbidden_key(std::vector<bool> forb_key); // Ankit
    std::vector<double> satis_error_rate(const std::vector<bool> dist_input); //Ankit
    std::vector<double> random_error_rate(const std::vector<bool> dist_input); //Ankit
    bool set_fork(bool force_proceed); // Ankit
    double vec_dbl_diff_avg(const std::vector<double> x, const std::vector<double> y); //Ankit
    double estimate_error_rate(); // Ankit
    double estimate_error_rate_herk();
    double get_norm(std::vector<double> x, std::vector<double> y, int n); // Ankit
    void final_solution(std::vector<bool> key); //Ankit

    void addKnownKeys(std::vector<std::pair<int, int> >& values);
    bool solve(rmap_t& keysFoundMap, bool force_proceed, bool deadlock);
    void blockKey(rmap_t& keysFoundMap);
    bool getNewKey(rmap_t& keysFoundMap);
    void findFixedKeys(std::map<int, int>& backbones);
    bool verify_solution_sim(std::map< std::string, int >& keysFound);


    static void solveSlice(
        slice_t& slice, 
        std::map<int, int>& fixedKeys,
        rmap_t& allKeys 
    );
    int sliceAndSolve( rmap_t& keysFoundMap, int maxKeys, int maxNodes );
};

#endif
