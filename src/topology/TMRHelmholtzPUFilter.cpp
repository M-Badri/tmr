/*
  This file is part of the package TMR for adaptive mesh refinement.

  Copyright (C) 2015 Georgia Tech Research Corporation.
  Additional copyright (C) 2015 Graeme Kennedy.
  All rights reserved.

  TMR is licensed under the Apache License, Version 2.0 (the "License");
  you may not use this software except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "TMRHelmholtzPUFilter.h"

#include "TACSToFH5.h"
#include "TMRHelmholtzModel.h"
#include "TMRMatrixCreator.h"
#include "TMRMatrixFilterModel.h"
#include "TMR_TACSCreator.h"

/*
  Find the boundary faces and set them as  and set them
*/
void computeQuadtreeBoundaryNormals(TMRQuadForest *filter, TACSBVec *Xpts,
                                    TACSBVec *normals) {
  // Get the block -> face information and the face -> block info.
  // This will be used to determine which faces lie on the boundaries
  // of the domain
  const int *face_edge_conn;
  filter->getConnectivity(NULL, NULL, NULL, NULL, &face_edge_conn);

  const int *edge_face_ptr;
  filter->getInverseConnectivity(NULL, NULL, NULL, &edge_face_ptr);

  // Get the array of octants
  TMRQuadrantArray *quadrants;
  int nelems;
  TMRQuadrant *quads;
  filter->getQuadrants(&quadrants);
  quadrants->getArray(&quads, &nelems);

  // Get the connectivity
  const int *conn;
  filter->getNodeConn(&conn);

  // Get the mesh order
  const double *knots;
  const int mesh_order = filter->getInterpKnots(&knots);
  const int nodes_per_elem = mesh_order * mesh_order;

  // Set the maximum length of any of the block sides
  const int32_t hmax = 1 << TMR_MAX_LEVEL;

  // Allocate space for the node locations and shape functions
  double *N = new double[mesh_order * mesh_order];
  double *Na = new double[mesh_order * mesh_order];
  double *Nb = new double[mesh_order * mesh_order];
  TacsScalar *X = new TacsScalar[3 * mesh_order * mesh_order];

  // Get the mesh coordinates from the filter
  for (int i = 0; i < nelems; i++) {
    // Compute the side-length of this element
    const int32_t h = 1 << (TMR_MAX_LEVEL - quads[i].level);

    // Get the block id
    int face = quads[i].face;

    // Check if the octree face lies on a boundary or not
    int quadtree_edge_boundary[4];
    for (int k = 0; k < 4; k++) {
      int edge = face_edge_conn[4 * face + k];
      int nfaces = edge_face_ptr[edge + 1] - edge_face_ptr[edge];
      quadtree_edge_boundary[k] = (nfaces == 1);
    }

    // Evaluate whether the element is actually on a face
    quadtree_edge_boundary[0] = quadtree_edge_boundary[0] && (quads[i].x == 0);
    quadtree_edge_boundary[1] =
        quadtree_edge_boundary[1] && (quads[i].x + h == hmax);
    quadtree_edge_boundary[2] = quadtree_edge_boundary[2] && (quads[i].y == 0);
    quadtree_edge_boundary[3] =
        quadtree_edge_boundary[3] && (quads[i].y + h == hmax);

    // Get the local connectivity
    const int *c = &conn[nodes_per_elem * i];
    Xpts->getValues(nodes_per_elem, c, X);

    for (int edge = 0; edge < 4; edge++) {
      if (quadtree_edge_boundary[edge]) {
        double pt0[2], d1[2];
        pt0[0] = pt0[1] = 0.0;
        d1[0] = d1[1] = 0.0;

        if (edge < 2) {
          pt0[0] = -1.0 + 2.0 * (edge % 2);
          d1[1] = -1.0 + 2.0 * (edge % 2);
        } else {
          d1[0] = 1.0 - 2.0 * (edge % 2);
          pt0[1] = -1.0 + 2.0 * (edge % 2);
        }

        for (int n = 0; n < mesh_order; n++) {
          // Find the node number based on the edge index
          int node = 0;
          if (edge < 2) {
            node = (edge % 2) * (mesh_order - 1) + n * mesh_order;
          } else {
            node = n + (edge % 2) * (mesh_order - 1) * mesh_order;
          }

          // Check that this is an independent node
          if (c[node] >= 0) {
            // Compute the shape functions/derivatives
            double pt[3];
            pt[0] = pt0[0] + knots[n] * d1[0];
            pt[1] = pt0[1] + knots[n] * d1[1];
            filter->evalInterp(pt, N, Na, Nb);

            // Compute Xa and Xb
            TacsScalar Xa[3];
            Xa[0] = Xa[1] = Xa[2] = 0.0;
            for (int k = 0; k < mesh_order * mesh_order; k++) {
              double na = Na[k] * d1[0] + Nb[k] * d1[1];
              Xa[0] += X[3 * k] * na;
              Xa[1] += X[3 * k + 1] * na;
              Xa[2] += X[3 * k + 2] * na;
            }

            TacsScalar Xb[3] = {0.0, 0.0, 1.0};

            // Compute the normal
            TacsScalar normal[3];
            normal[0] = Xa[1] * Xb[2] - Xa[2] * Xb[1];
            normal[1] = Xa[2] * Xb[0] - Xa[0] * Xb[2];
            normal[2] = Xa[0] * Xb[1] - Xa[1] * Xb[0];

            TacsScalar invnorm =
                1.0 / sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                           normal[2] * normal[2]);
            normal[0] *= invnorm;
            normal[1] *= invnorm;
            normal[2] *= invnorm;

            normals->setValues(1, &c[node], normal, TACS_ADD_VALUES);
          }
        }
      }
    }
  }

  // Add contributions from all procs
  normals->beginSetValues(TACS_ADD_VALUES);
  normals->endSetValues(TACS_ADD_VALUES);

  // Free values
  delete[] X;
  delete[] N;
  delete[] Na;
  delete[] Nb;
}

/*
  Find the boundary faces and set them as  and set them
*/
void computeOctreeBoundaryNormals(TMROctForest *filter, TACSBVec *Xpts,
                                  TACSBVec *normals) {
  // Get the block -> face information and the face -> block info.
  // This will be used to determine which faces lie on the boundaries
  // of the domain
  const int *block_face_conn;
  filter->getConnectivity(NULL, NULL, NULL, NULL, NULL, &block_face_conn, NULL,
                          NULL);

  const int *face_block_ptr;
  filter->getInverseConnectivity(NULL, NULL, NULL, NULL, NULL, &face_block_ptr);

  // Get the array of octants
  TMROctantArray *octants;
  int nelems;
  TMROctant *octs;
  filter->getOctants(&octants);
  octants->getArray(&octs, &nelems);

  // Get the connectivity
  const int *conn;
  filter->getNodeConn(&conn);

  // Get the mesh order
  const double *knots;
  const int mesh_order = filter->getInterpKnots(&knots);
  const int nodes_per_elem = mesh_order * mesh_order * mesh_order;

  // Set the maximum length of any of the block sides
  const int32_t hmax = 1 << TMR_MAX_LEVEL;

  // Allocate space for the node locations and shape functions
  double *N = new double[mesh_order * mesh_order * mesh_order];
  double *Na = new double[mesh_order * mesh_order * mesh_order];
  double *Nb = new double[mesh_order * mesh_order * mesh_order];
  double *Nc = new double[mesh_order * mesh_order * mesh_order];
  TacsScalar *X = new TacsScalar[3 * mesh_order * mesh_order * mesh_order];

  // Get the mesh coordinates from the filter
  for (int i = 0; i < nelems; i++) {
    // Compute the side-length of this element
    const int32_t h = 1 << (TMR_MAX_LEVEL - octs[i].level);

    // Get the block id
    int block = octs[i].block;

    // Check if the octree face lies on a boundary or not
    int octree_face_boundary[6];
    for (int k = 0; k < 6; k++) {
      int face = block_face_conn[6 * block + k];
      int nblocks = face_block_ptr[face + 1] - face_block_ptr[face];
      octree_face_boundary[k] = (nblocks == 1);
    }

    // Evaluate whether the element is actually on a face
    octree_face_boundary[0] = octree_face_boundary[0] && (octs[i].x == 0);
    octree_face_boundary[1] =
        octree_face_boundary[1] && (octs[i].x + h == hmax);
    octree_face_boundary[2] = octree_face_boundary[2] && (octs[i].y == 0);
    octree_face_boundary[3] =
        octree_face_boundary[3] && (octs[i].y + h == hmax);
    octree_face_boundary[4] = octree_face_boundary[4] && (octs[i].z == 0);
    octree_face_boundary[5] =
        octree_face_boundary[5] && (octs[i].z + h == hmax);

    // Get the local connectivity
    const int *c = &conn[nodes_per_elem * i];
    Xpts->getValues(nodes_per_elem, c, X);

    for (int surface = 0; surface < 6; surface++) {
      if (octree_face_boundary[surface]) {
        double pt0[3], d1[3], d2[3];
        pt0[0] = pt0[1] = pt0[2] = 0.0;
        d1[0] = d1[1] = d1[2] = 0.0;
        d2[0] = d2[1] = d2[2] = 0.0;

        if (surface < 2) {
          pt0[0] = -1.0 + 2.0 * (surface % 2);
          d1[1] = 1.0;
          d2[2] = 1.0;
        } else if (surface < 4) {
          pt0[1] = -1.0 + 2.0 * (surface % 2);
          d1[2] = 1.0;
          d2[0] = 1.0;
        } else {
          pt0[2] = -1.0 + 2.0 * (surface % 2);
          d1[0] = 1.0;
          d2[1] = 1.0;
        }

        for (int m = 0; m < mesh_order; m++) {
          for (int n = 0; n < mesh_order; n++) {
            // Find the node number based on the surface index
            int node = 0;
            if (surface < 2) {
              node = (surface % 2) * (mesh_order - 1) + n * mesh_order +
                     m * mesh_order * mesh_order;
            } else if (surface < 4) {
              node = m + (surface % 2) * (mesh_order - 1) * mesh_order +
                     n * mesh_order * mesh_order;
            } else {
              node = n + m * mesh_order +
                     (surface % 2) * (mesh_order - 1) * mesh_order * mesh_order;
            }

            // Check that this is an independent node
            if (c[node] >= 0) {
              // Compute the shape functions/derivatives
              double pt[3];
              pt[0] = pt0[0] + knots[n] * d1[0] + knots[m] * d2[0];
              pt[1] = pt0[1] + knots[n] * d1[1] + knots[m] * d2[1];
              pt[2] = pt0[2] + knots[n] * d1[2] + knots[m] * d2[2];
              filter->evalInterp(pt, N, Na, Nb, Nc);

              // Compute Xa and Xb
              TacsScalar Xa[3], Xb[3];
              Xa[0] = Xa[1] = Xa[2] = 0.0;
              Xb[0] = Xb[1] = Xb[2] = 0.0;
              for (int k = 0; k < mesh_order * mesh_order * mesh_order; k++) {
                double na = Na[k] * d1[0] + Nb[k] * d1[1] + Nc[k] * d1[2];
                Xa[0] += X[3 * k] * na;
                Xa[1] += X[3 * k + 1] * na;
                Xa[2] += X[3 * k + 2] * na;

                double nb = Na[k] * d2[0] + Nb[k] * d2[1] + Nc[k] * d2[2];
                Xb[0] += X[3 * k] * nb;
                Xb[1] += X[3 * k + 1] * nb;
                Xb[2] += X[3 * k + 2] * nb;
              }

              // Compute the normal
              TacsScalar normal[3];
              normal[0] = Xa[1] * Xb[2] - Xa[2] * Xb[1];
              normal[1] = Xa[2] * Xb[0] - Xa[0] * Xb[2];
              normal[2] = Xa[0] * Xb[1] - Xa[1] * Xb[0];

              TacsScalar invnorm =
                  1.0 / sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                             normal[2] * normal[2]);
              if (surface % 2 == 0) {
                invnorm *= -1.0;
              }
              normal[0] *= invnorm;
              normal[1] *= invnorm;
              normal[2] *= invnorm;

              normals->setValues(1, &c[node], normal, TACS_ADD_VALUES);
            }
          }
        }
      }
    }
  }

  // Add contributions from all procs
  normals->beginSetValues(TACS_ADD_VALUES);
  normals->endSetValues(TACS_ADD_VALUES);

  // Free values
  delete[] X;
  delete[] N;
  delete[] Na;
  delete[] Nb;
  delete[] Nc;
}

/*
  Create the filter matrix
*/
TMRHelmholtzPUFilter::TMRHelmholtzPUFilter(int _N, int _nlevels,
                                           TACSAssembler *_assembler[],
                                           TMROctForest *_filter[])
    : TMRConformFilter(_nlevels, _assembler, _filter) {
  N = _N;
  t1 = t2 = NULL;
  y1 = y2 = NULL;
  B = NULL;
  Dinv = NULL;
  Tinv = NULL;
  y1 = y2 = NULL;
  temp = NULL;

  xraw = assembler[0]->createDesignVec();
  xraw->incref();
}

TMRHelmholtzPUFilter::TMRHelmholtzPUFilter(int _N, int _nlevels,
                                           TACSAssembler *_assembler[],
                                           TMRQuadForest *_filter[])
    : TMRConformFilter(_nlevels, _assembler, _filter) {
  N = _N;
  t1 = t2 = NULL;
  y1 = y2 = NULL;
  B = NULL;
  Dinv = NULL;
  Tinv = NULL;
  y1 = y2 = NULL;
  temp = NULL;

  xraw = assembler[0]->createDesignVec();
  xraw->incref();
}

/*
  Destroy the filter matrix
*/
TMRHelmholtzPUFilter::~TMRHelmholtzPUFilter() {
  if (t1) {
    t1->decref();
  }
  if (t2) {
    t2->decref();
  }
  if (B) {
    B->decref();
  }
  if (Dinv) {
    Dinv->decref();
  }
  if (Tinv) {
    Tinv->decref();
  }
  if (y1) {
    y1->decref();
  }
  if (y2) {
    y2->decref();
  }
  if (temp) {
    temp->decref();
  }
  if (xraw) {
    xraw->decref();
  }
}

/*
  Initialize the matrix filter.

  This code creates a TACSAssembler object (and frees it), assembles a
  mass matrix, creates the internal variables required for the filter.
*/
void TMRHelmholtzPUFilter::initialize() {
  // Create the Assembler object
  TACSAssembler *matrix_assembler = NULL;
  if (oct_filter) {
    TACSElementModel *model = new TMRHexaMatrixModel();
    TMROctTACSMatrixCreator *matrix_creator3d =
        new TMROctTACSMatrixCreator(model);
    matrix_creator3d->incref();

    matrix_assembler = matrix_creator3d->createTACS(
        oct_filter[0], TACSAssembler::NATURAL_ORDER);
    matrix_assembler->incref();
    matrix_creator3d->decref();
  } else {
    TACSElementModel *model = new TMRQuadMatrixModel();
    TMRQuadTACSMatrixCreator *matrix_creator2d =
        new TMRQuadTACSMatrixCreator(model);
    matrix_creator2d->incref();

    matrix_assembler = matrix_creator2d->createTACS(
        quad_filter[0], TACSAssembler::NATURAL_ORDER);
    matrix_assembler->incref();
    matrix_creator2d->decref();
  }

  // Create the matrix that we're building
  TACSParallelMat *distMat = matrix_assembler->createMat();
  B = distMat;
  B->incref();

  // Get the distribute vector class from the matrix (which contains
  // ghost nodes from adjacent processors that will be needed)
  TACSBVecDistribute *colDist;
  distMat->getExtColMap(&colDist);

  // Get the variable info from TACS
  TACSNodeMap *nodeMap = matrix_assembler->getNodeMap();

  // Get the map between the global-external
  // variables and the local variables (for Bext)
  TACSBVecIndices *colIndex = colDist->getIndices();
  const int *col_vars;
  colIndex->getIndices(&col_vars);

  // Get the dependent node information
  TACSBVecDepNodes *depNodes = matrix_assembler->getBVecDepNodes();

  // Get the distributed list of indices
  TACSBVecDistribute *tacsDist = matrix_assembler->getBVecDistribute();
  TACSBVecIndices *tacsIndex = tacsDist->getIndices();

  // Create a merged list of indices
  TACSBVecIndices *vecIndex = new TACSBVecIndices(tacsIndex, colIndex);
  vecIndex->setUpInverse();
  TACSBVecDistribute *vecDist = new TACSBVecDistribute(nodeMap, vecIndex);

  // Create the node location vector
  TACSBVec *Xpts = new TACSBVec(nodeMap, 3, vecDist, depNodes);
  Xpts->incref();

  // Create the vector for the surface normals
  TACSBVec *normals = new TACSBVec(nodeMap, 3, vecDist, depNodes);
  normals->incref();

  // Get the node locations from the assembler object
  matrix_assembler->getNodes(Xpts);

  // Distribute the node locations
  Xpts->beginDistributeValues();
  Xpts->endDistributeValues();

  // Set the values of the surface normals
  if (oct_filter) {
    computeOctreeBoundaryNormals(oct_filter[0], Xpts, normals);
  } else {
    computeQuadtreeBoundaryNormals(quad_filter[0], Xpts, normals);
  }

  normals->beginDistributeValues();
  normals->endDistributeValues();

  // Get the values in the matrix
  int n, nc;
  distMat->getRowMap(NULL, &n, &nc);

  // Get the local and external contributions
  BCSRMat *Aloc, *Bext;
  distMat->getBCSRMat(&Aloc, &Bext);
  distMat->zeroEntries();

  // Get the sizes of the Aloc and Bext matrices
  const int *rowp, *cols;
  TacsScalar *Avals;
  Aloc->getArrays(NULL, NULL, NULL, &rowp, &cols, &Avals);

  int Nb, Mb;
  const int *browp, *bcols;
  TacsScalar *Bvals;
  Bext->getArrays(NULL, &Nb, &Mb, &browp, &bcols, &Bvals);

  // Create the Dinv vector
  Dinv = matrix_assembler->createVec();
  Dinv->incref();

  TacsScalar *Dvals;
  Dinv->getArray(&Dvals);

  // Get the mpi rank info
  int mpi_rank;
  const int *owner_range;
  nodeMap->getOwnerRange(&owner_range);
  MPI_Comm_rank(nodeMap->getMPIComm(), &mpi_rank);

  for (int i = 0; i < n; i++) {
    // Count up the number of columns in the row
    int num_acols = rowp[i + 1] - rowp[i];

    int ib = -1;
    int num_bcols = 0;
    if (i >= n - nc) {
      ib = i - (n - nc);
      num_bcols = browp[ib + 1] - browp[ib];
    }

    // Allocate space to store the indices
    int num_indices = num_acols + num_bcols;
    int *indices = new int[num_indices];
    TacsScalar *X = new TacsScalar[3 * num_indices];
    double *alpha = new double[num_indices];
    memset(alpha, 0, num_indices * sizeof(double));

    int diagonal_index = -1;

    // Add contributions from the local part of A
    int j = 0;
    for (int jp = rowp[i]; jp < rowp[i + 1]; jp++, j++) {
      indices[j] = cols[jp] + owner_range[mpi_rank];
      if (cols[jp] == i) {
        diagonal_index = j;
      }
    }

    // Add contributions from the external part
    if (ib >= 0) {
      for (int jp = browp[ib]; jp < browp[ib + 1]; jp++, j++) {
        indices[j] = col_vars[bcols[jp]];
      }
    }

    // Get the node locations from the node vector
    Xpts->getValues(num_indices, indices, X);

    // Get the normal
    TacsScalar normal[3];
    normals->getValues(1, &indices[diagonal_index], normal);

    // Find the stencil
    if (normal[0] == 0.0 && normal[1] == 0.0 && normal[2] == 0.0) {
      getInteriorStencil(diagonal_index, num_indices, X, alpha);
    } else {
      TacsScalar invnorm =
          1.0 / sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                     normal[2] * normal[2]);
      normal[0] *= invnorm;
      normal[1] *= invnorm;
      normal[2] *= invnorm;
      getBoundaryStencil(diagonal_index, normal, num_indices, X, alpha);
    }

    // Make the call-back to evaluate the weights
    j = 0;
    for (int jp = rowp[i]; jp < rowp[i + 1]; jp++, j++) {
      if (cols[jp] == i) {
        Dvals[i] = alpha[j];
        if (Dvals[i] <= 0.0) {
          Dvals[i] = 1.0;
        }
        Avals[jp] = 0.0;
      } else {
        Avals[jp] = alpha[j];
        if (Avals[jp] < 0.0) {
          Avals[jp] = 0.0;
        }
      }
    }

    // Add contributions from the external part
    if (ib >= 0) {
      for (int jp = browp[ib]; jp < browp[ib + 1]; jp++, j++) {
        Bvals[jp] = alpha[j];
        if (Bvals[jp] < 0.0) {
          Bvals[jp] = 0.0;
        }
      }
    }

    // Free the allocated space
    delete[] indices;
    delete[] X;
    delete[] alpha;
  }

  // Free the node locations
  Xpts->decref();
  normals->decref();

  // Allocate the vectors needed for the application of the filter
  Tinv = matrix_assembler->createVec();
  t1 = matrix_assembler->createVec();
  t2 = matrix_assembler->createVec();
  y1 = matrix_assembler->createVec();
  y2 = matrix_assembler->createVec();
  Tinv->incref();
  t1->incref();
  t2->incref();
  y1->incref();
  y2->incref();

  // Free this version of TACS - it's not required anymore!
  matrix_assembler->decref();

  // Create a temporary design vector
  temp = assembler[0]->createDesignVec();
  temp->incref();

  // Create the inverse of the diagonal matrix
  TacsScalar *D;
  int size = Dinv->getArray(&D);
  for (int i = 0; i < size; i++) {
    if (D[0] != 0.0) {
      D[0] = 1.0 / D[0];
    } else {
      D[0] = 0.0;
    }
    D++;
  }

  // Apply the filter to create the normalization
  Tinv->set(1.0);
  y2->set(1.0);
  applyFilter(y2, y1);

  // Create the inverse of the T matrix
  TacsScalar *T, *ty;
  size = Tinv->getArray(&T);
  y1->getArray(&ty);
  for (int i = 0; i < size; i++) {
    if (ty[0] != 0.0) {
      T[0] = 1.0 / ty[0];
    } else {
      T[0] = 0.0;
    }
    T++;
    ty++;
  }
}

/*
  Compute the action of the filter on the input vector using Horner's
  method

  t1 = D^{-1}*in
  out = t1
  for n in range(N):
  .   out = t1 + D^{-1}*B*out
*/
void TMRHelmholtzPUFilter::applyFilter(TACSBVec *in, TACSBVec *out) {
  // Compute t1 = D^{-1}*in
  kronecker(Dinv, in, t1);

  // Set out = D^{-1}*in
  out->copyValues(t1);

  // Apply Horner's method
  for (int n = 0; n < N; n++) {
    // Compute out = D^{-1}*B*out
    B->mult(out, t2);
    kronecker(Dinv, t2, out);

    // Compute out = t1 + D^{-1}*B*out
    out->axpy(1.0, t1);
  }

  // Multiply by Tinv
  kronecker(Tinv, out);
}

/*
  Compute the transpose of the filter operation
*/
void TMRHelmholtzPUFilter::applyTranspose(TACSBVec *in, TACSBVec *out) {
  kronecker(Tinv, in, t1);

  // Copy the values from t1 to the out vector
  out->copyValues(t1);

  // Apply Horner's method
  for (int n = 0; n < N; n++) {
    // Compute B^{T}*D^{-1}*out
    kronecker(Dinv, out, t2);
    B->multTranspose(t2, out);

    // Compute out = t1 + B^{T}*D^{-1}*out
    out->axpy(1.0, t1);
  }

  // Multiply by Dinv
  kronecker(Dinv, out);
}

/*
  Compute the Kronecker product of the input vectors c and x and store
  the result in either y (if it is provided) or otherwise x

  y = (c o x)
*/
void TMRHelmholtzPUFilter::kronecker(TACSBVec *c, TACSBVec *x, TACSBVec *y) {
  if (c && x && y) {
    TacsScalar *cvals, *xvals, *yvals;
    int size = c->getArray(&cvals);
    x->getArray(&xvals);
    y->getArray(&yvals);

    for (int i = 0; i < size; i++) {
      yvals[0] = cvals[0] * xvals[0];
      yvals++;
      xvals++;
      cvals++;
    }
  } else if (c && x) {
    TacsScalar *cvals, *xvals;
    int size = c->getArray(&cvals);
    x->getArray(&xvals);

    for (int i = 0; i < size; i++) {
      xvals[0] *= cvals[0];
      xvals++;
      cvals++;
    }
  }
}

/*
  Set the design variables for each level
*/
void TMRHelmholtzPUFilter::setDesignVars(TACSBVec *xvec) {
  xraw->copyValues(xvec);
  const int vpn = assembler[0]->getDesignVarsPerNode();

  if (vpn == 1) {
    applyFilter(xvec, x[0]);
  } else {
    for (int k = 0; k < vpn; k++) {
      TacsScalar *xin, *xout;
      xvec->getArray(&xin);
      x[0]->getArray(&xout);

      // Set the pointers offset by the vars per node
      xin = &xin[k];
      xout = &xout[k];

      // Copy the values to the input vector (y1)
      TacsScalar *yin;
      int size = y1->getArray(&yin);
      for (int i = 0; i < size; i++) {
        yin[0] = xin[0];
        yin++;
        xin += vpn;
      }

      // Apply the matrix filter from the input y1 to the
      // output y2
      applyFilter(y1, y2);

      // Copy the values from y2 to the output vector x[0]
      TacsScalar *yout;
      y2->getArray(&yout);
      for (int i = 0; i < size; i++) {
        xout[0] = yout[0];
        yout++;
        xout += vpn;
      }
    }
  }

  assembler[0]->setDesignVars(x[0]);

  // Set the design variable values on all levels
  for (int k = 0; k < nlevels - 1; k++) {
    filter_interp[k]->multWeightTranspose(x[k], x[k + 1]);

    // Set the design variable values
    assembler[k + 1]->setDesignVars(x[k + 1]);
  }
}

/*
  Get the unfiltered design variable at the finest level
*/
void TMRHelmholtzPUFilter::getDesignVars(TACSBVec *xvec) {
  xvec->copyValues(xraw);
}

/*
  Add values to the output TACSBVec
*/
void TMRHelmholtzPUFilter::addValues(TACSBVec *vec) {
  vec->beginSetValues(TACS_ADD_VALUES);
  vec->endSetValues(TACS_ADD_VALUES);

  temp->copyValues(vec);

  const int vpn = assembler[0]->getDesignVarsPerNode();
  if (vpn == 1) {
    applyTranspose(temp, vec);
  } else {
    for (int k = 0; k < vpn; k++) {
      TacsScalar *xin, *xout;

      // Get the pointer to the array and offset by vars per node
      temp->getArray(&xin);
      xin = &xin[k];

      // Copy the values to the input vector (y1)
      TacsScalar *yin;
      int size = y1->getArray(&yin);
      for (int i = 0; i < size; i++) {
        yin[0] = xin[0];
        yin++;
        xin += vpn;
      }

      // Apply the matrix filter from the input y1 to the
      // output y2
      applyTranspose(y1, y2);

      // Add the values from y2 back to the output vector vec
      vec->getArray(&xout);
      xout = &xout[k];

      TacsScalar *yout;
      y2->getArray(&yout);
      for (int i = 0; i < size; i++) {
        xout[0] = yout[0];
        yout++;
        xout += vpn;
      }
    }
  }
}
