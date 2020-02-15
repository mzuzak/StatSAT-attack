#include <iostream>
#include <math.h>
#include <vector>
#include <bitset>

#include "ckt.h"

using namespace std;

class fft_and_ifft
{
public:
	ckt_n::ckt_eval_t& simul;

	fft_and_ifft(ckt_n::ckt_eval_t& simulator);

	~fft_and_ifft();

	int main_fft_ifft();
	void ifft(std::vector<float>& Xf_real, std::vector<float>& Xf_imag, std::vector<float>& xr_real, std::vector<float>& xr_imag);
	void fft(std::vector<float>& x, std::vector<float>& Xf_real, std::vector<float>& Xf_imag);
	void disp_binary(unsigned short int x_num, bool x_sign);
	float adder(float a, float b);
	float bin2float(unsigned short int x_num, bool x_sign);
	std::vector<bool> get_vec_bool(std::bitset<8> x);
	unsigned short int vec_bool_to_int(std::vector<bool> num_lower, std::vector<bool> num_upper);
};
