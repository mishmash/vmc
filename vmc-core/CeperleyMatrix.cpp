#include "CeperleyMatrix.hpp"

template<>
const std::complex<double> CeperleyMatrix<std::complex<double> >::ceperley_determinant_upper_cutoff = 1e50;
template<>
const std::complex<long double> CeperleyMatrix<std::complex<long double> >::ceperley_determinant_upper_cutoff = 1e50;

template<>
const std::complex<double> CeperleyMatrix<std::complex<double> >::ceperley_determinant_lower_cutoff = 1e-50;
template<>
const std::complex<long double> CeperleyMatrix<std::complex<long double> >::ceperley_determinant_lower_cutoff = 1e-50;

// if this is set too low, we may not be able to reliably recognize singular matrices
template<>
const std::complex<double> CeperleyMatrix<std::complex<double> >::ceperley_determinant_safe_lower_cutoff = 1e-6;
template<>
const std::complex<long double> CeperleyMatrix<std::complex<long double> >::ceperley_determinant_safe_lower_cutoff = 1e-6;
