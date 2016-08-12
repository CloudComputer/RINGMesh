/*
 *  Copyright (c) 2004-2010, Bruno Levy
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *  * Neither the name of the ALICE Project-Team nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  If you modify this software, you should include a notice giving the
 *  name of the person performing the modification, the date of modification,
 *  and the reason for such modification.
 *
 *  Contact: Bruno Levy
 *
 *     levy@loria.fr
 *
 *     ALICE Project
 *     LORIA, INRIA Lorraine, 
 *     Campus Scientifique, BP 239
 *     54506 VANDOEUVRE LES NANCY CEDEX 
 *     FRANCE
 *
 */

#include "nl_preconditioners.h"
#include "nl_blas.h"
#include "nl_matrix.h"
#include "nl_context.h"

/******************************************************************************/
/* preconditioners */

/* Utilities for preconditioners */

void nlMultDiagonal(NLdouble* xy, NLdouble omega) {
    NLuint N = nlCurrentContext->n ;
    NLuint i ;
    NLdouble* diag = nlCurrentContext->M.diag ;
    for(i=0; i<N; i++) {
        xy[i] *= (diag[i] / omega) ;
    }
    nlCurrentContext->flops += (NLulong)(N);
}

void nlMultDiagonalInverse(NLdouble* xy, NLdouble omega) {
    NLuint N = nlCurrentContext->n ;
    NLuint i ;
    NLdouble* diag = nlCurrentContext->M.diag ;
    for(i=0; i<N; i++) {
        xy[i] *= ((diag[i] != 0) ? (omega / diag[i]) : omega) ;
    }
    nlCurrentContext->flops += (NLulong)(N);    
}

void nlMultLowerInverse(const NLdouble* x, NLdouble* y, double omega) {
    NLSparseMatrix* A = &(nlCurrentContext->M) ;
    NLuint n       = A->n ;
    NLdouble* diag = A->diag ;
    NLuint i ;
    NLuint ij ;
    NLCoeff* c = NULL ;
    NLdouble S ;

    nl_assert(A->storage & NL_MATRIX_STORE_SYMMETRIC) ;
    nl_assert(A->storage & NL_MATRIX_STORE_ROWS) ;

    for(i=0; i<n; i++) {
        NLRowColumn*  Ri = &(A->row[i]) ;       
        S = 0 ;
        for(ij=0; ij < Ri->size; ij++) {
            c = &(Ri->coeff[ij]) ;
            nl_parano_assert(c->index <= i) ; 
            if(c->index != i) {
                S += c->value * y[c->index] ; 
            }
        }
        nlCurrentContext->flops += (NLulong)(2*Ri->size);                    
        y[i] = (x[i] - S) * omega / diag[i] ;
    }
    nlCurrentContext->flops += (NLulong)(n*3);                
}

void nlMultUpperInverse(const NLdouble* x, NLdouble* y, NLdouble omega) {
    NLSparseMatrix* A = &(nlCurrentContext->M) ;
    NLuint n       = A->n ;
    NLdouble* diag = A->diag ;
    NLint i ;
    NLuint ij ;
    NLCoeff* c = NULL ;
    NLdouble S ;

    nl_assert(A->storage & NL_MATRIX_STORE_SYMMETRIC) ;
    nl_assert(A->storage & NL_MATRIX_STORE_COLUMNS) ;

    for(i=(NLint)(n-1); i>=0; i--) {
        NLRowColumn*  Ci = &(A->column[i]) ;       
        S = 0 ;
        for(ij=0; ij < Ci->size; ij++) {
            c = &(Ci->coeff[ij]) ;
            nl_parano_assert(c->index >= i) ; 
            if((NLint)(c->index) != i) {
                S += c->value * y[c->index] ; 
            }
        }
        nlCurrentContext->flops += (NLulong)(2*Ci->size);                    
        y[i] = (x[i] - S) * omega / diag[i] ;
    }
    nlCurrentContext->flops += (NLulong)(n*3);                
}


void nlPreconditioner_Jacobi(const NLdouble* x, NLdouble* y) {
    if(nlCurrentContext->M.storage & NL_MATRIX_STORE_DIAG_INV) {
        NLuint i;
        for(i=0; i<nlCurrentContext->M.diag_size; ++i) {
            y[i] = x[i]*nlCurrentContext->M.diag_inv[i];
        }
        nlCurrentContext->flops += (NLulong)(nlCurrentContext->M.diag_size*3);                        
    } else {
        NLint N = (NLint)(nlCurrentContext->n) ;
        dcopy(N, x, 1, y, 1) ;
        nlMultDiagonalInverse(y, 1.0) ;
    }
}


static double* nlPreconditioner_SSOR_work = NULL;
static NLuint nlPreconditioner_SSOR_work_size = 0;

static void nlPreconditioner_SSOR_terminate(void) {
    NL_DELETE_ARRAY(nlPreconditioner_SSOR_work);
}

void nlPreconditioner_SSOR(const NLdouble* x, NLdouble* y) {
    NLdouble omega = nlCurrentContext->omega ;
    NLuint n = nlCurrentContext->n ;
    static NLboolean init = NL_FALSE;
    if(!init) {
        atexit(nlPreconditioner_SSOR_terminate);
        init = NL_TRUE;
    }
    if(n != nlPreconditioner_SSOR_work_size) {
        nlPreconditioner_SSOR_work = NL_RENEW_ARRAY(
            NLdouble, nlPreconditioner_SSOR_work, n
        ) ;
        nlPreconditioner_SSOR_work_size = n ;
    }
    
    nlMultLowerInverse(
        x, nlPreconditioner_SSOR_work, omega
    );
    nlMultDiagonal(
        nlPreconditioner_SSOR_work, omega
    );
    nlMultUpperInverse(
        nlPreconditioner_SSOR_work, y, omega
    );

    dscal((NLint)n, 2.0 - omega, y, 1) ;

    nlCurrentContext->flops += (NLulong)(n);    
}

