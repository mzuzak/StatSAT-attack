#include <iostream>
#include <math.h>
#include <vector>

using namespace std;

void ifft(std::vector<float>& Xf_real, std::vector<float>& Xf_imag, std::vector<float>& xr_real, std::vector<float>& xr_imag);
void fft(std::vector<double>& x, std::vector<double>& Xf_real, std::vector<double>& Xf_imag);
void disp_binary(unsigned short int x_num, bool x_sign);
float adder(float a, float b);
float bin2float(unsigned short int x_num, bool x_sign);
std::vector<bool> print_vec_bool(std::bitset<8> x);