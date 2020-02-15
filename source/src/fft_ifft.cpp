// Code to compute the FFT and then IFFT

#include <iostream>
#include <math.h>
#include <vector>
#include <assert.h>
#include <limits.h>
#include <random>
#include <bitset>
#include <string>
#include "sim.h"
#include "fft_ifft.h"

#define pi M_PI
#define BASE 256.0

using namespace std;

fft_and_ifft::fft_and_ifft(ckt_n::ckt_eval_t& simulator)
	:simul(simulator)
{	
}

fft_and_ifft::~fft_and_ifft()
{}

void fft_and_ifft::fft(std::vector<float>& x, std::vector<float>& Xf_real, std::vector<float>& Xf_imag)
{
	assert(x.size()==Xf_real.size());
	int N = x.size();
	for (int k = 0; k < N; ++k)
	{
		for(int n=0; n<N; n++)
		{
			float r = std::round(x[n]*cos(2*pi*k*n/N)*BASE)/BASE;
			Xf_real[k] = adder(Xf_real[k],r); //Xf_real[k] += r;
			float i = -std::round(x[n]*sin(2*pi*k*n/N)*BASE)/BASE;
			Xf_imag[k] = adder(Xf_imag[k],i); // Xf_imag[k] -= i;

			// int dummy; std::cout<<"Enter any number "; std::cin>>dummy;
		} 
	}
}

void fft_and_ifft::ifft(std::vector<float>& Xf_real, std::vector<float>& Xf_imag, std::vector<float>& xr_real, std::vector<float>& xr_imag)
{
	int N = Xf_real.size();
	for(int n=0; n<N; n++)
	{
		for (int k = 0; k < N; ++k)
		{
			float ra = std::round(Xf_real[k]*cos(2*pi*k*n/N)*BASE)/BASE;
			float rb = std::round(Xf_imag[k]*sin(2*pi*k*n/N)*BASE)/BASE ;
			xr_real[n] = adder(xr_real[n],adder(ra,-rb)); // xr_real[n] +=  ra - rb;
			float ia = std::round(Xf_real[k]*sin(2*pi*k*n/N)*BASE)/BASE;
			float ib = std::round(Xf_imag[k]*cos(2*pi*k*n/N)*BASE)/BASE ;
			xr_imag[n] = adder(xr_imag[n],adder(ia,ib));  // xr_imag[n] +=  ia + ib;
		}
		xr_real[n] /= N;
		xr_imag[n] /= N;
	}
}

// add 2 float numbers by converting to binary representation and 
// then adding chunks of the binary values using circuit c3540
float fft_and_ifft::adder(float a, float b)
{
	using namespace ckt_n;
	bool a_sign = (a<0); bool b_sign = (b<0); bool c_sign;
	unsigned short int a_num, b_num, c_num;
	if(a_sign) //negative
		a_num = (std::floor(-a*BASE)); 
	else // positive
		a_num = (std::floor(a*BASE)); 

	if(b_sign) //negative
		b_num = (std::floor(-b*BASE)); 
	else // positive
		b_num = (std::floor(b*BASE)); 

	bool Cout2 = false;
	if(a_sign == b_sign)
	{
		c_sign = a_sign; 
		std::bitset<8> a_num_lower(a_num%int(BASE));
		std::bitset<8> a_num_upper(a_num/int(BASE));
		std::bitset<8> b_num_lower(b_num%int(BASE));
		std::bitset<8> b_num_upper(b_num/int(BASE));

		// std::cout<<"a_num = "<<a_num_upper<<" "<<a_num_lower<<"\n";
		// std::cout<<"b_num = "<<b_num_upper<<" "<<b_num_lower<<"\n";

		std::vector<bool> sum_lower(8), sum_upper(8);
		bool Cout1 = simul.add_c3540(get_vec_bool(a_num_lower),get_vec_bool(b_num_lower),false,sum_lower,true);
		// consider error for lower 8 bits
		Cout2 = simul.add_c3540(get_vec_bool(a_num_upper),get_vec_bool(b_num_upper),Cout1,sum_upper,false);
		// don't consider error for upper 8 bits

		if(Cout2==true)
		{
			std::cout<<"Sum = "<<Cout2<<" ";
			simul.print_bool_vec(sum_upper); // In sum_lower and _upper [7] is the MSB
			simul.print_bool_vec(sum_lower); // recall that this prints from MSB to LSB
			std::cout<<"\n";
		}	
		
		c_num = vec_bool_to_int(sum_lower,sum_upper); // c_num = a_num + b_num;
	}
	else
	{
		if(a_num>b_num)
		{
			c_sign = a_sign; c_num = a_num - b_num;
		}
		else if(a_num<b_num)
		{
			c_sign = b_sign; c_num = b_num - a_num;
		}
		else
		{
			c_sign = false;	c_num = 0;
		}
	}
	// std::cout<<"a = "; disp_binary(a_num,a_sign); //std::cout<<" -> "<<a<<
	// std::cout<<"b = "; disp_binary(b_num,b_sign); //<<b<<" -> "
	// std::cout<<"c = "; disp_binary(c_num,c_sign);
	float c = bin2float(c_num,c_sign);
	if(Cout2 == true)
	{
		std::cout<<"c = "; disp_binary(c_num,c_sign);
	}
	if(c==(a+b))
		0; //std::cout<<"Correct";
	else
	{
		std::cout<<"NOT Correct: c = "<<c<<" whereas a+b = "<<(a+b)<<"\n";
	}
	// std::cout<<"\n";

	// int dummy; std::cout<<"Enter any number "; std::cin>>dummy;
	
	return c;
}


// convert a bitset of size 8 to a vector of bool
std::vector<bool> fft_and_ifft::get_vec_bool(std::bitset<8> x)
{
	std::vector<bool> y(8); // y[7] is MSB and y[0] is LSB
	for (int i = 0; i < x.size(); ++i)
	{
		y[i] = (bool)x[i];
		// std::cout<<y[i];
	}
	return y;
}

void fft_and_ifft::disp_binary(unsigned short int x_num, bool x_sign)
{
	std::bitset<16> binary_num(x_num);
	std::cout<<x_sign<<" "<<binary_num<<"\n";
}

float fft_and_ifft::bin2float(unsigned short int x_num, bool x_sign)
{
	float x = x_num/BASE;
	if(x_sign)
		x = -x;
	return x;
}

unsigned short int fft_and_ifft::vec_bool_to_int(std::vector<bool> num_lower, std::vector<bool> num_upper)
{
	int base=1;
	unsigned short int num = 0;
	for(int i = 0; i<num_lower.size();i++)
	{
		num += base*num_lower[i];
		base *= 2;
	}
	for(int i = 0; i<num_upper.size();i++)
	{
		num += base*num_upper[i];
		base *= 2;
	}
	return num;
}

int fft_and_ifft::main_fft_ifft()
{
	using namespace std;
	std::cout.precision(4);

	// float a = atof(argv[1]), b = atof(argv[2]);
	// std::cout<<"a = "<<a<<", b = "<<b<<"\n";
	// float c = adder(a,b);
	// std::cout<<"c = "<<c<<" and a+b = "<<(a+b)<<"\n";

	int N = 8;

	std::vector<float> x(N);
	std::cout<<"\nx = ";
	for (int i = 0; i < N; ++i)
	{
		x[i] = float(rand())/RAND_MAX;
		x[i] = float(std::round(x[i]*BASE))/BASE;
		std::cout<<x[i]<<", ";
	}
	std::cout<<"\n";
	
	std::cout<<"\n";
	std::vector<float> Xf_real(N,0);
	std::vector<float> Xf_imag(N,0);
	fft(x,Xf_real,Xf_imag);
	std::cout<<"FFT Real part: ";
	for (int i = 0; i < N; ++i)
		std::cout<<Xf_real[i]<<", ";
	std::cout<<"\nFFT Imag part: ";
	for (int i = 0; i < N; ++i)
		std::cout<<Xf_imag[i]<<", ";

	std::cout<<"\n";

	std::vector<float> xr_real(N,0);
	std::vector<float> xr_imag(N,0);

	ifft(Xf_real,Xf_imag,xr_real,xr_imag);
	std::cout<<"IFFT Real part: ";
	for (int i = 0; i < N; ++i)
		std::cout<<xr_real[i]<<", ";
	std::cout<<"\nIFFT Imag part: ";
	for (int i = 0; i < N; ++i)
		std::cout<<xr_imag[i]<<", ";

	std::cout<<"\n";


	return 0;

}
