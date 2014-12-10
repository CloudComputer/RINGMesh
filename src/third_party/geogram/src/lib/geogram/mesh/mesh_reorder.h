/*
 *  Copyright (c) 2012-2014, Bruno Levy
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
 *     Bruno.Levy@inria.fr
 *     http://www.loria.fr/~levy
 *
 *     ALICE Project
 *     LORIA, INRIA Lorraine, 
 *     Campus Scientifique, BP 239
 *     54506 VANDOEUVRE LES NANCY CEDEX 
 *     FRANCE
 *
 */

#ifndef __GEOGRAM_MESH_MESH_REORDER__
#define __GEOGRAM_MESH_MESH_REORDER__

#include <geogram/basic/common.h>
#include <geogram/basic/numeric.h>
#include <geogram/basic/memory.h>

/**
 * \file geogram/mesh/mesh_reorder.h
 * \brief Reorders the elements in a mesh to improve data locality
 */

namespace {
    class Delaunay3dThread;
}

namespace GEO {
    class Mesh;

    /**
     * \brief Strategy for spatial sorting.
     */
    enum MeshOrder {
        /**
         * Hilbert ordering improves data locality and
         * has a continuous mapping between indices and space.
         */
        MESH_ORDER_HILBERT,
        /**
         * Morton ordering improves data locality and is
         * a bit simpler than Hilbert ordering.
         */
        MESH_ORDER_MORTON
    };

    /**
     * \brief Reorders all the elements of a mesh.
     * \details It is used for both improving data locality
     *  and for implementing mesh partitioning.
     * \param[in] M the mesh to reorder
     * \param[in] order the reordering scheme
     */
    void GEOGRAM_API mesh_reorder(
        Mesh& M, MeshOrder order = MESH_ORDER_HILBERT
    );

    /**
     * \brief Applies the specified permutation to the vertices
     *  of a mesh.
     * \param[in] M the mesh to be permuted.
     * \param[in,out] permutation the i-th vertex is
     *  replaced by the permutation[i]-th vertex.
     * \note: permutation is modified by the function.
     */
    void GEOGRAM_API mesh_permute_vertices(
        Mesh& M, vector<index_t>& permutation
    );

    /**
     * \brief Applies the specified permutation to the facets
     *  of a mesh.
     * \param[in] M the mesh to be permute. It needs to be triangulated.
     * \param[in,out] permutation the i-th facet is replaced by
     *   the permutation[i]-th facet.
     * \note: permutation is modified by the function.
     */
    void GEOGRAM_API mesh_permute_facets(
        Mesh& M, vector<index_t>& permutation
    );

    /**
     * \brief Applies the specified permutation to the tetrahedra
     *  of a mesh.
     * \param[in] M the mesh to be permute.
     * \param[in,out] permutation the i-th tetrahedron is replaced by
     *   the permutation[i]-th tetrahedron.
     * \note: permutation is modified by the function.
     */
    void GEOGRAM_API mesh_permute_tets(
        Mesh& M, vector<index_t>& permutation
    );

    /**
     * \brief Computes the Hilbert order for a set of 3D points.
     * \details The implementation is inspired by:
     *  - Christophe Delage and Olivier Devillers. Spatial Sorting. 
     *   In CGAL User and Reference Manual. CGAL Editorial Board, 
     *   3.9 edition, 2011
     * \param[in] nb_vertices number of vertices to sort
     * \param[in] vertices pointer to the coordinates of the vertices
     * \param[out] sorted_indices a vector of element indices to
     *  be sorted spatially
     * \param[in] stride number of doubles between two consecutive vertices
     */
    void GEOGRAM_API compute_Hilbert_order(
        index_t nb_vertices, const double* vertices,
        vector<index_t>& sorted_indices,
        index_t stride = 3
    );

    /**
     * \brief Computes the BRIO order for a set of 3D points.
     * \details It is used to accelerate incremental insertion
     *  in Delaunay triangulation. See the following reference:
     *  -Incremental constructions con brio. Nina Amenta, Sunghee Choi,
     *   Gunter Rote, Simposium on Computational Geometry conf. proc.,
     *   2003
     * \param[in] nb_vertices number of vertices to sort
     * \param[in] vertices pointer to the coordinates of the vertices
     * \param[out] sorted_indices a vector of element indices to
     *  be sorted spatially
     * \param[in] stride number of doubles between two consecutive vertices
     * \param[in] threshold minimum size of interval to be sorted
     * \param[in] ratio splitting ratio between current interval and
     *  the rest to be sorted
     * \param[out] levels if non-nil, indices that correspond to level l are
     *   in the range levels[l] (included) ... levels[l+1] (excluded)
     */
    void GEOGRAM_API compute_BRIO_order(
        index_t nb_vertices, const double* vertices,
        vector<index_t>& sorted_indices,
        index_t stride = 3,
        index_t threshold = 64,
        double ratio = 0.125,
        vector<index_t>* levels = nil
    );
}

#endif

