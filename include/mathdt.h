#ifndef MATHDT_H
#define MATHDT_H

#include <cmath>
#include <complex>

#ifdef __cplusplus
extern "C" {
#endif

// Derivative table
#define SINX_DX(a, x) (a * cos(a * x))
#define COSX_DX(a, x) (-a * sin(a * x))
#define TANX_DX(a, x) (a * (1 / (cos(a * x) * cos(a * x))))
#define ARCSINX_DX(a, x) (a * (1 / sqrt(1 - (a * x) * (a * x))))
#define ARCCOSX_DX(a, x) (-a * (1 / sqrt(1 - (a * x) * (a * x))))
#define ARCTANX_DX(a, x) (a * (1 / (1 + (a * x) * (a * x))))
#define SINH_DX(a, x) (a * cosh(a * x))
#define COSH_DX(a, x) (a * sinh(a * x))
#define TANH_DX(a, x) (a * (1 / (cosh(a * x) * cosh(a * x))))
#define ARCSINH_DX(a, x) (a * (1 / sqrt(1 + (a * x) * (a * x))))
#define ARCCOSH_DX(a, x) (a * (1 / sqrt((a * x) * (a * x) - 1)))
#define ARCTANH_DX(a, x) (a * (1 / (1 - (a * x) * (a * x))))
#define EXP_DX(a, x) (a * exp(a * x))
#define LN_DX(a, x) (a * (1 / x))
#define LOG10_DX(a, x) (a * (1 / (x * log(10))))
#define LOG2_DX(a, x) (a * (1 / (x * log(2))))
#define SQRT_DX(a, x) (a * (1 / (2 * sqrt(x))))
#define CBRT_DX(a, x) (a * (1 / (3 * cbrt(x * x))))
#define POW_DX(a, x, n) (a * n * pow(x, n - 1))
#define ABS_DX(a, x) (a * (x >= 0 ? 1 : -1))
#define SIGN_DX(a, x) (a * (x > 0 ? 1 : (x < 0 ? -1 : 0)))
#define CEIL_DX(a, x) (0)
#define FLOOR_DX(a, x) (0)
#define ROUND_DX(a, x) (0)

// Primitive table
#define SINX_PRIM(a, x) (-cos(a * x) / a)
#define COSX_PRIM(a, x) (sin(a * x) / a)
#define TANX_PRIM(a, x) (-log(cos(a * x)) / a)
#define ARCSINX_PRIM(a, x) (asin(a * x) / a)
#define ARCCOSX_PRIM(a, x) (acos(a * x) / a)
#define ARCTANX_PRIM(a, x) (atan(a * x) / a)
#define SINH_PRIM(a, x) (cosh(a * x) / a)
#define COSH_PRIM(a, x) (sinh(a * x) / a)
#define TANH_PRIM(a, x) (log(cosh(a * x)) / a)
#define ARCSINH_PRIM(a, x) (asinh(a * x) / a)
#define ARCCOSH_PRIM(a, x) (acosh(a * x) / a)
#define ARCTANH_PRIM(a, x) (atanh(a * x) / a)
#define EXP_PRIM(a, x) (exp(a * x) / a)
#define LN_PRIM(a, x) (log(x) / a)
#define LOG10_PRIM(a, x) (log10(x) / a)
#define LOG2_PRIM(a, x) (log2(x) / a)
#define SQRT_PRIM(a, x) (sqrt(x) / a)
#define CBRT_PRIM(a, x) (cbrt(x) / a)
#define POW_PRIM(a, x, n) (pow(x, n + 1)/(a * n + 1))
#define ABS_PRIM(a, x) (abs(x)/a)
#define SIGN_PRIM(a, x) (x > 0 ? 1 : (x < 0 ? -1 : 0))
#define CEIL_PRIM(a, x) (ceil(x)/a)
#define FLOOR_PRIM(a, x) (floor(x)/a)
#define ROUND_PRIM(a, x) (round(x)/a)

//Electro-statique
#define COULOMB (1/(4*M_PI*8.854187817e-12)) // Coulomb's constant in N·m²/C²

#ifdef __cplusplus
}
#endif

#endif /* MATHDT_H */
