#ifndef CONFIT_BUILD_CLANG_CONTRACT_H
#define CONFIT_BUILD_CLANG_CONTRACT_H

#if !defined(__clang__)
#error "Confit bootstrap requires a provisioned clang compiler"
#endif

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201710L
#error "Confit bootstrap requires C17 or newer"
#endif

#endif /* CONFIT_BUILD_CLANG_CONTRACT_H */
