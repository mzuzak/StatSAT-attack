// Code to compute the FFT and then IFFT

#include <iostream>
#include <math.h>
#include <vector>
#include <assert.h>
#include <limits.h>
#include <random>
#include <bitset>
#include <string>
#include "fft_ifft.h"

#define pi M_PI
#define BASE 256.0

using namespace std;

void fft(std::vector<float>& x, std::vector<float>& Xf_real, std::vector<float>& Xf_imag)
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

void ifft(std::vector<float>& Xf_real, std::vector<float>& Xf_imag, std::vector<float>& xr_real, std::vector<float>& xr_imag)
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

float adder(float a, float b)
{
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

	std::cout<<"a = "; disp_binary(a_num,a_sign); //std::cout<<" -> "<<a<<
	std::cout<<"b = "; disp_binary(b_num,b_sign); //<<b<<" -> "

	
	// std::vector<bool> vec_a_lower = a_num_lower;
	// std::vector<bool> vec_b_upper = b;

	std::cout<<"vec a lower = "; print_vec_bool(a_num_lower); std::cout<<"\n";


	int dummy; std::cout<<"Enter any number "; std::cin>>dummy;


	if(a_sign == b_sign)
	{
		c_sign = a_sign; 
		c_num = a_num + b_num;	
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
	// if(c==(a+b))
	// 	std::cout<<"Correct";
	// else
	// 	std::cout<<"NOT Correct";
	// std::cout<<"\n";
	
	return c;
}

// bool adder_8bit(std::bitset<8> a, std::bitset<8> b, bool Cin, std::bitset<8> sum)
// {
// 	std::vector<bool> vec_a = a;
// 	std::vector<bool> vec_b = b;
// }

std::vector<bool> print_vec_bool(std::bitset<8> x)
{
	std::vector<bool> y(8); // 
	for (int i = 0; i < x.size(); ++i)
	{
		y[i] = (bool)x[i];
		std::cout<<y[i];
	}
	return y;
}

void disp_binary(unsigned short int x_num, bool x_sign)
{
	std::bitset<16> binary_num(x_num);
	std::cout<<x_sign<<" "<<binary_num<<"\n";
}

float bin2float(unsigned short int x_num, bool x_sign)
{
	float x = x_num/BASE;
	if(x_sign)
		x = -x;
	return x;
}

int main(int argc, char* argv[])
{
	using namespace std;
	std::cout.precision(4);

	// float a = atof(argv[1]), b = atof(argv[2]);
	// std::cout<<"a = "<<a<<", b = "<<b<<"\n";
	// float c = adder(a,b);
	// std::cout<<"c = "<<c<<" and a+b = "<<(a+b)<<"\n";

	int N = 12;

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
