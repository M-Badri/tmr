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

#include "TMR_RefinementTools.h"

#include "TACSElementAlgebra.h"
#include "tacslapack.h"

// Include the stdlib set/string classes
#include <set>
#include <string>

// Include for writing output file
#include <stdio.h>
#include <stdlib.h>

/*
  Create a multgrid object for a forest of octrees
*/
void TMR_CreateTACSMg(int num_levels, TACSAssembler *assembler[],
                      TMROctForest *forest[], TACSMg **_mg, double omega,
                      int use_galerkin, int use_coarse_direct_solve,
                      int use_chebyshev_smoother) {
  // Get the communicator
  MPI_Comm comm = assembler[0]->getMPIComm();

  // Create the multigrid object
  int zero_guess = 0;
  double lower = 1.0 / 30.0, upper = 1.1;
  int cheb_degree = 3;
  int mg_smooth_iters = 1;
  int mg_sor_symm = 1;
  int mg_iters_per_level = 1;
  TACSMg *mg =
      new TACSMg(comm, num_levels, omega, mg_smooth_iters, mg_sor_symm);

  // Create the intepolation/restriction objects between mesh levels
  for (int level = 0; level < num_levels - 1; level++) {
    // Create the interpolation object
    TACSBVecInterp *interp =
        new TACSBVecInterp(assembler[level + 1], assembler[level]);

    // Set the interpolation
    forest[level]->createInterpolation(forest[level + 1], interp);

    // Initialize the interpolation
    interp->initialize();

    if (use_chebyshev_smoother) {
      // Create the matrix
      TACSParallelMat *mat = assembler[level]->createMat();

      // Set up the smoother
      TACSChebyshevSmoother *pc = new TACSChebyshevSmoother(
          mat, cheb_degree, lower, upper, mg_smooth_iters);

      // Set the interpolation and TACS object within the multigrid object
      mg->setLevel(level, assembler[level], interp, mg_iters_per_level,
                   use_galerkin, mat, pc);
    } else {
      mg->setLevel(level, assembler[level], interp, mg_iters_per_level,
                   use_galerkin);
    }
  }

  if (use_coarse_direct_solve) {
    // Set the lowest level - with no interpolation object
    mg->setLevel(num_levels - 1, assembler[num_levels - 1], NULL, 1,
                 use_galerkin);
  } else {
    TACSParallelMat *mat = assembler[num_levels - 1]->createMat();
    TACSPc *pc = NULL;

    if (use_chebyshev_smoother) {
      // Set up the smoother
      pc = new TACSChebyshevSmoother(mat, cheb_degree, lower, upper,
                                     mg_smooth_iters);
    } else {
      pc = new TACSGaussSeidel(mat, zero_guess, omega, mg_smooth_iters,
                               mg_sor_symm);
    }

    // Set the interpolation and TACS object within the multigrid object
    mg->setLevel(num_levels - 1, assembler[num_levels - 1], NULL, 1,
                 use_galerkin, mat, pc);
  }

  // Return the multigrid object
  *_mg = mg;
}

/*
  Create the TACS multigrid objects for the quadrilateral case
*/
void TMR_CreateTACSMg(int num_levels, TACSAssembler *assembler[],
                      TMRQuadForest *forest[], TACSMg **_mg, double omega,
                      int use_galerkin, int use_coarse_direct_solve,
                      int use_chebyshev_smoother) {
  // Get the communicator
  MPI_Comm comm = assembler[0]->getMPIComm();

  // Create the multigrid object
  int zero_guess = 0;
  double lower = 1.0 / 30.0, upper = 1.1;
  int cheb_degree = 3;
  int mg_smooth_iters = 1;
  int mg_sor_symm = 0;
  int mg_iters_per_level = 1;
  TACSMg *mg =
      new TACSMg(comm, num_levels, omega, mg_smooth_iters, mg_sor_symm);

  // Create the intepolation/restriction objects between mesh levels
  for (int level = 0; level < num_levels - 1; level++) {
    // Create the interpolation object
    TACSBVecInterp *interp =
        new TACSBVecInterp(assembler[level + 1], assembler[level]);

    // Set the interpolation
    forest[level]->createInterpolation(forest[level + 1], interp);

    // Initialize the interpolation
    interp->initialize();

    if (use_chebyshev_smoother) {
      // Create the matrix
      TACSParallelMat *mat = assembler[level]->createMat();

      // Set up the smoother
      TACSChebyshevSmoother *pc = new TACSChebyshevSmoother(
          mat, cheb_degree, lower, upper, mg_smooth_iters);

      // Set the interpolation and TACS object within the multigrid object
      mg->setLevel(level, assembler[level], interp, mg_iters_per_level,
                   use_galerkin, mat, pc);
    } else {
      mg->setLevel(level, assembler[level], interp, mg_iters_per_level,
                   use_galerkin);
    }
  }

  if (use_coarse_direct_solve) {
    // Set the lowest level - with no interpolation object
    mg->setLevel(num_levels - 1, assembler[num_levels - 1], NULL, 1,
                 use_galerkin);
  } else {
    TACSParallelMat *mat = assembler[num_levels - 1]->createMat();
    TACSPc *pc = NULL;

    if (use_chebyshev_smoother) {
      // Set up the smoother
      pc = new TACSChebyshevSmoother(mat, cheb_degree, lower, upper,
                                     mg_smooth_iters);
    } else {
      pc = new TACSGaussSeidel(mat, zero_guess, omega, mg_smooth_iters,
                               mg_sor_symm);
    }

    // Set the interpolation and TACS object within the multigrid object
    mg->setLevel(num_levels - 1, assembler[num_levels - 1], NULL, 1,
                 use_galerkin, mat, pc);
  }

  // Return the multigrid object
  *_mg = mg;
}

/*
  Compute the transpose of the Jacobian transformation at a point
  within the element.
*/
static TacsScalar computeJacobianTrans2D(const TacsScalar Xpts[],
                                         const double Na[], const double Nb[],
                                         TacsScalar Xd[], TacsScalar J[],
                                         const int num_nodes) {
  memset(Xd, 0, 9 * sizeof(TacsScalar));

  // Compute the derivative along the coordinate directions
  const double *na = Na, *nb = Nb;
  const TacsScalar *X = Xpts;
  for (int i = 0; i < num_nodes; i++) {
    Xd[0] += X[0] * na[0];
    Xd[1] += X[1] * na[0];
    Xd[2] += X[2] * na[0];

    Xd[3] += X[0] * nb[0];
    Xd[4] += X[1] * nb[0];
    Xd[5] += X[2] * nb[0];

    na++;
    nb++;
    X += 3;
  }

  // Compute the cross-product with the normal
  crossProduct(&Xd[0], &Xd[3], &Xd[6]);
  vec3Normalize(&Xd[6]);

  // Compute the transpose of the Jacobian transformation
  return inv3x3(Xd, J);
}

/*
  Compute the transpose of the 3D Jacobian transformation at a point
  within the element
*/
static TacsScalar computeJacobianTrans3D(const TacsScalar Xpts[],
                                         const double Na[], const double Nb[],
                                         const double Nc[], TacsScalar Xd[],
                                         TacsScalar J[], const int num_nodes) {
  memset(Xd, 0, 9 * sizeof(TacsScalar));

  // Compute the derivative along the coordinate directions
  const double *na = Na, *nb = Nb, *nc = Nc;
  const TacsScalar *X = Xpts;
  for (int i = 0; i < num_nodes; i++) {
    Xd[0] += X[0] * na[0];
    Xd[1] += X[1] * na[0];
    Xd[2] += X[2] * na[0];

    Xd[3] += X[0] * nb[0];
    Xd[4] += X[1] * nb[0];
    Xd[5] += X[2] * nb[0];

    Xd[6] += X[0] * nc[0];
    Xd[7] += X[1] * nc[0];
    Xd[8] += X[2] * nc[0];

    na++;
    nb++;
    nc++;
    X += 3;
  }

  // Compute the transpose of the Jacobian transformation
  return inv3x3(Xd, J);
}

// Specify the maximum order
static const int MAX_ORDER = 6;

// Specify the maximum number of enrichment functions in 2D and 3D
static const int MAX_2D_ENRICH = 9;
static const int MAX_3D_ENRICH = 15;

/*
  Get the number of enrichment functions associated with the given
  order of the element
*/
static int getNum2dEnrich(int order) {
  if (order == 2) {
    return 5;
  } else if (order == 3) {
    return 7;
  }
  return 9;
}

static int getNum3dEnrich(int order) {
  if (order == 2) {
    return 9;
  }
  return 15;
}

/*
  Evaluate the enrichment function for a second-order shell problem
*/
static void evalEnrichmentFuncs2D(const int order, const double pt[],
                                  const double *knots, double N[]) {
  if (order == 2) {
    // Compute the cubic enrichment shape functions along the two
    // coordinate directions
    double ca = (1.0 + pt[0]) * (1.0 - pt[0]);
    double cb = (1.0 + pt[1]) * (1.0 - pt[1]);

    N[0] = ca;
    N[1] = pt[1] * ca;
    N[2] = cb;
    N[3] = pt[0] * cb;
    N[4] = ca * cb;
  } else if (order == 3) {
    // Compute the cubic enrichment shape functions along the two
    // coordinate directions
    double ca = (1.0 + pt[0]) * pt[0] * (1.0 - pt[0]);
    double cb = (1.0 + pt[1]) * pt[1] * (1.0 - pt[1]);

    // Set the shape functions themselves
    N[0] = ca;
    N[1] = pt[1] * ca;
    N[2] = pt[1] * pt[1] * ca;
    N[3] = cb;
    N[4] = pt[0] * cb;
    N[5] = pt[0] * pt[0] * cb;
    N[6] = ca * cb;
  } else if (order == 4) {
    double ca = (1.0 + pt[0]) * (1.0 - pt[0]);
    ca *= (pt[0] - knots[1]) * (pt[0] - knots[2]);

    double cb = (1.0 + pt[1]) * (1.0 - pt[1]);
    cb *= (pt[1] - knots[1]) * (pt[1] - knots[2]);

    // Set the shape functions themselves
    N[0] = ca;
    N[1] = pt[1] * ca;
    N[2] = pt[1] * pt[1] * ca;
    N[3] = pt[1] * pt[1] * pt[1] * ca;
    N[4] = cb;
    N[5] = pt[0] * cb;
    N[6] = pt[0] * pt[0] * cb;
    N[7] = pt[0] * pt[0] * pt[0] * cb;
    N[8] = ca * cb;
  }
}

/*
  Evaluate the derivative of the enrichment functions for a
  second-order problem.
*/
static void evalEnrichmentFuncs2D(const int order, const double pt[],
                                  const double *knots, double N[], double Na[],
                                  double Nb[]) {
  if (order == 2) {
    // Compute the cubic enrichment shape functions along the two
    // coordinate directions
    double ca = (1.0 + pt[0]) * (1.0 - pt[0]);
    double cb = (1.0 + pt[1]) * (1.0 - pt[1]);

    // Compute the derivatives
    double da = -2.0 * pt[0];
    double db = -2.0 * pt[1];

    // Evaluate the shape functions
    N[0] = ca;
    N[1] = pt[1] * ca;
    N[2] = cb;
    N[3] = pt[0] * cb;
    N[4] = ca * cb;

    // Evaluate the derivatives of the shape functions
    Na[0] = da;
    Na[1] = pt[1] * da;
    Na[2] = 0.0;
    Na[3] = cb;
    Na[4] = da * cb;

    Nb[0] = 0.0;
    Nb[1] = ca;
    Nb[2] = db;
    Nb[3] = pt[0] * db;
    Nb[4] = ca * db;
  } else if (order == 3) {
    // Compute the cubic enrichment shape functions along the two
    // coordinate directions
    double ca = (1.0 + pt[0]) * pt[0] * (1.0 - pt[0]);
    double cb = (1.0 + pt[1]) * pt[1] * (1.0 - pt[1]);

    // Compute the derivative of the enrichment functions w.r.t.
    // the two coordinate directions
    double da = 1.0 - 3 * pt[0] * pt[0];
    double db = 1.0 - 3 * pt[1] * pt[1];

    // Set the shape functions themselves
    N[0] = ca;
    N[1] = pt[1] * ca;
    N[2] = pt[1] * pt[1] * ca;
    N[3] = cb;
    N[4] = pt[0] * cb;
    N[5] = pt[0] * pt[0] * cb;
    N[6] = ca * cb;

    // Set the derivatives of the enrichment functions with respect to
    // the first and second coordinate directions
    Na[0] = da;
    Na[1] = pt[1] * da;
    Na[2] = pt[1] * pt[1] * da;
    Na[3] = 0.0;
    Na[4] = cb;
    Na[5] = 2.0 * pt[0] * cb;
    Na[6] = da * cb;

    Nb[0] = 0.0;
    Nb[1] = ca;
    Nb[2] = 2.0 * pt[1] * ca;
    Nb[3] = db;
    Nb[4] = pt[0] * db;
    Nb[5] = pt[0] * pt[0] * db;
    Nb[6] = ca * db;
  } else {
    double ca = (1.0 + pt[0]) * (1.0 - pt[0]);
    ca *= (pt[0] - knots[1]) * (pt[0] - knots[2]);
    double da =
        -2.0 * pt[0] * (pt[0] - knots[1]) * (pt[0] - knots[2]) +
        (1.0 + pt[0]) * (1.0 - pt[0]) * (2.0 * pt[0] - knots[1] - knots[2]);

    double cb = (1.0 + pt[1]) * (1.0 - pt[1]);
    cb *= (pt[1] - knots[1]) * (pt[1] - knots[2]);
    double db =
        -2.0 * pt[1] * (pt[1] - knots[1]) * (pt[1] - knots[2]) +
        (1.0 + pt[1]) * (1.0 - pt[1]) * (2.0 * pt[1] - knots[1] - knots[2]);

    // Set the shape functions themselves
    N[0] = ca;
    N[1] = pt[1] * ca;
    N[2] = pt[1] * pt[1] * ca;
    N[3] = pt[1] * pt[1] * pt[1] * ca;
    N[4] = cb;
    N[5] = pt[0] * cb;
    N[6] = pt[0] * pt[0] * cb;
    N[7] = pt[0] * pt[0] * pt[0] * cb;
    N[8] = ca * cb;

    Na[0] = da;
    Na[1] = pt[1] * da;
    Na[2] = pt[1] * pt[1] * da;
    Na[3] = pt[1] * pt[1] * pt[1] * da;
    Na[4] = 0.0;
    Na[5] = cb;
    Na[6] = 2 * pt[0] * cb;
    Na[7] = 3 * pt[0] * pt[0] * cb;
    Na[8] = da * cb;

    Nb[0] = 0.0;
    Nb[1] = ca;
    Nb[2] = 2 * pt[1] * ca;
    Nb[3] = 3 * pt[1] * pt[1] * ca;
    Nb[4] = db;
    Nb[5] = pt[0] * db;
    Nb[6] = pt[0] * pt[0] * db;
    Nb[7] = pt[0] * pt[0] * pt[0] * db;
    Nb[8] = ca * db;
  }
}

/*
  Evaluate the enrichment function for a second-order shell problem
*/
static void eval2ndEnrichmentFuncs3D(const double pt[], double N[]) {
  // Compute the cubic enrichment shape functions along the two
  // coordinate directions
  double ca = (1.0 + pt[0]) * (1.0 - pt[0]);
  double cb = (1.0 + pt[1]) * (1.0 - pt[1]);
  double cc = (1.0 + pt[2]) * (1.0 - pt[2]);

  N[0] = ca;
  N[1] = pt[1] * ca;
  N[2] = pt[2] * ca;
  N[3] = cb;
  N[4] = pt[0] * cb;
  N[5] = pt[2] * cb;
  N[6] = cc;
  N[7] = pt[0] * cc;
  N[8] = pt[1] * cc;
}

/*
  Evaluate the derivative of the enrichment functions for a
  second-order problem.
*/
static void eval2ndEnrichmentFuncs3D(const double pt[], double N[], double Na[],
                                     double Nb[], double Nc[]) {
  // Compute the cubic enrichment shape functions along the two
  // coordinate directions
  double ca = (1.0 + pt[0]) * (1.0 - pt[0]);
  double cb = (1.0 + pt[1]) * (1.0 - pt[1]);
  double cc = (1.0 + pt[2]) * (1.0 - pt[2]);

  // Compute the derivatives
  double da = -2.0 * pt[0];
  double db = -2.0 * pt[1];
  double dc = -2.0 * pt[2];

  // Evaluate the shape functions
  N[0] = ca;
  N[1] = pt[1] * ca;
  N[2] = pt[2] * ca;
  N[3] = cb;
  N[4] = pt[0] * cb;
  N[5] = pt[2] * cb;
  N[6] = cc;
  N[7] = pt[0] * cc;
  N[8] = pt[1] * cc;

  // Evaluate the derivatives of the shape functions
  Na[0] = da;
  Na[1] = pt[1] * da;
  Na[2] = pt[2] * da;
  Na[3] = 0.0;
  Na[4] = cb;
  Na[5] = 0.0;
  Na[6] = 0.0;
  Na[7] = cc;
  Na[8] = 0.0;

  Nb[0] = 0.0;
  Nb[1] = ca;
  Nb[2] = 0.0;
  Nb[3] = db;
  Nb[4] = pt[0] * db;
  Nb[5] = pt[2] * db;
  Nb[6] = 0.0;
  Nb[7] = 0.0;
  Nb[8] = cc;

  Nc[0] = 0.0;
  Nc[1] = 0.0;
  Nc[2] = ca;
  Nc[3] = 0.0;
  Nc[4] = 0.0;
  Nc[5] = cb;
  Nc[6] = dc;
  Nc[7] = pt[0] * dc;
  Nc[8] = pt[1] * dc;
}

/*
  Evaluate the enrichment function for a second-order shell problem
*/
static void eval3rdEnrichmentFuncs3D(const double pt[], double N[]) {
  // Compute the cubic enrichment shape functions along the two
  // coordinate directions
  double ca = (1.0 + pt[0]) * pt[0] * (1.0 - pt[0]);
  double cb = (1.0 + pt[1]) * pt[1] * (1.0 - pt[1]);
  double cc = (1.0 + pt[2]) * pt[2] * (1.0 - pt[2]);

  N[0] = ca;
  N[1] = pt[1] * ca;
  N[2] = pt[1] * pt[1] * ca;
  N[3] = pt[2] * ca;
  N[4] = pt[2] * pt[2] * ca;
  N[5] = cb;
  N[6] = pt[0] * cb;
  N[7] = pt[0] * pt[0] * cb;
  N[8] = pt[2] * cb;
  N[9] = pt[2] * pt[2] * cb;
  N[10] = cc;
  N[11] = pt[0] * cc;
  N[12] = pt[0] * pt[0] * cc;
  N[13] = pt[1] * cc;
  N[14] = pt[1] * pt[1] * cc;
}

/*
  Evaluate the derivative of the enrichment functions for a
  third-order problem.
*/
static void eval3rdEnrichmentFuncs3D(const double pt[], double N[], double Na[],
                                     double Nb[], double Nc[]) {
  // Compute the cubic enrichment shape functions along the two
  // coordinate directions
  double ca = (1.0 + pt[0]) * pt[0] * (1.0 - pt[0]);
  double cb = (1.0 + pt[1]) * pt[1] * (1.0 - pt[1]);
  double cc = (1.0 + pt[2]) * pt[2] * (1.0 - pt[2]);

  // Compute the derivatives
  double da = 1.0 - 3.0 * pt[0] * pt[0];
  double db = 1.0 - 3.0 * pt[1] * pt[1];
  double dc = 1.0 - 3.0 * pt[2] * pt[2];

  // Evaluate the shape functions
  N[0] = ca;
  N[1] = pt[1] * ca;
  N[2] = pt[1] * pt[1] * ca;
  N[3] = pt[2] * ca;
  N[4] = pt[2] * pt[2] * ca;
  N[5] = cb;
  N[6] = pt[0] * cb;
  N[7] = pt[0] * pt[0] * cb;
  N[8] = pt[2] * cb;
  N[9] = pt[2] * pt[2] * cb;
  N[10] = cc;
  N[11] = pt[0] * cc;
  N[12] = pt[0] * pt[0] * cc;
  N[13] = pt[1] * cc;
  N[14] = pt[1] * pt[1] * cc;

  // Evaluate the derivatives of the shape functions
  Na[0] = da;
  Na[1] = pt[1] * da;
  Na[2] = pt[1] * pt[1] * da;
  Na[3] = pt[2] * da;
  Na[4] = pt[2] * pt[2] * da;
  Na[5] = 0.0;
  Na[6] = cb;
  Na[7] = 2.0 * pt[0] * cb;
  Na[8] = 0.0;
  Na[9] = 0.0;
  Na[10] = 0.0;
  Na[11] = cc;
  Na[12] = 2.0 * pt[0] * cc;
  Na[13] = 0.0;
  Na[14] = 0.0;

  Nb[0] = 0.0;
  Nb[1] = ca;
  Nb[2] = 2.0 * pt[1] * ca;
  Nb[3] = 0.0;
  Nb[4] = 0.0;
  Nb[5] = db;
  Nb[6] = pt[0] * db;
  Nb[7] = pt[0] * pt[0] * db;
  Nb[8] = pt[2] * db;
  Nb[9] = pt[2] * pt[2] * db;
  Nb[10] = 0.0;
  Nb[11] = 0.0;
  Nb[12] = 0.0;
  Nb[13] = cc;
  Nb[14] = 2.0 * pt[1] * cc;

  Nc[0] = 0.0;
  Nc[1] = 0.0;
  Nc[2] = 0.0;
  Nc[3] = ca;
  Nc[4] = 2.0 * pt[2] * ca;
  Nc[5] = 0.0;
  Nc[6] = 0.0;
  Nc[7] = 0.0;
  Nc[8] = cb;
  Nc[9] = 2.0 * pt[2] * cb;
  Nc[10] = dc;
  Nc[11] = pt[0] * dc;
  Nc[12] = pt[0] * pt[0] * dc;
  Nc[13] = pt[1] * dc;
  Nc[14] = pt[1] * pt[1] * dc;
}

/*
  Given the values of the derivatives of one component of the
  displacement or rotation field at the nodes, compute the
  reconstruction over the element by solving a least-squares problem

  input:
  Xpts:    the element node locations
  uvals:   the solution at the nodes
  uderiv:  the derivative of the solution in x/y/z at the nodes

  output:
  ubar:    the values of the coefficients on the enrichment functions
*/
static void computeElemRecon2D(const int vars_per_node, TMRQuadForest *forest,
                               TMRQuadForest *refined_forest,
                               const TacsScalar Xpts[],
                               const TacsScalar uvals[],
                               const TacsScalar uderiv[], TacsScalar ubar[],
                               TacsScalar *tmp) {
  // Get information about the interpolation
  const double *knots, *refined_knots;
  const int order = forest->getInterpKnots(&knots);
  const int refined_order = refined_forest->getInterpKnots(&refined_knots);

  // The number of enrichment functions
  const int nenrich = getNum2dEnrich(order);

  // The number of equations
  const int neq = 2 * order * order;

  // The number of derivatives per node
  const int deriv_per_node = 3 * vars_per_node;

  // Set up the least squares problem at the nodes
  int nrhs = vars_per_node;

  TacsScalar *A = &tmp[0];
  TacsScalar *b = &tmp[nenrich * neq];

  // Set the weighs
  double wvals[4];
  if (order == 2) {
    wvals[0] = wvals[1] = 1.0;
  } else if (order == 3) {
    wvals[0] = wvals[2] = 0.5;
    wvals[1] = 1.0;
  } else {
    wvals[0] = wvals[3] = 0.5;
    wvals[1] = wvals[2] = 1.0;
  }

  for (int c = 0, jj = 0; jj < order; jj++) {
    for (int ii = 0; ii < order; ii++, c += 2) {
      // Set the parametric location within the element
      double pt[2];
      pt[0] = knots[ii];
      pt[1] = knots[jj];

      // Compute the element shape functions at this point
      double N[MAX_ORDER * MAX_ORDER];
      double Na[MAX_ORDER * MAX_ORDER], Nb[MAX_ORDER * MAX_ORDER];
      refined_forest->evalInterp(pt, N, Na, Nb);

      // Evaluate the Jacobian transformation at this point
      TacsScalar Xd[9], J[9];
      computeJacobianTrans2D(Xpts, Na, Nb, Xd, J,
                             refined_order * refined_order);

      // Normalize the first direction
      TacsScalar d1[3], d2[3];
      d1[0] = Xd[0];
      d1[1] = Xd[1];
      d1[2] = Xd[2];
      vec3Normalize(d1);

      // Compute d2 = n x d1
      crossProduct(&Xd[6], d1, d2);

      // First, compute the contributions to the righ-hand-side. The
      // right vector contains the difference between the prescribed
      // derivative and the contribution to the derivative from the
      // quadratic shape function terms
      const TacsScalar *ud = &uderiv[deriv_per_node * (ii + order * jj)];

      for (int k = 0; k < vars_per_node; k++) {
        b[neq * k + c] = wvals[ii] * wvals[jj] *
                         (d1[0] * ud[0] + d1[1] * ud[1] + d1[2] * ud[2]);
        b[neq * k + c + 1] = wvals[ii] * wvals[jj] *
                             (d2[0] * ud[0] + d2[1] * ud[1] + d2[2] * ud[2]);
        ud += 3;
      }

      // Evaluate the interpolation on the original mesh
      forest->evalInterp(pt, N, Na, Nb);

      // Add the contribution from the nodes
      for (int k = 0; k < vars_per_node; k++) {
        // Evaluate the derivatives along the parametric directions
        TacsScalar Ua = 0.0, Ub = 0.0;
        for (int i = 0; i < order * order; i++) {
          Ua += uvals[vars_per_node * i + k] * Na[i];
          Ub += uvals[vars_per_node * i + k] * Nb[i];
        }

        // Compute the derivative along the x,y,z directions
        TacsScalar d[3];
        d[0] = Ua * J[0] + Ub * J[1];
        d[1] = Ua * J[3] + Ub * J[4];
        d[2] = Ua * J[6] + Ub * J[7];

        b[neq * k + c] -= wvals[ii] * wvals[jj] *
                          (d1[0] * d[0] + d1[1] * d[1] + d1[2] * d[2]);
        b[neq * k + c + 1] -= wvals[ii] * wvals[jj] *
                              (d2[0] * d[0] + d2[1] * d[1] + d2[2] * d[2]);
      }

      // xi,X = [X,xi]^{-1}
      // U,X = U,xi*xi,X = U,xi*J^{T}

      // Now, evaluate the terms for the left-hand-side
      // that contribute to the derivative
      double Nr[MAX_2D_ENRICH];
      double Nar[MAX_2D_ENRICH], Nbr[MAX_2D_ENRICH];
      evalEnrichmentFuncs2D(order, pt, knots, Nr, Nar, Nbr);

      // Add the contributions to the the enricment
      for (int i = 0; i < nenrich; i++) {
        TacsScalar d[3];
        d[0] = Nar[i] * J[0] + Nbr[i] * J[1];
        d[1] = Nar[i] * J[3] + Nbr[i] * J[4];
        d[2] = Nar[i] * J[6] + Nbr[i] * J[7];

        A[neq * i + c] = wvals[ii] * wvals[jj] *
                         (d1[0] * d[0] + d1[1] * d[1] + d1[2] * d[2]);
        A[neq * i + c + 1] = wvals[ii] * wvals[jj] *
                             (d2[0] * d[0] + d2[1] * d[1] + d2[2] * d[2]);
      }
    }
  }

  // Singular values
  TacsScalar s[MAX_2D_ENRICH];
  int m = neq, n = nenrich;
  double rcond = -1.0;
  int rank;

  // Length of the work array
  int lwork = 512;
  TacsScalar work[512];

  // LAPACK output status
  int info;

  // Using LAPACK, compute the least squares solution
  LAPACKdgelss(&m, &n, &nrhs, A, &m, b, &m, s, &rcond, &rank, work, &lwork,
               &info);

  // Copy over the ubar solution
  for (int i = 0; i < nenrich; i++) {
    for (int j = 0; j < vars_per_node; j++) {
      ubar[vars_per_node * i + j] = b[m * j + i];
    }
  }
}

/*
  Given the values of the derivatives of one component of the
  displacement at the nodes, compute the reconstruction over the
  element by solving a least-squares problem

  input:
  Xpts:    the element node locations
  uvals:   the solution at the nodes
  uderiv:  the derivative of the solution in x/y/z at the nodes

  output:
  ubar:    the values of the coefficients on the enrichment functions
*/
static void computeElemRecon3D(const int vars_per_node, TMROctForest *forest,
                               TMROctForest *refined_forest,
                               const TacsScalar Xpts[],
                               const TacsScalar uvals[],
                               const TacsScalar uderiv[], TacsScalar ubar[],
                               TacsScalar *tmp) {
  // Get information about the interpolation
  const double *knots;
  const int order = forest->getInterpKnots(&knots);
  const int refined_order = refined_forest->getMeshOrder();

  // Get the number of enrichment functions
  const int nenrich = getNum3dEnrich(order);

  // The number of equations
  const int neq = 3 * order * order * order;

  // The number of derivatives per node
  const int deriv_per_node = 3 * vars_per_node;

  // Set up the least squares problem at the nodes
  int nrhs = vars_per_node;

  TacsScalar *A = &tmp[0];
  TacsScalar *b = &tmp[nenrich * neq];

  // Set the weights
  double wvals[3];
  if (order == 2) {
    wvals[0] = wvals[1] = 1.0;
  } else if (order == 3) {
    wvals[0] = wvals[2] = 0.5;
    wvals[1] = 1.0;
  }

  for (int c = 0, kk = 0; kk < order; kk++) {
    for (int jj = 0; jj < order; jj++) {
      for (int ii = 0; ii < order; ii++, c += 3) {
        // Set the parametric location within the element
        double pt[3];
        pt[0] = knots[ii];
        pt[1] = knots[jj];
        pt[2] = knots[kk];

        // Compute the element shape functions at this point
        double N[MAX_ORDER * MAX_ORDER * MAX_ORDER];
        double Na[MAX_ORDER * MAX_ORDER * MAX_ORDER];
        double Nb[MAX_ORDER * MAX_ORDER * MAX_ORDER];
        double Nc[MAX_ORDER * MAX_ORDER * MAX_ORDER];
        refined_forest->evalInterp(pt, N, Na, Nb, Nc);

        // Evaluate the Jacobian transformation at this point
        TacsScalar Xd[9], J[9];
        computeJacobianTrans3D(Xpts, Na, Nb, Nc, Xd, J,
                               refined_order * refined_order * refined_order);

        // First, compute the contributions to the righ-hand-side. The
        // right vector contains the difference between the prescribed
        // derivative and the contribution to the derivative from the
        // quadratic shape function terms
        const TacsScalar *ud =
            &uderiv[deriv_per_node * (ii + order * jj + order * order * kk)];
        for (int k = 0; k < vars_per_node; k++) {
          b[neq * k + c] = wvals[ii] * wvals[jj] * wvals[kk] * ud[0];
          b[neq * k + c + 1] = wvals[ii] * wvals[jj] * wvals[kk] * ud[1];
          b[neq * k + c + 2] = wvals[ii] * wvals[jj] * wvals[kk] * ud[2];
          ud += 3;
        }

        // Compute the shape functions on the coarser mesh
        forest->evalInterp(pt, N, Na, Nb, Nc);

        // Add the contribution from the nodes
        for (int k = 0; k < vars_per_node; k++) {
          // Evaluate the derivatives along the parametric directions
          TacsScalar Ua = 0.0, Ub = 0.0, Uc = 0.0;
          for (int i = 0; i < order * order * order; i++) {
            Ua += uvals[vars_per_node * i + k] * Na[i];
            Ub += uvals[vars_per_node * i + k] * Nb[i];
            Uc += uvals[vars_per_node * i + k] * Nc[i];
          }

          // Compute the derivative along the x,y,z directions
          TacsScalar d[3];
          d[0] = Ua * J[0] + Ub * J[1] + Uc * J[2];
          d[1] = Ua * J[3] + Ub * J[4] + Uc * J[5];
          d[2] = Ua * J[6] + Ub * J[7] + Uc * J[8];

          b[neq * k + c] -= wvals[ii] * wvals[jj] * wvals[kk] * d[0];
          b[neq * k + c + 1] -= wvals[ii] * wvals[jj] * wvals[kk] * d[1];
          b[neq * k + c + 2] -= wvals[ii] * wvals[jj] * wvals[kk] * d[2];
        }

        // Now, evaluate the terms for the left-hand-side that
        // contribute to the derivative
        // xi,X = [X,xi]^{-1}
        // U,X = U,xi*xi,X = U,xi*J^{T}
        double Nr[MAX_3D_ENRICH];
        double Nar[MAX_3D_ENRICH], Nbr[MAX_3D_ENRICH], Ncr[MAX_3D_ENRICH];
        if (order == 2) {
          eval2ndEnrichmentFuncs3D(pt, Nr, Nar, Nbr, Ncr);
        } else if (order == 3) {
          eval3rdEnrichmentFuncs3D(pt, Nr, Nar, Nbr, Ncr);
        }

        // Add the contributions to the the enricment
        for (int i = 0; i < nenrich; i++) {
          TacsScalar d[3];
          d[0] = Nar[i] * J[0] + Nbr[i] * J[1] + Ncr[i] * J[2];
          d[1] = Nar[i] * J[3] + Nbr[i] * J[4] + Ncr[i] * J[5];
          d[2] = Nar[i] * J[6] + Nbr[i] * J[7] + Ncr[i] * J[8];

          A[neq * i + c] = wvals[ii] * wvals[jj] * wvals[kk] * d[0];
          A[neq * i + c + 1] = wvals[ii] * wvals[jj] * wvals[kk] * d[1];
          A[neq * i + c + 2] = wvals[ii] * wvals[jj] * wvals[kk] * d[2];
        }
      }
    }
  }

  // Singular values
  TacsScalar s[MAX_3D_ENRICH];
  int m = neq, n = nenrich;
  double rcond = -1.0;
  int rank;

  // Length of the work array
  int lwork = 512;
  TacsScalar work[512];

  // LAPACK output status
  int info;

  // Using LAPACK, compute the least squares solution
  LAPACKdgelss(&m, &n, &nrhs, A, &m, b, &m, s, &rcond, &rank, work, &lwork,
               &info);

  // Copy over the ubar solution
  for (int i = 0; i < nenrich; i++) {
    for (int j = 0; j < vars_per_node; j++) {
      ubar[vars_per_node * i + j] = b[m * j + i];
    }
  }
}

/*
  Compute the local derivative weights
*/
static void computeLocalWeights(TACSAssembler *tacs, TACSBVec *weights,
                                const int *element_nums = NULL,
                                int num_elements = -1) {
  // Zero the entries in the array
  weights->zeroEntries();

  // Set the local element weights
  int max_nodes = tacs->getMaxElementNodes();
  TacsScalar *welem = new TacsScalar[max_nodes];
  for (int i = 0; i < max_nodes; i++) {
    welem[i] = 1.0;
  }

  if (num_elements < 0) {
    // Add unit weights to all the elements. This will sum up the
    // number of times each node is referenced.
    int nelems = tacs->getNumElements();
    for (int i = 0; i < nelems; i++) {
      int len = 0;
      const int *nodes;
      tacs->getElement(i, &len, &nodes);

      // Compute the local weights
      for (int j = 0; j < len; j++) {
        welem[j] = 1.0;
        if (nodes[j] < 0) {
          welem[j] = 0.0;
        }
      }

      weights->setValues(len, nodes, welem, TACS_ADD_VALUES);
    }
  } else {
    // Add up the weights for only those elements in the list
    for (int i = 0; i < num_elements; i++) {
      int elem = element_nums[i];

      // Get the node numbers for this elements
      int len = 0;
      const int *nodes;
      tacs->getElement(elem, &len, &nodes);

      // Compute the local weights
      for (int j = 0; j < len; j++) {
        welem[j] = 1.0;
        if (nodes[j] < 0) {
          welem[j] = 0.0;
        }
      }

      weights->setValues(len, nodes, welem, TACS_ADD_VALUES);
    }
  }

  delete[] welem;

  // Finish setting all of the values
  weights->beginSetValues(TACS_ADD_VALUES);
  weights->endSetValues(TACS_ADD_VALUES);

  // Distribute the values
  weights->beginDistributeValues();
  weights->endDistributeValues();
}

/*
  Given the input solution, compute and set the derivatives
  in the vector ux. Note that this vector must be 3*

  input:
  TACSAssembler:  the TACSAssembler object
  uvec:           the solution vector
  wlocal:         the local element-weight values

  output:
  uderiv:         the approximate derivatives at the nodes
*/
static void computeNodeDeriv2D(TMRQuadForest *forest, TACSAssembler *tacs,
                               TACSBVec *uvec, TACSBVec *weights,
                               TACSBVec *uderiv, const int *element_nums = NULL,
                               int num_elements = -1) {
  // Zero the nodal derivatives
  uderiv->zeroEntries();

  // Get the interpolation knot positions
  const double *knots;
  const int order = forest->getInterpKnots(&knots);

  // The number of variables at each node
  const int vars_per_node = tacs->getVarsPerNode();

  // Number of derivatives per node - x,y,z derivatives
  const int deriv_per_node = 3 * vars_per_node;

  // Get the number of elements
  int nelems = tacs->getNumElements();

  // We're only going to iterate over the elements in the list, not
  // the entire array
  if (element_nums) {
    nelems = num_elements;
  }

  // Allocate space for the element-wise values and derivatives
  TacsScalar *Ud = new TacsScalar[2 * vars_per_node];
  TacsScalar *uelem = new TacsScalar[order * order * vars_per_node];
  TacsScalar *delem = new TacsScalar[order * order * deriv_per_node];

  // Perform the reconstruction for the local
  for (int index = 0; index < nelems; index++) {
    // Set the element number, depending on whether we're using the
    // full set of elements, or only a subset of the elements
    int elem = index;
    if (element_nums) {
      elem = element_nums[index];
    }

    // Get the element nodes
    int len = 0;
    const int *nodes;
    tacs->getElement(elem, &len, &nodes);

    // Get the local weight values for this element
    TacsScalar welem[MAX_ORDER * MAX_ORDER];
    weights->getValues(len, nodes, welem);

    // Get the local element variables
    uvec->getValues(len, nodes, uelem);

    // Get the node locations for the element
    TacsScalar Xpts[3 * MAX_ORDER * MAX_ORDER];
    tacs->getElement(elem, Xpts);

    // Compute the derivative of the components of the variables along
    // each of the 3-coordinate directions
    TacsScalar *d = delem;

    // Compute the contributions to the derivative from this side of
    // the element
    for (int jj = 0; jj < order; jj++) {
      for (int ii = 0; ii < order; ii++) {
        double pt[2];
        pt[0] = knots[ii];
        pt[1] = knots[jj];

        // Evaluate the the quadratic shape functions at this point
        double N[MAX_ORDER * MAX_ORDER];
        double Na[MAX_ORDER * MAX_ORDER], Nb[MAX_ORDER * MAX_ORDER];
        forest->evalInterp(pt, N, Na, Nb);

        // Evaluate the Jacobian transformation at this point
        TacsScalar Xd[9], J[9];
        computeJacobianTrans2D(Xpts, Na, Nb, Xd, J, order * order);

        // Compute the derivatives from the interpolated solution
        memset(Ud, 0, 2 * vars_per_node * sizeof(TacsScalar));
        for (int k = 0; k < vars_per_node; k++) {
          const TacsScalar *ue = &uelem[k];
          for (int i = 0; i < order * order; i++) {
            Ud[2 * k] += ue[0] * Na[i];
            Ud[2 * k + 1] += ue[0] * Nb[i];
            ue += vars_per_node;
          }
        }

        // Evaluate the x/y/z derivatives of each value at the
        // independent nodes
        TacsScalar winv = 1.0 / welem[ii + jj * order];
        if (nodes[ii + jj * order] >= 0) {
          for (int k = 0; k < vars_per_node; k++) {
            d[0] = winv * (Ud[2 * k] * J[0] + Ud[2 * k + 1] * J[1]);
            d[1] = winv * (Ud[2 * k] * J[3] + Ud[2 * k + 1] * J[4]);
            d[2] = winv * (Ud[2 * k] * J[6] + Ud[2 * k + 1] * J[7]);
            d += 3;
          }
        } else {
          for (int k = 0; k < vars_per_node; k++) {
            d[0] = d[1] = d[2] = 0.0;
            d += 3;
          }
        }
      }
    }

    // Add the values of the derivatives
    uderiv->setValues(len, nodes, delem, TACS_ADD_VALUES);
  }

  // Free the element values
  delete[] Ud;
  delete[] uelem;
  delete[] delem;

  // Add the values in parallel
  uderiv->beginSetValues(TACS_ADD_VALUES);
  uderiv->endSetValues(TACS_ADD_VALUES);

  // Distribute the values so that we can call getValues
  uderiv->beginDistributeValues();
  uderiv->endDistributeValues();
}

/*
  Given the input solution, compute and set the derivatives
  in the vector ux. Note that this vector must be 3*

  input:
  TACSAssembler:  the TACSAssembler object
  uvec:           the solution vector
  wlocal:         the local element-weight values

  output:
  uderiv:         the approximate derivatives at the nodes
*/
static void computeNodeDeriv3D(TMROctForest *forest, TACSAssembler *tacs,
                               TACSBVec *uvec, TACSBVec *weights,
                               TACSBVec *uderiv, const int *element_nums = NULL,
                               int num_elements = -1) {
  // Zero the nodal derivatives
  uderiv->zeroEntries();

  // Get the interpolation knot positions
  const double *knots;
  const int order = forest->getInterpKnots(&knots);

  // The number of variables at each node
  const int vars_per_node = tacs->getVarsPerNode();

  // Number of derivatives per node - x,y,z derivatives
  const int deriv_per_node = 3 * vars_per_node;

  // Get the number of elements
  int nelems = tacs->getNumElements();

  // We're only going to iterate over the elements in the list, not
  // the entire array
  if (element_nums) {
    nelems = num_elements;
  }

  // Allocate space for the element-wise values and derivatives
  TacsScalar *Ud = new TacsScalar[3 * vars_per_node];
  TacsScalar *uelem = new TacsScalar[order * order * order * vars_per_node];
  TacsScalar *delem = new TacsScalar[order * order * order * deriv_per_node];

  // Perform the reconstruction for the local
  for (int index = 0; index < nelems; index++) {
    // Set the element number, depending on whether we're using the
    // full set of elements, or only a subset of the elements
    int elem = index;
    if (element_nums) {
      elem = element_nums[index];
    }

    // Get the element nodes
    int len = 0;
    const int *nodes = NULL;
    tacs->getElement(elem, &len, &nodes);

    // Get the local weight values for this element
    TacsScalar welem[MAX_ORDER * MAX_ORDER * MAX_ORDER];
    weights->getValues(len, nodes, welem);

    // Get the local element variables
    uvec->getValues(len, nodes, uelem);

    // Get the node locations for the element
    TacsScalar Xpts[3 * MAX_ORDER * MAX_ORDER * MAX_ORDER];
    tacs->getElement(elem, Xpts);

    // Compute the derivative of the components of the variables along
    // each of the 3-coordinate directions
    TacsScalar *d = delem;

    // Compute the contributions to the derivative from this side of
    // the element
    for (int kk = 0; kk < order; kk++) {
      for (int jj = 0; jj < order; jj++) {
        for (int ii = 0; ii < order; ii++) {
          double pt[3];
          pt[0] = knots[ii];
          pt[1] = knots[jj];
          pt[2] = knots[kk];

          // Evaluate the the shape functions
          double N[MAX_ORDER * MAX_ORDER * MAX_ORDER];
          double Na[MAX_ORDER * MAX_ORDER * MAX_ORDER];
          double Nb[MAX_ORDER * MAX_ORDER * MAX_ORDER];
          double Nc[MAX_ORDER * MAX_ORDER * MAX_ORDER];
          forest->evalInterp(pt, N, Na, Nb, Nc);

          // Evaluate the Jacobian transformation at this point
          TacsScalar Xd[9], J[9];
          computeJacobianTrans3D(Xpts, Na, Nb, Nc, Xd, J,
                                 order * order * order);

          // Compute the derivatives from the interpolated solution
          memset(Ud, 0, 3 * vars_per_node * sizeof(TacsScalar));
          for (int k = 0; k < vars_per_node; k++) {
            const TacsScalar *ue = &uelem[k];
            for (int i = 0; i < order * order * order; i++) {
              Ud[3 * k] += ue[0] * Na[i];
              Ud[3 * k + 1] += ue[0] * Nb[i];
              Ud[3 * k + 2] += ue[0] * Nc[i];
              ue += vars_per_node;
            }
          }

          // Evaluate the x/y/z derivatives of each value at the
          // independent nodes
          TacsScalar winv = 1.0 / welem[ii + jj * order + kk * order * order];
          if (nodes[ii + jj * order + kk * order * order] >= 0) {
            for (int k = 0; k < vars_per_node; k++) {
              d[0] = winv * (Ud[3 * k] * J[0] + Ud[3 * k + 1] * J[1] +
                             Ud[3 * k + 2] * J[2]);
              d[1] = winv * (Ud[3 * k] * J[3] + Ud[3 * k + 1] * J[4] +
                             Ud[3 * k + 2] * J[5]);
              d[2] = winv * (Ud[3 * k] * J[6] + Ud[3 * k + 1] * J[7] +
                             Ud[3 * k + 2] * J[8]);
              d += 3;
            }
          } else {
            for (int k = 0; k < vars_per_node; k++) {
              d[0] = d[1] = d[2] = 0.0;
              d += 3;
            }
          }
        }
      }
    }

    // Add the values of the derivatives
    uderiv->setValues(len, nodes, delem, TACS_ADD_VALUES);
  }

  // Free the element values
  delete[] Ud;
  delete[] uelem;
  delete[] delem;

  // Add the values in parallel
  uderiv->beginSetValues(TACS_ADD_VALUES);
  uderiv->endSetValues(TACS_ADD_VALUES);

  // Distribute the values so that we can call getValues
  uderiv->beginDistributeValues();
  uderiv->endDistributeValues();
}

/*
  Reconstruct the solution on a more refined mesh
*/
void addRefinedSolution2D(TMRQuadForest *forest, TACSAssembler *tacs,
                          TMRQuadForest *forest_refined,
                          TACSAssembler *tacs_refined, TACSBVec *vec,
                          TACSBVec *vecDeriv, TACSBVec *vec_refined,
                          const int compute_difference = 0,
                          const int *element_nums = NULL,
                          int num_elements = -1) {
  // Number of variables/derivatives per node
  const int vars_per_node = tacs->getVarsPerNode();
  const int deriv_per_node = 3 * vars_per_node;

  // Get the order of the solution and the refined solution
  const double *knots, *refined_knots;
  const int order = forest->getInterpKnots(&knots);
  const int refined_order = forest_refined->getInterpKnots(&refined_knots);
  const int num_nodes = order * order;
  const int num_refined_nodes = refined_order * refined_order;

  // The number of enrichment functions
  const int nenrich = getNum2dEnrich(order);

  // The number of equations for the reconstruction: 2 times the
  // number of nodes for each element
  const int neq = 2 * order * order;

  // Allocate space for the element reconstruction problem
  TacsScalar *tmp = new TacsScalar[neq * (nenrich + vars_per_node)];

  // Element solution on the coarse TACS mesh
  TacsScalar *uelem = new TacsScalar[vars_per_node * num_nodes];
  TacsScalar *delem = new TacsScalar[deriv_per_node * num_nodes];
  TacsScalar *ubar = new TacsScalar[vars_per_node * nenrich];

  // Refined element solution
  TacsScalar *uref = new TacsScalar[vars_per_node * num_refined_nodes];

  // The maximum number of nodes for any element
  TacsScalar Xpts[3 * MAX_ORDER * MAX_ORDER];

  // Number of local elements in the coarse version of TACS
  int nelems = tacs->getNumElements();
  if (element_nums) {
    nelems = num_elements;
  }

  for (int index = 0; index < nelems; index++) {
    // Get the element number
    int elem = index;
    if (element_nums) {
      elem = element_nums[index];
    }

    // Get the node numbers and node locations for this element
    int len;
    const int *nodes;
    tacs->getElement(elem, &len, &nodes);

    // Get the derivatives at the nodes
    vec->getValues(len, nodes, uelem);
    vecDeriv->getValues(len, nodes, delem);

    // Get the node locateions
    tacs_refined->getElement(elem, Xpts);

    // Compute the reconstruction weights for the enrichment functions
    computeElemRecon2D(vars_per_node, forest, forest_refined, Xpts, uelem,
                       delem, ubar, tmp);

    if (compute_difference) {
      // Get the refined element nodes
      const int *refined_nodes;
      tacs_refined->getElement(elem, &len, &refined_nodes);

      // Zero the refined element contribution
      memset(uref, 0, vars_per_node * num_refined_nodes * sizeof(TacsScalar));

      // Compute the solution at the refined points
      for (int m = 0; m < refined_order; m++) {
        for (int n = 0; n < refined_order; n++) {
          // Set the new parameter point in the refined element
          double pt[2];
          pt[0] = refined_knots[n];
          pt[1] = refined_knots[m];

          // Evaluate the shape functions and the enrichment
          // functions at the new parametric point
          double Nr[MAX_2D_ENRICH];
          evalEnrichmentFuncs2D(order, pt, knots, Nr);

          // Add the portion from the enrichment functions
          for (int i = 0; i < vars_per_node; i++) {
            const TacsScalar *ue = &ubar[i];
            TacsScalar *u = &uref[vars_per_node * (n + refined_order * m) + i];

            for (int k = 0; k < nenrich; k++) {
              u[0] += Nr[k] * ue[vars_per_node * k];
            }
          }
        }
      }

      // Zero the contribution if it goes to a dependent node
      for (int i = 0; i < num_refined_nodes; i++) {
        if (refined_nodes[i] < 0) {
          for (int j = 0; j < vars_per_node; j++) {
            uref[vars_per_node * i + j] = 0.0;
          }
        }
      }

      // Add the contributions to the element
      vec_refined->setValues(len, refined_nodes, uref, TACS_ADD_VALUES);
    } else {
      // Get the refined element nodes
      const int *refined_nodes;
      tacs_refined->getElement(elem, &len, &refined_nodes);

      // Zero the refined element contribution
      memset(uref, 0, vars_per_node * num_refined_nodes * sizeof(TacsScalar));

      // Compute the element order
      for (int m = 0; m < refined_order; m++) {
        for (int n = 0; n < refined_order; n++) {
          // Set the new parameter point in the refined element
          double pt[2];
          pt[0] = refined_knots[n];
          pt[1] = refined_knots[m];

          // Evaluate the shape functions
          double N[MAX_ORDER * MAX_ORDER];
          forest->evalInterp(pt, N);

          // Set the values of the variables at this point
          for (int i = 0; i < vars_per_node; i++) {
            const TacsScalar *ue = &uelem[i];
            TacsScalar *u = &uref[vars_per_node * (n + refined_order * m) + i];

            for (int k = 0; k < num_nodes; k++) {
              u[0] += N[k] * ue[0];
              ue += vars_per_node;
            }
          }

          // Evaluate the enrichment functions and add them to the
          // solution
          double Nr[MAX_2D_ENRICH];
          evalEnrichmentFuncs2D(order, pt, knots, Nr);

          // Add the portion from the enrichment functions
          for (int i = 0; i < vars_per_node; i++) {
            const TacsScalar *ue = &ubar[i];
            TacsScalar *u = &uref[vars_per_node * (n + refined_order * m) + i];

            for (int k = 0; k < nenrich; k++) {
              u[0] += Nr[k] * ue[vars_per_node * k];
            }
          }
        }
      }

      // Zero the contribution if it goes to a dependent node
      for (int i = 0; i < num_refined_nodes; i++) {
        if (refined_nodes[i] < 0) {
          for (int j = 0; j < vars_per_node; j++) {
            uref[vars_per_node * i + j] = 0.0;
          }
        }
      }

      // Add the contributions to the element
      vec_refined->setValues(len, refined_nodes, uref, TACS_ADD_VALUES);
    }
  }

  // Free allocated data
  delete[] tmp;
  delete[] uelem;
  delete[] delem;
  delete[] ubar;
  delete[] uref;
}

/*
  Reconstruct the solution on a more refined mesh
*/
void addRefinedSolution3D(TMROctForest *forest, TACSAssembler *tacs,
                          TMROctForest *refined_forest,
                          TACSAssembler *refined_tacs, TACSBVec *vec,
                          TACSBVec *vecDeriv, TACSBVec *vec_refined,
                          const int compute_difference = 0,
                          const int *element_nums = NULL,
                          int num_elements = -1) {
  // Number of variables/derivatives per node
  const int vars_per_node = tacs->getVarsPerNode();
  const int deriv_per_node = 3 * vars_per_node;

  // Get the order of the solution and the refined solution
  const double *knots, *refined_knots;
  const int order = forest->getInterpKnots(&knots);
  const int refined_order = refined_forest->getInterpKnots(&refined_knots);
  const int num_refined_nodes = refined_order * refined_order * refined_order;

  // The number of enrichment functions
  const int nenrich = getNum3dEnrich(order);

  // The number of equations for the reconstruction: 2 times the
  // number of nodes for each element
  const int neq = 3 * order * order * order;

  // Allocate space for the element reconstruction problem
  TacsScalar *tmp = new TacsScalar[neq * (nenrich + vars_per_node)];

  // Element solution on the coarse TACS mesh
  TacsScalar *uelem = new TacsScalar[vars_per_node * order * order * order];
  TacsScalar *delem = new TacsScalar[deriv_per_node * order * order * order];
  TacsScalar *ubar = new TacsScalar[vars_per_node * nenrich];

  // Refined element solution
  TacsScalar *uref = new TacsScalar[vars_per_node * order * order * order];

  // The maximum number of nodes for any element
  TacsScalar Xpts[3 * MAX_ORDER * MAX_ORDER * MAX_ORDER];

  // Number of local elements in the coarse version of TACS
  int nelems = tacs->getNumElements();
  if (element_nums) {
    nelems = num_elements;
  }

  for (int index = 0; index < nelems; index++) {
    // Get the element number
    int elem = index;
    if (element_nums) {
      elem = element_nums[index];
    }

    // Get the node numbers for this element
    int len;
    const int *nodes;
    tacs->getElement(elem, &len, &nodes);

    // Get the derivatives at the nodes
    vec->getValues(len, nodes, uelem);
    vecDeriv->getValues(len, nodes, delem);

    // Get the refined node locations
    refined_tacs->getElement(elem, Xpts);

    // Compute the reconstruction weights for the enrichment functions
    computeElemRecon3D(vars_per_node, forest, refined_forest, Xpts, uelem,
                       delem, ubar, tmp);

    if (compute_difference) {
      // Get the refined element nodes
      const int *refined_nodes;
      refined_tacs->getElement(elem, &len, &refined_nodes);

      // Zero the refined element contribution
      memset(uref, 0, vars_per_node * num_refined_nodes * sizeof(TacsScalar));

      for (int p = 0; p < refined_order; p++) {
        for (int m = 0; m < refined_order; m++) {
          for (int n = 0; n < refined_order; n++) {
            // Set the new parameter point in the refined element
            double pt[3];
            pt[0] = refined_knots[n];
            pt[1] = refined_knots[m];
            pt[2] = refined_knots[p];

            // Evaluate the shape functions and the enrichment
            // functions at the new parametric point
            double Nr[MAX_3D_ENRICH];
            if (order == 2) {
              eval2ndEnrichmentFuncs3D(pt, Nr);
            } else if (order == 3) {
              eval3rdEnrichmentFuncs3D(pt, Nr);
            }

            // Add the portion from the enrichment functions
            int offset =
                (n + refined_order * m + refined_order * refined_order * p);
            TacsScalar *u = &uref[vars_per_node * offset];
            for (int i = 0; i < vars_per_node; i++) {
              const TacsScalar *ue = &ubar[i];
              for (int k = 0; k < nenrich; k++) {
                u[i] += Nr[k] * ue[vars_per_node * k];
              }
            }
          }
        }
      }

      // Zero the contribution if it goes to a dependent node
      for (int i = 0; i < num_refined_nodes; i++) {
        if (refined_nodes[i] < 0) {
          for (int j = 0; j < vars_per_node; j++) {
            uref[vars_per_node * i + j] = 0.0;
          }
        }
      }

      // Add the contributions to the element
      vec_refined->setValues(len, refined_nodes, uref, TACS_ADD_VALUES);
    } else {
      // Get the refined element nodes
      const int *refined_nodes;
      refined_tacs->getElement(elem, &len, &refined_nodes);

      // Zero the refined element contribution
      memset(uref, 0, vars_per_node * num_refined_nodes * sizeof(TacsScalar));

      for (int p = 0; p < refined_order; p++) {
        for (int m = 0; m < refined_order; m++) {
          for (int n = 0; n < refined_order; n++) {
            // Set the new parameter point in the refined element
            double pt[3];
            pt[0] = refined_knots[n];
            pt[1] = refined_knots[m];
            pt[2] = refined_knots[p];

            // Evaluate the shape functions and the enrichment
            // functions at the new parametric point
            double N[MAX_ORDER * MAX_ORDER * MAX_ORDER];
            forest->evalInterp(pt, N);

            // Add the portion from the enrichment functions
            int offset =
                (n + refined_order * m + refined_order * refined_order * p);
            TacsScalar *u = &uref[vars_per_node * offset];

            for (int i = 0; i < vars_per_node; i++) {
              const TacsScalar *ue = &uelem[i];
              for (int k = 0; k < order * order * order; k++) {
                u[i] += N[k] * ue[vars_per_node * k];
              }
            }

            double Nr[MAX_3D_ENRICH];
            if (order == 2) {
              eval2ndEnrichmentFuncs3D(pt, Nr);
            } else if (order == 3) {
              eval3rdEnrichmentFuncs3D(pt, Nr);
            }

            // Add the portion from the enrichment functions
            for (int i = 0; i < vars_per_node; i++) {
              const TacsScalar *ue = &ubar[i];
              for (int k = 0; k < nenrich; k++) {
                u[i] += Nr[k] * ue[vars_per_node * k];
              }
            }
          }
        }
      }

      // Zero the contribution if it goes to a dependent node
      for (int i = 0; i < num_refined_nodes; i++) {
        if (refined_nodes[i] < 0) {
          for (int j = 0; j < vars_per_node; j++) {
            uref[vars_per_node * i + j] = 0.0;
          }
        }
      }

      // Add the contributions to the element
      vec_refined->setValues(len, refined_nodes, uref, TACS_ADD_VALUES);
    }
  }

  // Free allocated data
  delete[] tmp;
  delete[] uelem;
  delete[] delem;
  delete[] ubar;
  delete[] uref;
}

/*
  Use Newton's method to find the closest point
*/
int inverseEvalPoint(const TacsScalar Xp[], const TacsScalar Xpts[],
                     TMRQuadForest *forest, double pt[],
                     const int max_iterations = 20,
                     const double eps_dist = 1e-8,
                     const double eps_cosine = 1e-8) {
  // Get the order of the forest
  const int order = forest->getMeshOrder();

  // Find the closes point
  for (int iter = 0; iter < max_iterations; iter++) {
    double N[MAX_ORDER * MAX_ORDER];
    double N1[MAX_ORDER * MAX_ORDER], N2[MAX_ORDER * MAX_ORDER];
    double N11[MAX_ORDER * MAX_ORDER], N22[MAX_ORDER * MAX_ORDER],
        N12[MAX_ORDER * MAX_ORDER];

    // Evaluate the shape functions
    forest->evalInterp(pt, N, N1, N2, N11, N22, N12);

    // Set the point and their derivatives
    TMRPoint X, Xu, Xv, Xuu, Xuv, Xvv;
    X.zero();
    Xu.zero();
    Xv.zero();
    Xuu.zero();
    Xuv.zero();
    Xvv.zero();

    // Evaluate the points and their derivatives
    for (int i = 0; i < order * order; i++) {
      const TacsScalar x = Xpts[3 * i];
      const TacsScalar y = Xpts[3 * i + 1];
      const TacsScalar z = Xpts[3 * i + 2];

      // Node location
      X.x += N[i] * x;
      X.y += N[i] * y;
      X.z += N[i] * z;

      // First derivatives
      Xu.x += N1[i] * x;
      Xu.y += N1[i] * y;
      Xu.z += N1[i] * z;
      Xv.x += N2[i] * x;
      Xv.y += N2[i] * y;
      Xv.z += N2[i] * z;

      // Second derivatives
      Xuu.x += N11[i] * x;
      Xuu.y += N11[i] * y;
      Xuu.z += N11[i] * z;
      Xvv.x += N22[i] * x;
      Xvv.y += N22[i] * y;
      Xvv.z += N22[i] * z;
      Xuv.x += N12[i] * x;
      Xuv.y += N12[i] * y;
      Xuv.z += N12[i] * z;
    }

    // Compute the difference between the position on the surface and
    // the point
    TMRPoint r;
    r.x = (X.x - Xp[0]);
    r.y = (X.y - Xp[1]);
    r.z = (X.z - Xp[2]);

    // Compute the residual
    double ru = Xu.dot(r);
    double rv = Xv.dot(r);

    // Compute the elements of the Jacobian matrix
    double Juu = Xuu.dot(r) + Xu.dot(Xu);
    double Juv = Xuv.dot(r) + Xu.dot(Xv);
    double Jvv = Xvv.dot(r) + Xv.dot(Xv);

    // The increments along the parametric directions
    double du = 0.0, dv = 0.0;

    // Solve the 2x2 system
    double det = Juu * Jvv - Juv * Juv;
    if (det != 0.0) {
      du = (Jvv * ru - Juv * rv) / det;
      dv = (Juu * rv - Juv * ru) / det;
    }

    // Compute the updates
    pt[0] = pt[0] - du;
    pt[1] = pt[1] - dv;

    // Check if the convergence test is satisfied
    if (fabs(r.x) < eps_dist && fabs(r.y) < eps_dist && fabs(r.z) < eps_dist) {
      return 0;
    }

    // Perform the cosine check
    double dotr = r.dot(r);
    double dotu = Xu.dot(Xu);
    double dotv = Xv.dot(Xv);
    if (ru * ru < eps_cosine * eps_cosine * dotu * dotr &&
        rv * rv < eps_cosine * eps_cosine * dotv * dotr) {
      return 0;
    }
  }

  return 1;
}

/*
  Compute the interpolated solution on the order-elevated mesh
*/
void TMR_ComputeInterpSolution(TMRQuadForest *forest, TACSAssembler *tacs,
                               TMRQuadForest *forest_refined,
                               TACSAssembler *tacs_refined, TACSBVec *_uvec,
                               TACSBVec *_uvec_refined) {
  // The maximum number of nodes
  const int max_num_nodes = MAX_ORDER * MAX_ORDER;

  // Get the order of the mesh
  const double *refined_knots;
  const int order = forest->getInterpKnots(NULL);
  const int refined_order = forest_refined->getInterpKnots(&refined_knots);
  const int num_nodes = order * order;
  const int num_refined_nodes = refined_order * refined_order;

  // Get the number of elements
  const int nelems = tacs->getNumElements();
  const int vars_per_node = tacs->getVarsPerNode();

  // Retrieve the variables from the TACSAssembler object
  TACSBVec *uvec = _uvec;
  if (!uvec) {
    uvec = tacs->createVec();
    uvec->incref();
    tacs->getVariables(uvec);
  }
  TACSBVec *uvec_refined = _uvec_refined;
  if (!uvec_refined) {
    uvec_refined = tacs_refined->createVec();
    uvec_refined->incref();
  }

  // Zero the entries of the reconstructed solution
  uvec_refined->zeroEntries();

  // Distribute the solution vector answer
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Allocate space for the interpolation
  TacsScalar *vars_elem = new TacsScalar[vars_per_node * num_nodes];
  TacsScalar *vars_interp = new TacsScalar[vars_per_node * num_refined_nodes];

  // Loop over all the elements in the refined mesh (same number as
  // the original mesh)
  for (int elem = 0; elem < nelems; elem++) {
    // Get the element unknowns for the coarse mesh
    int len = 0;
    const int *nodes;
    tacs->getElement(elem, &len, &nodes);

    // Get the node locations for the coarse mesh
    TacsScalar Xpts[3 * max_num_nodes], Xpts_refined[3 * max_num_nodes];
    tacs->getElement(elem, Xpts);
    tacs_refined->getElement(elem, Xpts_refined);

    // For each element, interpolate the solution
    memset(vars_interp, 0,
           vars_per_node * num_refined_nodes * sizeof(TacsScalar));

    // Get the element variables
    uvec->getValues(len, nodes, vars_elem);

    // Perform the interpolation
    for (int m = 0; m < refined_order; m++) {
      for (int n = 0; n < refined_order; n++) {
        double pt[2];
        pt[0] = refined_knots[n];
        pt[1] = refined_knots[m];

        // Evaluate the shape functions
        double N[max_num_nodes];
        forest->evalInterp(pt, N);

        // Evaluate the interpolation
        int offset = n + m * refined_order;
        TacsScalar *v = &vars_interp[vars_per_node * offset];

        for (int k = 0; k < num_nodes; k++) {
          for (int kk = 0; kk < vars_per_node; kk++) {
            v[kk] += vars_elem[vars_per_node * k + kk] * N[k];
          }
        }
      }
    }

    // Get the element unknowns for the refined mesh
    int refined_len = 0;
    const int *refined_nodes;
    tacs_refined->getElement(elem, &refined_len, &refined_nodes);

    uvec_refined->setValues(refined_len, refined_nodes, vars_interp,
                            TACS_INSERT_NONZERO_VALUES);
  }

  // Free the data
  delete[] vars_elem;
  delete[] vars_interp;

  // Finish setting the values into the nodal error array
  uvec_refined->beginSetValues(TACS_INSERT_NONZERO_VALUES);
  uvec_refined->endSetValues(TACS_INSERT_NONZERO_VALUES);

  uvec_refined->beginDistributeValues();
  uvec_refined->endDistributeValues();

  // The solution was not passed as an argument
  if (!_uvec) {
    uvec->decref();
  }

  // The refined solution vector was not passed as an argument
  if (!_uvec_refined) {
    tacs_refined->setVariables(uvec_refined);
    uvec_refined->decref();
  }
}

/*
  Compute the interpolated solution on the order-elevated mesh
*/
void TMR_ComputeInterpSolution(TMROctForest *forest, TACSAssembler *tacs,
                               TMROctForest *forest_refined,
                               TACSAssembler *tacs_refined, TACSBVec *_uvec,
                               TACSBVec *_uvec_refined) {
  // The maximum number of nodes
  const int max_num_nodes = MAX_ORDER * MAX_ORDER * MAX_ORDER;

  // Get the order of the mesh
  const double *refined_knots;
  const int order = forest->getInterpKnots(NULL);
  const int refined_order = forest_refined->getInterpKnots(&refined_knots);
  const int num_nodes = order * order * order;
  const int num_refined_nodes = refined_order * refined_order * refined_order;

  // Get the number of elements
  const int nelems = tacs->getNumElements();
  const int vars_per_node = tacs->getVarsPerNode();

  // Retrieve the variables from the TACSAssembler object
  TACSBVec *uvec = _uvec;
  if (!uvec) {
    uvec = tacs->createVec();
    uvec->incref();
    tacs->getVariables(uvec);
  }
  TACSBVec *uvec_refined = _uvec_refined;
  if (!uvec_refined) {
    uvec_refined = tacs_refined->createVec();
    uvec_refined->incref();
  }

  // Zero the entries of the reconstructed solution
  uvec_refined->zeroEntries();

  // Distribute the solution vector answer
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Loop over all the elements in the refined mesh (same number as
  // the original mesh)

  // Allocate space for the interpolation
  TacsScalar *vars_elem = new TacsScalar[vars_per_node * num_nodes];
  TacsScalar *vars_interp = new TacsScalar[vars_per_node * num_refined_nodes];

  for (int elem = 0; elem < nelems; elem++) {
    // Get the element unknowns for the coarse mesh
    int len = 0;
    const int *nodes;
    tacs->getElement(elem, &len, &nodes);

    // For each element, interpolate the solution
    memset(vars_interp, 0,
           vars_per_node * num_refined_nodes * sizeof(TacsScalar));

    // Get the element variables
    uvec->getValues(len, nodes, vars_elem);

    // Perform the interpolation
    for (int p = 0; p < refined_order; p++) {
      for (int m = 0; m < refined_order; m++) {
        for (int n = 0; n < refined_order; n++) {
          double pt[3];
          pt[0] = refined_knots[n];
          pt[1] = refined_knots[m];
          pt[2] = refined_knots[p];

          // Evaluate the locations of the new nodes
          double N[max_num_nodes];
          forest->evalInterp(pt, N);

          // Evaluate the interpolation part of the reconstruction
          int offset =
              (n + m * refined_order + p * refined_order * refined_order);
          TacsScalar *v = &vars_interp[vars_per_node * offset];

          for (int k = 0; k < num_nodes; k++) {
            for (int kk = 0; kk < vars_per_node; kk++) {
              v[kk] += vars_elem[vars_per_node * k + kk] * N[k];
            }
          }
        }
      }
    }

    // Get the element unknowns for the refined mesh
    int refined_len = 0;
    const int *refined_nodes;
    tacs_refined->getElement(elem, &refined_len, &refined_nodes);

    uvec_refined->setValues(refined_len, refined_nodes, vars_interp,
                            TACS_INSERT_NONZERO_VALUES);
  }

  // Free the data
  delete[] vars_elem;
  delete[] vars_interp;

  // Finish setting the values into the nodal error array
  uvec_refined->beginSetValues(TACS_INSERT_NONZERO_VALUES);
  uvec_refined->endSetValues(TACS_INSERT_NONZERO_VALUES);

  // The solution was not passed as an argument
  if (!_uvec) {
    uvec->decref();
  }

  // The refined solution vector was not passed as an argument
  if (!_uvec_refined) {
    tacs_refined->setVariables(uvec_refined);
    uvec_refined->decref();
  }
}

/*
  Compute the reconstructed solution on an embedded mesh with elevated
  order
*/
void TMR_ComputeReconSolution(TMRQuadForest *forest, TACSAssembler *tacs,
                              TMRQuadForest *forest_refined,
                              TACSAssembler *tacs_refined, TACSBVec *_uvec,
                              TACSBVec *_uvec_refined,
                              const int compute_difference) {
  // Retrieve the variables from the TACSAssembler object
  TACSBVec *uvec = _uvec;
  if (!uvec) {
    uvec = tacs->createVec();
    uvec->incref();
    tacs->getVariables(uvec);
  }
  TACSBVec *uvec_refined = _uvec_refined;
  if (!uvec_refined) {
    uvec_refined = tacs_refined->createVec();
    uvec_refined->incref();
  }

  // Zero the entries of the reconstructed solution
  uvec_refined->zeroEntries();

  // Distribute the solution vector answer
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Allocate a vector for the derivatives
  int vars_per_node = tacs->getVarsPerNode();
  TACSBVec *uderiv =
      new TACSBVec(tacs->getNodeMap(), 3 * vars_per_node,
                   tacs->getBVecDistribute(), tacs->getBVecDepNodes());
  uderiv->incref();

  // Create the weight vector - the weights are the number of times
  // each node is referenced by adjacent elements, including
  // inter-process references.
  TACSBVec *weights =
      new TACSBVec(tacs->getNodeMap(), 1, tacs->getBVecDistribute(),
                   tacs->getBVecDepNodes());
  weights->incref();

  // Get the underlying topology object
  TMRTopology *topo = forest->getTopology();

  // Compute the max size of the element array
  int nelems = tacs->getNumElements();
  int *face_elem_nums = new int[nelems];

  // Loop over all of the faces and uniquely sort the faces
  int num_faces = topo->getNumFaces();
  std::set<std::string> face_name_set;
  for (int face_num = 0; face_num < num_faces; face_num++) {
    TMRFace *face;
    topo->getFace(face_num, &face);
    const char *name = face->getName();
    if (name) {
      std::string str(name);
      face_name_set.insert(str);
    } else {
      std::string str("");
      face_name_set.insert(str);
    }
  }

  // Loop over all of the faces
  std::set<std::string>::iterator it;
  for (it = face_name_set.begin(); it != face_name_set.end(); it++) {
    // Get the quads with the given face number
    const char *name = NULL;
    if (!(*it).empty()) {
      name = (*it).c_str();
    }
    TMRQuadrantArray *quad_array = forest->getQuadsWithName(name);

    // Get the quadrants for this face
    int num_face_elems;
    TMRQuadrant *array;
    quad_array->getArray(&array, &num_face_elems);

    // Create an array of the element numbers for this face
    for (int i = 0; i < num_face_elems; i++) {
      face_elem_nums[i] = array[i].tag;
    }

    // Free the quadrant array - it is no longer required
    delete quad_array;

    // Compute the nodal weights for the derivatives
    computeLocalWeights(tacs, weights, face_elem_nums, num_face_elems);

    // Compute the nodal derivatives
    computeNodeDeriv2D(forest, tacs, uvec, weights, uderiv, face_elem_nums,
                       num_face_elems);

    // Compute the refined solution
    addRefinedSolution2D(forest, tacs, forest_refined, tacs_refined, uvec,
                         uderiv, uvec_refined, compute_difference,
                         face_elem_nums, num_face_elems);
  }

  // Free the temp array
  delete[] face_elem_nums;

  // Free the weights on the coarse mesh
  weights->decref();

  // Add the values
  uvec_refined->beginSetValues(TACS_ADD_VALUES);
  uvec_refined->endSetValues(TACS_ADD_VALUES);

  // Create a vector for the refined weights
  TACSBVec *weights_refined = new TACSBVec(tacs_refined->getNodeMap(), 1,
                                           tacs_refined->getBVecDistribute(),
                                           tacs_refined->getBVecDepNodes());
  weights_refined->incref();

  // Compute the nodal weights for the refined mesh
  computeLocalWeights(tacs_refined, weights_refined);

  // Multiply every non-dependent entry by the refined weights
  TacsScalar *u, *w;
  uvec_refined->getArray(&u);
  int size = weights_refined->getArray(&w);

  for (int i = 0; i < size; i++) {
    TacsScalar winv = 1.0 / w[i];
    for (int j = 0; j < vars_per_node; j++) {
      u[j] *= winv;
    }
    u += vars_per_node;
  }

  // Free the refined weights
  weights_refined->decref();

  // The solution was not passed as an argument
  if (!_uvec) {
    uvec->decref();
  }

  // The refined solution vector was not passed as an argument
  if (!_uvec_refined) {
    tacs_refined->setVariables(uvec_refined);
    uvec_refined->decref();
  }
}

/*
  Compute the reconstructed solution on an embedded mesh with elevated
  order
*/
void TMR_ComputeReconSolution(TMROctForest *forest, TACSAssembler *tacs,
                              TMROctForest *forest_refined,
                              TACSAssembler *tacs_refined, TACSBVec *_uvec,
                              TACSBVec *_uvec_refined,
                              const int compute_difference) {
  // Retrieve the variables from the TACSAssembler object
  TACSBVec *uvec = _uvec;
  if (!uvec) {
    uvec = tacs->createVec();
    uvec->incref();
    tacs->getVariables(uvec);
  }
  TACSBVec *uvec_refined = _uvec_refined;
  if (!uvec_refined) {
    uvec_refined = tacs_refined->createVec();
    uvec_refined->incref();
  }

  // Zero the entries in the refined vector
  uvec_refined->zeroEntries();

  // Distribute the solution vector answer
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Allocate a vector for the derivatives
  int vars_per_node = tacs->getVarsPerNode();
  TACSBVec *uderiv =
      new TACSBVec(tacs->getNodeMap(), 3 * vars_per_node,
                   tacs->getBVecDistribute(), tacs->getBVecDepNodes());
  uderiv->incref();

  // Create the weight vector - the weights are the number of times
  // each node is referenced by adjacent elements, including
  // inter-process references.
  TACSBVec *weights =
      new TACSBVec(tacs->getNodeMap(), 1, tacs->getBVecDistribute(),
                   tacs->getBVecDepNodes());
  weights->incref();

  // Get the underlying topology object
  TMRTopology *topo = forest->getTopology();

  // Compute the max size of the element array
  int nelems = tacs->getNumElements();
  int *vol_elem_nums = new int[nelems];

  // Loop over all of the vols and uniquely sort the vols
  int num_vols = topo->getNumVolumes();
  std::set<std::string> vol_name_set;
  for (int vol_num = 0; vol_num < num_vols; vol_num++) {
    TMRVolume *vol;
    topo->getVolume(vol_num, &vol);
    const char *name = vol->getName();
    if (name) {
      std::string str(name);
      vol_name_set.insert(str);
    } else {
      std::string str("");
      vol_name_set.insert(str);
    }
  }

  // Loop over all of the volumes
  std::set<std::string>::iterator it;
  for (it = vol_name_set.begin(); it != vol_name_set.end(); it++) {
    // Get the quads with the given vol number
    const char *name = NULL;
    if (!(*it).empty()) {
      name = (*it).c_str();
    }
    TMROctantArray *oct_array = forest->getOctsWithName(name);

    // Get the octants for this volume
    int num_vol_elems;
    TMROctant *array;
    oct_array->getArray(&array, &num_vol_elems);

    // Create an array of the element numbers for this vol
    for (int i = 0; i < num_vol_elems; i++) {
      vol_elem_nums[i] = array[i].tag;
    }

    // Free the quadrant array - it is no longer required
    delete oct_array;

    // Compute the nodal weights for the derivatives
    computeLocalWeights(tacs, weights, vol_elem_nums, num_vol_elems);

    // Compute the nodal derivatives
    computeNodeDeriv3D(forest, tacs, uvec, weights, uderiv, vol_elem_nums,
                       num_vol_elems);

    // Compute the refined solution
    addRefinedSolution3D(forest, tacs, forest_refined, tacs_refined, uvec,
                         uderiv, uvec_refined, compute_difference,
                         vol_elem_nums, num_vol_elems);
  }

  // Free the temp array
  delete[] vol_elem_nums;

  // Free the weights on the coarse mesh
  weights->decref();

  // Add the values
  uvec_refined->beginSetValues(TACS_ADD_VALUES);
  uvec_refined->endSetValues(TACS_ADD_VALUES);

  // Create a vector for the refined weights
  TACSBVec *weights_refined = new TACSBVec(tacs_refined->getNodeMap(), 1,
                                           tacs_refined->getBVecDistribute(),
                                           tacs_refined->getBVecDepNodes());
  weights_refined->incref();

  // Compute the nodal weights for the refined mesh
  computeLocalWeights(tacs_refined, weights_refined);

  TacsScalar *u, *w;
  uvec_refined->getArray(&u);
  int size = weights_refined->getArray(&w);

  for (int i = 0; i < size; i++) {
    TacsScalar winv = 1.0 / w[i];
    for (int j = 0; j < vars_per_node; j++) {
      u[j] *= winv;
    }
    u += vars_per_node;
  }

  // Free the refined weights
  weights_refined->decref();

  // The solution was not passed as an argument
  if (!_uvec) {
    uvec->decref();
  }

  // The refined solution vector was not passed as an argument
  if (!_uvec_refined) {
    tacs_refined->setVariables(uvec_refined);
    uvec_refined->decref();
  }
}

/*
  The following function performs a mesh refinement based on a strain
  energy criteria. It is based on the following relationship for
  linear finite-element analysis

  a(u-uh,u-uh) = a(u,u) - a(uh,uh)

  where a(u,u) is the bilinear strain energy functional, u is the
  exact solution, and uh is the discretized solution at any mesh
  level. This relies on the relationship that a(uh, u - uh) = 0 which
  is satisfied due to the method of Galerkin/Ritz.

  The following function computes a localized error indicator using
  the element-wise strain energy. The code computes a higher-order
  reconstructed solution using a cubic enrichment functions. These
  enrichment functions expand the original solution space and are
  computed based on a least-squares approximation with nodal gradient
  values. The localized error indicator is evaluated as follows:

  err = [sum_{i=1}^{4} ae(uCe, uCe) ] - ae(ue, ue)

  where uCe is the element-wise cuibc element reconstruction projected
  onto a uniformly refined mesh.

  input:
  tacs:        the TACSAssembler object
  forest:      the forest of quadtrees

  returns:     predicted strain energy error
*/
double TMR_StrainEnergyErrorEst(TMRQuadForest *forest, TACSAssembler *tacs,
                                TMRQuadForest *forest_refined,
                                TACSAssembler *tacs_refined, double *error) {
  // The maximum number of nodes
  const int max_num_nodes = MAX_ORDER * MAX_ORDER;

  // Get the order of the original mesh and the number of enrichment
  // functions associated with the order
  const double *knots;
  const int order = forest->getInterpKnots(&knots);
  const int nenrich = getNum2dEnrich(order);

  // Get the refined order of the mesh
  const double *refined_knots;
  const int refined_order = forest_refined->getInterpKnots(&refined_knots);
  const int num_refined_nodes = refined_order * refined_order;

  // Get the number of variables per node
  const int vars_per_node = tacs->getVarsPerNode();
  const int deriv_per_node = 3 * vars_per_node;

  // The number of equations: 2 times the number of nodes for each element
  const int neq = 2 * order * order;

  // Number of local elements
  const int nelems = tacs->getNumElements();

  // Allocate space for the element reconstruction problem
  TacsScalar *tmp = new TacsScalar[neq * (nenrich + vars_per_node)];
  TacsScalar *ubar = new TacsScalar[vars_per_node * nenrich];
  TacsScalar *delem = new TacsScalar[deriv_per_node * order * order];

  // Allocate arrays needed for the reconstruction
  TacsScalar *vars_elem = new TacsScalar[vars_per_node * order * order];

  // The interpolated variables on the refined mesh
  TacsScalar *dvars = new TacsScalar[vars_per_node * num_refined_nodes];
  TacsScalar *vars_interp = new TacsScalar[vars_per_node * num_refined_nodes];

  // Zero the refined nodes
  memset(dvars, 0, vars_per_node * num_refined_nodes * sizeof(TacsScalar));

  // Get the communicator
  MPI_Comm comm = tacs->getMPIComm();

  // Retrieve the variables from the TACSAssembler object
  TACSBVec *uvec = tacs->createVec();
  uvec->incref();

  tacs->getVariables(uvec);
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Create the weight vector - the weights are the number of times
  // each node is referenced by adjacent elements, including
  // inter-process references.
  TACSBVec *weights =
      new TACSBVec(tacs->getNodeMap(), 1, tacs->getBVecDistribute(),
                   tacs->getBVecDepNodes());
  weights->incref();
  computeLocalWeights(tacs, weights);

  // Compute the nodal derivatives
  TACSBVec *uderiv =
      new TACSBVec(tacs->getNodeMap(), 3 * vars_per_node,
                   tacs->getBVecDistribute(), tacs->getBVecDepNodes());
  uderiv->incref();
  computeNodeDeriv2D(forest, tacs, uvec, weights, uderiv);
  weights->decref();

  // Keep track of the total error
  TacsScalar SE_total_error = 0.0;

  for (int i = 0; i < nelems; i++) {
    // The simulation time -- we assume time-independent analysis
    double time = 0.0;

    // Get the variables for this element on the coarse mesh
    tacs->getElement(i, NULL, vars_elem);

    // Get the node numbers for this element
    int len;
    const int *nodes;
    tacs->getElement(i, &len, &nodes);

    // Compute the solution on the refined mesh
    uderiv->getValues(len, nodes, delem);

    // Get the refined node locations
    TacsScalar Xpts[3 * max_num_nodes];
    TACSElement *elem = tacs_refined->getElement(i, Xpts);

    // Compute the enrichment functions for each degree of freedom
    computeElemRecon2D(vars_per_node, forest, forest_refined, Xpts, vars_elem,
                       delem, ubar, tmp);

    // Set the variables to zero
    memset(vars_interp, 0,
           vars_per_node * num_refined_nodes * sizeof(TacsScalar));

    // Evaluate the interpolation on the refined mesh
    for (int m = 0; m < refined_order; m++) {
      for (int n = 0; n < refined_order; n++) {
        double pt[2];
        pt[0] = refined_knots[n];
        pt[1] = refined_knots[m];

        // Add the contribution from the enrichment functions
        double Nr[MAX_2D_ENRICH];
        evalEnrichmentFuncs2D(order, pt, knots, Nr);

        // Add the portion from the enrichment functions
        for (int k = 0; k < nenrich; k++) {
          // Evaluate the interpolation part of the reconstruction
          for (int kk = 0; kk < vars_per_node; kk++) {
            vars_interp[vars_per_node * (n + m * refined_order) + kk] +=
                ubar[vars_per_node * k + kk] * Nr[k];
          }
        }
      }
    }

    // Compute the strain/potential energy
    TacsScalar Te, Pe;
    elem->computeEnergies(i, time, Xpts, vars_interp, dvars, &Te, &Pe);
    error[i] = fabs(TacsRealPart(Pe));

    // Add up the total error
    SE_total_error += error[i];
  }

  // Count up the total strain energy
  double SE_temp = 0.0;
  MPI_Allreduce(&SE_total_error, &SE_temp, 1, MPI_DOUBLE, MPI_SUM, comm);
  SE_total_error = SE_temp;

  // Free the global vectors
  uvec->decref();
  uderiv->decref();

  // Free the element-related data
  delete[] tmp;
  delete[] ubar;
  delete[] delem;
  delete[] vars_elem;
  delete[] dvars;
  delete[] vars_interp;

  // Return the error
  return SE_total_error;
}

/*
  The following function performs a mesh refinement based on the strain
  energy criteria.

  This is the equivalent of the TMR_StrainEnergyRefine function for
  quadtrees.
*/
double TMR_StrainEnergyErrorEst(TMROctForest *forest, TACSAssembler *tacs,
                                TMROctForest *refined_forest,
                                TACSAssembler *refined_tacs, double *error) {
  // The maximum number of nodes
  const int max_num_nodes = MAX_ORDER * MAX_ORDER * MAX_ORDER;

  // Get the order of the original mesh and the number of enrichment
  // functions associated with the order
  const double *knots;
  const int order = forest->getInterpKnots(&knots);
  const int nenrich = getNum3dEnrich(order);

  // Get the refined order of the mesh
  const double *refined_knots;
  const int refined_order = refined_forest->getInterpKnots(&refined_knots);
  const int num_nodes = order * order * order;
  const int num_refined_nodes = refined_order * refined_order * refined_order;

  // Get the number of variables per node
  const int vars_per_node = tacs->getVarsPerNode();
  const int deriv_per_node = 3 * vars_per_node;

  // The number of equations: 3 times the number of nodes for each element
  const int neq = 3 * order * order * order;

  // Number of local elements
  const int nelems = tacs->getNumElements();

  // Allocate space for the element reconstruction problem
  TacsScalar *tmp = new TacsScalar[neq * (nenrich + vars_per_node)];
  TacsScalar *ubar = new TacsScalar[vars_per_node * nenrich];
  TacsScalar *delem = new TacsScalar[deriv_per_node * num_nodes];

  // Allocate arrays needed for the reconstruction
  TacsScalar *vars_elem = new TacsScalar[vars_per_node * num_nodes];

  // The interpolated variables on the refined mesh
  TacsScalar *dvars = new TacsScalar[vars_per_node * num_refined_nodes];
  TacsScalar *vars_interp = new TacsScalar[vars_per_node * num_refined_nodes];

  // Get the communicator
  MPI_Comm comm = tacs->getMPIComm();

  // Retrieve the variables from the TACSAssembler object
  TACSBVec *uvec = tacs->createVec();
  uvec->incref();

  tacs->getVariables(uvec);
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Create the weight vector - the weights are the number of times
  // each node is referenced by adjacent elements, including
  // inter-process references.
  TACSBVec *weights =
      new TACSBVec(tacs->getNodeMap(), 1, tacs->getBVecDistribute(),
                   tacs->getBVecDepNodes());
  weights->incref();
  computeLocalWeights(tacs, weights);

  // Compute the nodal derivatives
  TACSBVec *uderiv =
      new TACSBVec(tacs->getNodeMap(), 3 * vars_per_node,
                   tacs->getBVecDistribute(), tacs->getBVecDepNodes());
  uderiv->incref();
  computeNodeDeriv3D(forest, tacs, uvec, weights, uderiv);
  weights->decref();

  // Keep track of the total error
  double SE_total_error = 0.0;

  for (int i = 0; i < nelems; i++) {
    // The simulation time -- we assume time-independent analysis
    double time = 0.0;

    // Get the node numbers for this element
    int len;
    const int *nodes;
    tacs->getElement(i, &len, &nodes);

    // Get the values of the derivatives at the nodes
    uderiv->getValues(len, nodes, delem);

    // Get the node locations on the new mesh
    TacsScalar Xpts[3 * max_num_nodes];
    TACSElement *elem = refined_tacs->getElement(i, Xpts);

    // Compute the enrichment functions for each degree of freedom
    computeElemRecon3D(vars_per_node, forest, refined_forest, Xpts, vars_elem,
                       delem, ubar, tmp);

    // Set the variables to zero
    memset(vars_interp, 0,
           vars_per_node * num_refined_nodes * sizeof(TacsScalar));

    for (int p = 0; p < refined_order; p++) {
      for (int m = 0; m < refined_order; m++) {
        for (int n = 0; n < refined_order; n++) {
          double pt[3];
          pt[0] = refined_knots[n];
          pt[1] = refined_knots[m];
          pt[2] = refined_knots[p];

          // Evaluate the difference between the interpolation
          // and the reconstruction (just the reconstruction part)
          double Nr[MAX_3D_ENRICH];
          if (order == 2) {
            eval2ndEnrichmentFuncs3D(pt, Nr);
          } else {
            eval3rdEnrichmentFuncs3D(pt, Nr);
          }

          // Add the portion from the enrichment functions
          int offset =
              (n + m * refined_order + p * refined_order * refined_order);
          TacsScalar *v = &vars_interp[vars_per_node * offset];
          for (int k = 0; k < nenrich; k++) {
            // Evaluate the interpolation part of the reconstruction
            for (int kk = 0; kk < vars_per_node; kk++) {
              v[kk] += ubar[vars_per_node * k + kk] * Nr[k];
            }
          }
        }
      }
    }

    // Compute the strain/potential energy
    TacsScalar Te, Pe;
    elem->computeEnergies(i, time, Xpts, vars_interp, dvars, &Te, &Pe);
    error[i] = fabs(TacsRealPart(Pe));

    // Add up the total error
    SE_total_error += error[i];
  }

  // Count up the total strain energy
  double SE_temp = 0.0;
  MPI_Allreduce(&SE_total_error, &SE_temp, 1, MPI_DOUBLE, MPI_SUM, comm);
  SE_total_error = SE_temp;

  // Free the global vectors
  uvec->decref();
  uderiv->decref();

  // Free the element-related data
  delete[] tmp;
  delete[] ubar;
  delete[] delem;
  delete[] vars_elem;
  delete[] dvars;
  delete[] vars_interp;

  return SE_total_error;
}

/*
  Write out the error bins to stdout
*/
void TMR_PrintErrorBins(MPI_Comm comm, const double *error, const int nelems,
                        double *mean, double *stddev) {
  const int NUM_BINS = 30;
  double low = -15;
  double high = 0;
  double bin_bounds[NUM_BINS + 1];
  int bins[NUM_BINS + 2];
  memset(bins, 0, (NUM_BINS + 2) * sizeof(int));

  // Compute the total number of elements
  int ntotal = nelems;
  MPI_Allreduce(MPI_IN_PLACE, &ntotal, 1, MPI_INT, MPI_SUM, comm);

  // Compute the mean of the element errors
  double m = 0;
  for (int i = 0; i < nelems; i++) {
    m += log(error[i]);
  }
  MPI_Allreduce(MPI_IN_PLACE, &m, 1, MPI_DOUBLE, MPI_SUM, comm);
  m = m / ntotal;
  if (mean) {
    *mean = m;
  }

  // Compute the standard deviation
  double s = 0;
  for (int i = 0; i < nelems; i++) {
    double er = log(error[i]) - m;
    s += er * er;
  }
  MPI_Allreduce(MPI_IN_PLACE, &s, 1, MPI_DOUBLE, MPI_SUM, comm);
  s = sqrt(s / (ntotal - 1));
  if (stddev) {
    *stddev = s;
  }

  // Now compute the bins
  for (int k = 0; k < NUM_BINS + 1; k++) {
    double val = low + 1.0 * k * (high - low) / NUM_BINS;
    bin_bounds[k] = pow(10.0, val);
  }

  for (int i = 0; i < nelems; i++) {
    if (error[i] <= bin_bounds[0]) {
      bins[0]++;
    } else if (error[i] >= bin_bounds[NUM_BINS]) {
      bins[NUM_BINS + 1]++;
    } else {
      for (int j = 0; j < NUM_BINS; j++) {
        if (error[i] >= bin_bounds[j] && error[i] < bin_bounds[j + 1]) {
          bins[j + 1]++;
        }
      }
    }
  }

  int mpi_rank;
  MPI_Comm_rank(comm, &mpi_rank);

  // Create a linear space
  MPI_Allreduce(MPI_IN_PLACE, bins, NUM_BINS + 2, MPI_INT, MPI_SUM, comm);

  if (mpi_rank == 0) {
    int total = 0;
    for (int i = 0; i < NUM_BINS + 2; i++) {
      total += bins[i];
    }
    printf("%10s  %10s  %12s  %12s\n", "stats", " ", "log(mean)",
           "log(stddev)");
    printf("%10s  %10s  %12.2e %12.2e\n", " ", " ", m, s);
    printf("%10s  %10s  %12s  %12s\n", "low", "high", "bins", "percentage");
    printf("%10s  %10.2e  %12d  %12.2f\n", " ", bin_bounds[0], bins[0],
           100.0 * bins[0] / total);

    for (int k = 0; k < NUM_BINS; k++) {
      printf("%10.2e  %10.2e  %12d  %12.2f\n", bin_bounds[k], bin_bounds[k + 1],
             bins[k + 1], 100.0 * bins[k + 1] / total);
    }
    printf("%10.2e  %10s  %12d  %12.2f\n", bin_bounds[NUM_BINS], " ",
           bins[NUM_BINS + 1], 100.0 * bins[NUM_BINS + 1] / total);
    fflush(stdout);
  }
}

/*!
  Create a nodal vector from the forest
*/
void createPartUnityVector(TMRQuadForest *pu, TACSBVec **pu_vec) {
  MPI_Comm comm = pu->getMPIComm();

  // Get the rank
  int mpi_rank;
  MPI_Comm_rank(comm, &mpi_rank);

  // Create the variable map for the partition of unity constraint
  const int *pu_range;
  pu->getOwnedNodeRange(&pu_range);

  int num_pu_local = pu_range[mpi_rank + 1] - pu_range[mpi_rank];
  TACSNodeMap *pu_map = new TACSNodeMap(comm, num_pu_local);

  // Create/retrieve the dependent node information
  const int *dep_ptr = NULL, *dep_conn = NULL;
  const double *dep_weights = NULL;
  int ndep = pu->getDepNodeConn(&dep_ptr, &dep_conn, &dep_weights);

  // Get the external numbers from the filter itself
  const int *pu_ext;
  int num_pu_ext = pu->getNodeNumbers(&pu_ext);

  // Count up all the external nodes
  int num_ext = 0;
  int *ext_nodes = new int[num_pu_ext];

  // Add the external nodes from the filter
  for (int i = 0; i < num_pu_ext; i++) {
    int node = pu_ext[i];
    if (node >= 0 &&
        (node < pu_range[mpi_rank] || node >= pu_range[mpi_rank + 1])) {
      ext_nodes[num_ext] = node;
      num_ext++;
    }
  }

  // Set up the external filter indices for this filter.  The indices
  // objects steals the array for the external nodes.
  TACSBVecIndices *pu_indices = new TACSBVecIndices(&ext_nodes, num_ext);
  pu_indices->setUpInverse();

  // Set up the external indices
  TACSBVecDistribute *pu_dist = new TACSBVecDistribute(pu_map, pu_indices);
  pu_dist->incref();

  // Copy over the data (inefficient)
  int *dptr = new int[ndep + 1];
  int *dconn = new int[dep_ptr[ndep]];
  double *dweights = new double[dep_ptr[ndep]];
  memcpy(dptr, dep_ptr, (ndep + 1) * sizeof(int));
  memcpy(dconn, dep_conn, dep_ptr[ndep] * sizeof(int));
  memcpy(dweights, dep_weights, dep_ptr[ndep] * sizeof(double));
  TACSBVecDepNodes *pu_dep_nodes =
      new TACSBVecDepNodes(ndep, &dptr, &dconn, &dweights);

  // Create a vector for the predicted nodal errors.
  *pu_vec = new TACSBVec(pu_map, 1, pu_dist, pu_dep_nodes);
}

/*
  Estimate error in a functional output using an adjoint-weighted residual.

  Assumes steady solution, transient problems not yet supported.

  input:
  forest:           the forest of quadtrees
  tacs:             the TACSAssembler object
  forest_refined:   the higher-order forest of quadtrees
  tacs_refined:     the higher-order TACSAssembler object
  solution_refined: the higher-order solution (or approximation)
  adjoint_refined:  the difference between the refined and coarse adjoint
                    solutions computed in some manner

  output:
  node_error:       the node-wise error indicators
  elem_error:       the localized element-wise error indicators
  adj_corr:         adjoint-based functional output correction

  returns:
  absolute functional output error estimate
*/
double TMR_AdjointErrorEst(TMRQuadForest *forest, TACSAssembler *tacs,
                           TMRQuadForest *forest_refined,
                           TACSAssembler *tacs_refined,
                           TACSBVec *solution_refined,
                           TACSBVec *adjoint_refined, double *node_error,
                           double *elem_error, double *adj_corr) {
  // The maximum number of nodes
  const int max_num_nodes = MAX_ORDER * MAX_ORDER;

  // Get the number of variables per node
  const int vars_per_node = tacs->getVarsPerNode();

  // Get the order of the mesh and the number of enrichment shape functions
  const double *refined_knots;
  const int refined_order = forest_refined->getInterpKnots(&refined_knots);
  const int num_refined_nodes = refined_order * refined_order;

  // Get the number of elements in the coarse/refined mesh
  const int nelems = tacs->getNumElements();

  // Get the communicator
  MPI_Comm comm = tacs->getMPIComm();

  // Allocate the element arrays needed for the adjoint-weighted residual
  TacsScalar *vars_refined = new TacsScalar[vars_per_node * num_refined_nodes];
  TacsScalar *dvars_refined = new TacsScalar[vars_per_node * num_refined_nodes];
  TacsScalar *ddvars_refined =
      new TacsScalar[vars_per_node * num_refined_nodes];
  TacsScalar *adj_refined = new TacsScalar[vars_per_node * num_refined_nodes];
  TacsScalar *res_refined = new TacsScalar[vars_per_node * num_refined_nodes];
  memset(dvars_refined, 0,
         vars_per_node * num_refined_nodes * sizeof(TacsScalar));
  memset(ddvars_refined, 0,
         vars_per_node * num_refined_nodes * sizeof(TacsScalar));

  // Allocate arrays for storing the nodal error esimates and nodal
  // element counts - updated per element
  TacsScalar *err = new TacsScalar[num_refined_nodes];
  TacsScalar *weights = new TacsScalar[num_refined_nodes];

  // Keep track of the total output functional error estimate and the total
  // output functional correction terms
  TacsScalar total_error_est = 0.0;
  TacsScalar total_output_corr = 0.0;

  // Create a distributed vector for the nodal error estimates
  TACSNodeMap *refined_map = tacs_refined->getNodeMap();
  TACSBVecDistribute *refined_dist = tacs_refined->getBVecDistribute();
  TACSBVecDepNodes *refined_dep_nodes = tacs_refined->getBVecDepNodes();
  TACSBVec *nodal_error =
      new TACSBVec(refined_map, 1, refined_dist, refined_dep_nodes);
  nodal_error->incref();
  nodal_error->zeroEntries();

  // Create a distributed vector for the element count of each node
  // This is used for error localization from the nodes to the elements
  TACSBVec *nodal_weights =
      new TACSBVec(refined_map, 1, refined_dist, refined_dep_nodes);
  nodal_weights->incref();
  computeLocalWeights(tacs_refined, nodal_weights);

  // Distribute the values for the adjoint/solution
  solution_refined->beginDistributeValues();
  adjoint_refined->beginDistributeValues();
  solution_refined->endDistributeValues();
  adjoint_refined->endDistributeValues();

  // Get the auxiliary elements defined in the problem
  TACSAuxElements *aux_elements = tacs_refined->getAuxElements();
  int num_aux_elems = 0;
  TACSAuxElem *aux = NULL;
  if (aux_elements) {
    aux_elements->sort();
    num_aux_elems = aux_elements->getAuxElements(&aux);
  }

  // Compute the nodal error estimates using an adjoint-weighted residual for
  // each element on this proc
  int aux_count = 0;
  for (int elem = 0; elem < nelems; elem++) {
    // Set the simulation time
    double time = 0.0;

    // Get the node numbers for this element in the refined mesh
    int nnodes = 0;
    const int *node_inds;
    tacs_refined->getElement(elem, &nnodes, &node_inds);

    // Get the node locations for this element
    TacsScalar Xpts[3 * max_num_nodes];
    TACSElement *element = tacs_refined->getElement(elem, Xpts);

    // Get the state and adjoint variables for this element
    solution_refined->getValues(nnodes, node_inds, vars_refined);
    adjoint_refined->getValues(nnodes, node_inds, adj_refined);

    // Compute the residual on the element
    memset(err, 0, nnodes * sizeof(TacsScalar));
    memset(res_refined, 0, nnodes * vars_per_node * sizeof(TacsScalar));
    element->addResidual(elem, time, Xpts, vars_refined, dvars_refined,
                         ddvars_refined, res_refined);

    // Compute the nodal error estimates with the adjoint-weighted residual
    for (int inode = 0; inode < nnodes; inode++) {
      for (int ivar = 0; ivar < vars_per_node; ivar++) {
        int ind = vars_per_node * inode + ivar;
        err[inode] += -1. * (res_refined[ind] * adj_refined[ind]);
      }
    }

    // Add the contribution from any loads - opposite sign for error
    // contribution
    while (aux_count < num_aux_elems && aux[aux_count].num == elem) {
      memset(res_refined, 0, nnodes * vars_per_node * sizeof(TacsScalar));
      aux[aux_count].elem->addResidual(elem, time, Xpts, vars_refined,
                                       dvars_refined, ddvars_refined,
                                       res_refined);
      for (int inode = 0; inode < nnodes; inode++) {
        for (int ivar = 0; ivar < vars_per_node; ivar++) {
          int ind = vars_per_node * inode + ivar;
          err[inode] += 1. * (res_refined[ind] * adj_refined[ind]);
        }
      }
      aux_count++;
    }

    // Add the nodal errors to the total output functional correction term
    for (int i = 0; i < nnodes; i++) total_output_corr += TacsRealPart(err[i]);

    // Add the nodal errors from this element to the nodal error vector
    nodal_error->setValues(nnodes, node_inds, err, TACS_ADD_VALUES);
  }

  // Finish setting the values into the nodal error array
  nodal_error->beginSetValues(TACS_ADD_VALUES);
  nodal_error->endSetValues(TACS_ADD_VALUES);

  // Distribute the values back to all nodes
  nodal_error->beginDistributeValues();
  nodal_error->endDistributeValues();

  // Localize the error from the nodes to the elements with an equal split
  for (int elem = 0; elem < nelems; elem++) {
    // Get the node numbers for this element in the refined mesh
    int nnodes = 0;
    const int *node_inds;
    tacs_refined->getElement(elem, &nnodes, &node_inds);

    // Get the errors and element counts for the nodes of this element
    nodal_error->getValues(nnodes, node_inds, err);
    nodal_weights->getValues(nnodes, node_inds, weights);

    // Compute the element indicator error as a function of the nodal
    // error estimate.
    elem_error[elem] = 0.0;
    for (int i = 0; i < nnodes; i++) {
      if (node_inds[i] < 0) {
        // skip dependent nodes - handled in beginSetValues() with dep weights
        continue;
      }
      elem_error[elem] += TacsRealPart(err[i]) / weights[i];
    }
    // absolute value after the sum allows for error cancellation between each
    // basis function within an element
    elem_error[elem] = fabs(elem_error[elem]);
  }

  // Add up the absolute value of the nodal errors to get the output
  // functional error
  TacsScalar *nerr;
  int size = nodal_error->getArray(&nerr);
  memcpy(node_error, nerr, size * sizeof(double));
  for (int i = 0; i < nelems; i++) {
    total_error_est += elem_error[i];
  }

  // Sum up the contributions across all processors
  double temp[2];
  temp[0] = total_error_est;
  temp[1] = total_output_corr;
  MPI_Allreduce(MPI_IN_PLACE, temp, 2, MPI_DOUBLE, MPI_SUM, comm);
  total_error_est = temp[0];
  total_output_corr = temp[1];

  // Free the data that is no longer required
  delete[] vars_refined;
  delete[] dvars_refined;
  delete[] ddvars_refined;
  delete[] adj_refined;
  delete[] res_refined;
  delete[] err;
  delete[] weights;
  nodal_error->decref();
  nodal_weights->decref();

  // Set the adjoint residual correction
  if (adj_corr) {
    *adj_corr = total_output_corr;
  }

  // Return the error
  return total_error_est;
}

/*
  Refine the mesh using the original solution and the adjoint solution

  input:
  forest:           the forest of quadtrees
  tacs:             the TACSAssembler object
  forest_refined:   the higher-order forest of quadtrees
  tacs_refined:     the higher-order TACSAssembler object
  solution_refined: the higher-order solution (or approximation)
  adjoint_refined:  the difference between the refined and coarse adjoint
                    solutions computed in some manner

  output:
  adj_corr:      adjoint-based functional correction

  returns:
  absolute functional error estimate
*/
double TMR_AdjointErrorEst(TMROctForest *forest, TACSAssembler *tacs,
                           TMROctForest *forest_refined,
                           TACSAssembler *tacs_refined,
                           TACSBVec *solution_refined,
                           TACSBVec *adjoint_refined, double *error,
                           double *adj_corr) {
  /*
    const int max_num_nodes = MAX_ORDER*MAX_ORDER*MAX_ORDER;

    // Get the order of the mesh and the number of enrichment shape functions
    const double *refined_knots;
    const int refined_order = forest_refined->getInterpKnots(&refined_knots);
    const int num_refined_nodes = refined_order*refined_order*refined_order;

    // Perform a local refinement of the nodes based on the strain energy
    // within each element
    const int nelems = tacs->getNumElements();

    // Get the number of variables per node
    const int vars_per_node = tacs->getVarsPerNode();

    // Get the communicator
    MPI_Comm comm = tacs->getMPIComm();

    // Allocate the element arrays needed for the reconstruction
    TacsScalar *vars_interp = new TacsScalar[ vars_per_node*num_refined_nodes ];
    TacsScalar *adj_interp = new TacsScalar[ vars_per_node*num_refined_nodes ];

    // Allocate the nodal error estimates array
    TacsScalar *err = new TacsScalar[ num_refined_nodes ];

    // Keep track of the total error remaining from each element
    // indicator and the adjoint error correction
    double total_error_remain = 0.0;
    TacsScalar total_adjoint_corr = 0.0;

    // Create a vector for the predicted nodal errors.
    TACSBVec *nodal_error = new TACSBVec(tacs_refined->getNodeMap(), 1,
                                         tacs_refined->getBVecDistribute(),
                                         tacs_refined->getBVecDepNodes());
    nodal_error->incref();

    // Distribute the values for the adjoint/solution
    solution_refined->beginDistributeValues();
    adjoint_refined->beginDistributeValues();
    solution_refined->endDistributeValues();
    adjoint_refined->endDistributeValues();

    // Get the auxiliary elements (surface tractions) associated with
    // the element class
    TACSAuxElements *aux_elements = tacs_refined->getAuxElements();
    int num_aux_elems = 0;
    TACSAuxElem *aux = NULL;
    if (aux_elements){
      aux_elements->sort();
      num_aux_elems = aux_elements->getAuxElements(&aux);
    }

    // Compute the residual on the refined mesh with the interpolated
    // variables.
    int aux_count = 0;
    for ( int elem = 0; elem < nelems; elem++ ){
      // Set the simulation time
      double time = 0.0;

      // Get the node numbers for the refined mesh
      int refine_len = 0;
      const int *refine_nodes;
      tacs_refined->getElement(elem, &refine_nodes, &refine_len);

      // Get the node locations
      TacsScalar Xpts[3*max_num_nodes];
      TACSElement *element = tacs_refined->getElement(elem, Xpts);

      // Get the adjoint variables for this element
      solution_refined->getValues(refine_len, refine_nodes, vars_interp);
      adjoint_refined->getValues(refine_len, refine_nodes, adj_interp);

      // Compute the localized error on the refined mesh
      memset(err, 0, element->getNumNodes()*sizeof(TacsScalar));
      element->addLocalizedError(time, err, adj_interp,
                                 Xpts, vars_interp);

      // Add the contribution from any forces
      while (aux_count < num_aux_elems && aux[aux_count].num == elem){
        aux[aux_count].elem->addLocalizedError(time, err, adj_interp,
                                               Xpts, vars_interp);
        aux_count++;
      }

      // Add up the total adjoint correction
      for ( int i = 0; i < element->getNumNodes(); i++ ){
        total_adjoint_corr += err[i];
      }

      // Add the contributions to the nodal error
      nodal_error->setValues(refine_len, refine_nodes, err, TACS_ADD_VALUES);
    }

    // Finish setting the values into the nodal error array
    nodal_error->beginSetValues(TACS_ADD_VALUES);
    nodal_error->endSetValues(TACS_ADD_VALUES);

    // Distribute the values back to all nodes
    nodal_error->beginDistributeValues();
    nodal_error->endDistributeValues();

    // Finish setting the values into the array
    for ( int elem = 0; elem < nelems; elem++ ){
      // Get the node numbers for the refined mesh
      int len = 0;
      const int *nodes;
      tacs_refined->getElement(elem, &len, &nodes);

      // Get the adjoint variables for this element
      nodal_error->getValues(len, nodes, err);

      // Compute the element indicator error as a function of the nodal
      // error estimate.
      TacsScalar estimate = 0.0;
      for ( int k = 0; k < 2; k++ ){
        for ( int j = 0; j < 2; j++ ){
          for ( int i = 0; i < 2; i++ ){
            estimate +=
              err[(refined_order-1)*i +
                  (refined_order-1)*j*refined_order +
                  (refined_order-1)*k*refined_order*refined_order];
          }
        }
      }

      error[elem] = 0.125*fabs(TacsRealPart(estimate));
      total_error_remain += error[elem];
    }

    // Sum up the contributions across all processors
    double temp[2];
    temp[0] = total_error_remain;
    temp[1] = total_adjoint_corr;
    MPI_Allreduce(MPI_IN_PLACE, temp, 2, MPI_DOUBLE, MPI_SUM, comm);
    total_error_remain = temp[0];
    total_adjoint_corr = temp[1];

    // Free the data that is no longer required
    delete [] vars_interp;
    delete [] adj_interp;
    delete [] err;
    nodal_error->decref();

    // Set the adjoint residual correction
    if (adj_corr){
      *adj_corr = total_adjoint_corr;
    }

    // Return the error
    return total_error_remain;
  */
  return 0.0;
}

/*
  Evaluate the stress constraints on a more-refined mesh
*/
/*
TMRStressConstraint::TMRStressConstraint( TMROctForest *_forest,
                                          TACSAssembler *_tacs,
                                          TacsScalar _ks_weight ){
  tacs = _tacs;
  tacs->incref();

  // Set the order/ksweight
  forest = _forest;
  forest->incref();
  ks_weight = _ks_weight;

  // Get the mesh order
  order = forest->getMeshOrder();
  TMRInterpolationType interp_type = forest->getInterpType();

  // Create a forest with elevated order
  interp_forest = forest->duplicate();
  interp_forest->incref();

  // Create the mesh for the forest
  interp_forest->setMeshOrder(order+1, interp_type);

  // Create the nodes for the duplicated forest
  interp_forest->createNodes();

  // Allocate a local vector
  uvec = tacs->createVec();
  uvec->incref();

  // Create the weight vector - the weights are the number of times
  // each node is referenced by adjacent elements, including
  // inter-process references.
  weights = new TACSBVec(tacs->getNodeMap(), 1,
                         tacs->getBVecDistribute(),
                         tacs->getBVecDepNodes());
  weights->incref();

  // Compute the local weights in each vector
  computeLocalWeights(tacs, weights);

  // Allocate a vector for the derivatives
  int vars_per_node = tacs->getVarsPerNode();
  int deriv_per_node = 3*vars_per_node;
  uderiv = new TACSBVec(tacs->getNodeMap(), deriv_per_node,
                        tacs->getBVecDistribute(), tacs->getBVecDepNodes());
  uderiv->incref();

  // Allocate derivative vector
  dfduderiv = new TACSBVec(tacs->getNodeMap(), deriv_per_node,
                           tacs->getBVecDistribute(), tacs->getBVecDepNodes());
  dfduderiv->incref();

  // Allocate the vectors
  int max_nodes = tacs->getMaxElementNodes();
  Xpts = new TacsScalar[ 3*(order+1)*(order+1)*(order+1) ];
  vars = new TacsScalar[ vars_per_node*max_nodes ];
  dvars = new TacsScalar[ vars_per_node*max_nodes ];
  ddvars = new TacsScalar[ vars_per_node*max_nodes ];

  // Set the maximum number of nodes
  int neq = 3*max_nodes;
  int max_enrich = 15;
  varderiv = new TacsScalar[ deriv_per_node*max_nodes ];
  ubar = new TacsScalar[ vars_per_node*max_enrich ];
  tmp = new TacsScalar[ neq*(max_enrich + vars_per_node) ];
}
*/

/*
  Free the data that was allocated
*/
/*
TMRStressConstraint::~TMRStressConstraint(){
  forest->decref();
  interp_forest->decref();
  tacs->decref();
  weights->decref();
  uderiv->decref();
  uvec->decref();
  dfduderiv->decref();
  delete [] Xpts;
  delete [] vars;
  delete [] dvars;
  delete [] ddvars;
  delete [] varderiv;
  delete [] ubar;
  delete [] tmp;
}
*/

/*
  Evaluate the constraint on the refined mesh
*/
/*
TacsScalar TMRStressConstraint::evalConstraint( TACSBVec *_uvec ){
  const int vars_per_node = tacs->getVarsPerNode();

  // Copy the values
  uvec->copyValues(_uvec);

  // Distribute the variable values so that the non-owned values can
  // be accessed locally
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Compute the derivatives at the nodes
  computeNodeDeriv3D(forest, tacs, uvec, weights, uderiv);

  // Number of local elements
  const int nelems = tacs->getNumElements();

  // Set the communicator
  MPI_Comm comm = tacs->getMPIComm();

  // First, go through and evaluate the maximum stress
  // in all of the elements
  ks_max_fail = -1e20;

  // Get the quadrature points/weights
  const double *gaussPts, *gaussWts;

  //int num_quad_pts = FElibrary::getGaussPtsWts(order+1, &gaussPts, &gaussWts);
  int num_quad_pts = 0;
  // FElibrary::getGaussPtsWts(order+1, &gaussPts, &gaussWts);

  // int num_quad_pts =
  //   FElibrary::getGaussPtsWts(LOBATTO_QUADRATURE, order+2,
  //                             &gaussPts, &gaussWts);

  // Get the local connectivity for the higher-order mesh
  const int *conn = NULL;
  interp_forest->getNodeConn(&conn);

  // Get the higher-order points
  TMRPoint *X;
  interp_forest->getPoints(&X);

  for ( int i = 0; i < nelems; i++ ){
    // Get the element class and the variables associated with it
    TACSElement *elem = tacs->getElement(i, Xpts, vars, dvars, ddvars);

    // Get the constitutive relationship
    TACSConstitutive *con = elem->getConstitutive();

    // Get the node numbers and node locations for this element
    int len;
    const int *nodes;
    tacs->getElement(i, &len, &nodes);
    tacs->getElement(i, Xpts);

    // Retrieve the nodal values and nodal derivatives
    uvec->getValues(len, nodes, vars);
    uderiv->getValues(len, nodes, varderiv);

    // Now get the node locations for the locally refined mesh
    const int interp_elem_size = (order+1)*(order+1)*(order+1);
    for ( int j = 0; j < interp_elem_size; j++ ){
      int c = conn[interp_elem_size*i + j];
      int node = interp_forest->getLocalNodeNumber(c);
      Xpts[3*j] = X[node].x;
      Xpts[3*j+1] = X[node].y;
      Xpts[3*j+2] = X[node].z;
    }

    // Compute the values of the enrichment coefficient for each
    // degree of freedom
    computeElemRecon3D(vars_per_node, forest, interp_forest,
                       Xpts, vars, varderiv, ubar, tmp);

    // For each quadrature point, evaluate the strain at the
    // quadrature point and evaluate the stress constraint
    for ( int kk = 0; kk < num_quad_pts; kk++ ){
      for ( int jj = 0; jj < num_quad_pts; jj++ ){
        for ( int ii = 0; ii < num_quad_pts; ii++ ){
          // Pick the quadrature point at which we will
          // evaluate the strain
          double pt[3];
          pt[0] = gaussPts[ii];
          pt[1] = gaussPts[jj];
          pt[2] = gaussPts[kk];

          // Evaluate the strain
          TacsScalar J[9], e[6];
          evalStrain(pt, Xpts, vars, ubar, J, e);

          // Evaluate the failure criteria
          TacsScalar fval;
          con->failure(pt, e, &fval);

          if (TacsRealPart(fval) >
              TacsRealPart(ks_max_fail)){
            ks_max_fail = fval;
          }
        }
      }
    }
  }

  // Find the maximum failure value across all of the processors
  MPI_Allreduce(MPI_IN_PLACE, &ks_max_fail, 1, TACS_MPI_TYPE, MPI_MAX, comm);

  // Compute the sum over all the element - integrate the sum over all
  // elements/procs
  ks_fail_sum = 0.0;

  for ( int i = 0; i < nelems; i++ ){
    // Get the element class and the variables associated with it
    TACSElement *elem = tacs->getElement(i, Xpts, vars, dvars, ddvars);

    // Get the constitutive relationship
    TACSConstitutive *con = elem->getConstitutive();

    // Get the node numbers and node locations for this element
    int len;
    const int *nodes;
    tacs->getElement(i, &len, &nodes);
    tacs->getElement(i, Xpts);

    // Retrieve the nodal values and nodal derivatives
    uvec->getValues(len, nodes, vars);
    uderiv->getValues(len, nodes, varderiv);

    // Now get the node locations for the locally refined mesh
    const int interp_elem_size = (order+1)*(order+1)*(order+1);
    for ( int j = 0; j < interp_elem_size; j++ ){
      int c = conn[interp_elem_size*i + j];
      int node = interp_forest->getLocalNodeNumber(c);
      Xpts[3*j] = X[node].x;
      Xpts[3*j+1] = X[node].y;
      Xpts[3*j+2] = X[node].z;
    }

    // Compute the values of the enrichment coefficient for each
    // degree of freedom
    computeElemRecon3D(vars_per_node, forest, interp_forest,
                       Xpts, vars, varderiv, ubar, tmp);

    // For each quadrature point, evaluate the strain at the
    // quadrature point and evaluate the stress constraint
    for ( int kk = 0; kk < num_quad_pts; kk++ ){
      for ( int jj = 0; jj < num_quad_pts; jj++ ){
        for ( int ii = 0; ii < num_quad_pts; ii++ ){
          // Pick the quadrature point at which we will
          // evaluate the strain
          double pt[3];
          pt[0] = gaussPts[ii];
          pt[1] = gaussPts[jj];
          pt[2] = gaussPts[kk];

          // Evaluate the strain
          TacsScalar J[9], e[6];
          TacsScalar detJ = evalStrain(pt, Xpts, vars, ubar, J, e);
          detJ *= gaussWts[ii]*gaussWts[jj]*gaussWts[kk];

          // Evaluate the failure criteria
          TacsScalar fval;
          con->failure(pt, e, &fval);

          // Compute the sum of KS failure values
          ks_fail_sum += detJ*exp(ks_weight*(fval - ks_max_fail));
        }
      }
    }
  }

  MPI_Allreduce(MPI_IN_PLACE, &ks_fail_sum, 1, TACS_MPI_TYPE, MPI_SUM, comm);

  TacsScalar ks_func_val = ks_max_fail + log(ks_fail_sum)/ks_weight;

  int mpi_rank;
  MPI_Comm_rank(comm, &mpi_rank);
  if (mpi_rank == 0){
    printf("KS stress value:  %25.10e\n", ks_func_val);
    printf("Max stress value: %25.10e\n", ks_max_fail);
  }

  return ks_func_val;
}
*/

/*
  Evaluate the derivative w.r.t. state and design vectors
*/
/*
void TMRStressConstraint::evalConDeriv( TacsScalar *dfdx, int size,
                                        TACSBVec *dfdu ){

  memset(dfdx, 0, size*sizeof(TacsScalar));

  // Get information about the interpolation
  const double *knots;
  forest->getInterpKnots(&knots);

  // Get the number of enrichment functions
  const int nenrich = getNum3dEnrich(order);

  // Get vars per node and compute other size variables
  const int vars_per_node = tacs->getVarsPerNode();
  const int num_nodes = order*order*order;
  const int neq = num_nodes*vars_per_node;

  // Set the derivative of the function w.r.t. the state variables
  dfdu->zeroEntries();
  dfduderiv->zeroEntries();

  // Number of local elements
  const int nelems = tacs->getNumElements();

  // Set the weights
  double wvals[3];
  if (order == 2){
    wvals[0] = wvals[1] = 1.0;
  }
  else if (order == 3){
    wvals[0] = wvals[2] = 0.5;
    wvals[1] = 1.0;
  }

  // Get the quadrature points/weights
  const double *gaussPts, *gaussWts;
  int num_quad_pts =
    FElibrary::getGaussPtsWts(order+1, &gaussPts, &gaussWts);
  // int num_quad_pts =
  //   FElibrary::getGaussPtsWts(LOBATTO_QUADRATURE, order+2,
  //                             &gaussPts, &gaussWts);

  // Get the local connectivity for the higher-order mesh
  const int *conn = NULL;
  interp_forest->getNodeConn(&conn);

  // Get the higher-order points
  TMRPoint *X;
  interp_forest->getPoints(&X);

  // Set the matrix dimensions
  int m = nenrich;
  int n = neq;
  int p = num_nodes; //num_quad_pts*num_quad_pts*num_quad_pts;

  // Initialize variables
  TacsScalar *dfdu_elem = new TacsScalar[3*p];
  TacsScalar *dfdubar = new TacsScalar[3*m];
  TacsScalar *dubardu = new TacsScalar[m*p];
  TacsScalar *A = new TacsScalar[n*m];
  TacsScalar *dbdu = new TacsScalar[n*p];
  TacsScalar *dubar_duderiv = new TacsScalar[m*n];
  TacsScalar *dfduderiv_elem = new TacsScalar[3*n];
  TacsScalar *duderiv_du = new TacsScalar[3*n*3*p];

  for ( int i = 0; i < nelems; i++ ){
    // Get the element class and the variables associated with it
    TACSElement *elem = tacs->getElement(i, Xpts, vars, dvars, ddvars);

    // Get the constitutive relationship
    TACSConstitutive *con = elem->getConstitutive();

    // Get the node numbers and node locations for this element
    int len;
    const int *nodes;
    tacs->getElement(i, &len, &nodes);

    // Get the local weight used for computing uderiv
    TacsScalar welem[order*order*order];
    weights->getValues(len, nodes, welem);

    // Retrieve the nodal values and nodal derivatives
    uvec->getValues(len, nodes, vars);
    uderiv->getValues(len, nodes, varderiv);

    // Now get the node locations for the locally refined mesh
    const int interp_elem_size = (order+1)*(order+1)*(order+1);
    for ( int j = 0; j < interp_elem_size; j++ ){
      int c = conn[interp_elem_size*i + j];
      int node = interp_forest->getLocalNodeNumber(c);
      Xpts[3*j] = X[node].x;
      Xpts[3*j+1] = X[node].y;
      Xpts[3*j+2] = X[node].z;
    }

    // Compute the values of the enrichment coefficient for each
    // degree of freedom
    computeElemRecon3D(vars_per_node, forest, interp_forest,
                       Xpts, vars, varderiv, ubar, tmp);

    // Zero variables before elementwise operations begin
    memset(dfdu_elem, 0, 3*p*sizeof(TacsScalar));
    memset(dfdubar, 0, 3*m*sizeof(TacsScalar));
    memset(dubardu, 0, m*p*sizeof(TacsScalar));
    memset(A, 0, n*m*sizeof(TacsScalar));
    memset(dbdu, 0, n*p*sizeof(TacsScalar));
    memset(dubar_duderiv, 0, m*n*sizeof(TacsScalar));
    memset(duderiv_du, 0, 3*n*3*p*sizeof(TacsScalar));

    // Compute the partial derivatives (df/du) and (df/dubar)
    for ( int c = 0, kk = 0; kk < num_quad_pts; kk++ ){
      for ( int jj = 0; jj < num_quad_pts; jj++ ){
        for ( int ii = 0; ii < num_quad_pts; ii++, c += 3 ){
          // Pick the quadrature point at which we will
          // evaluate the strain
          double pt[3];
          pt[0] = gaussPts[ii];
          pt[1] = gaussPts[jj];
          pt[2] = gaussPts[kk];

          // Evaluate the strain
          TacsScalar J[9], e[6];
          TacsScalar detJ = evalStrain(pt, Xpts, vars, ubar, J, e);
          detJ *= gaussWts[ii]*gaussWts[jj]*gaussWts[kk];

          // Evaluate the failure criteria
          TacsScalar fval;
          con->failure(pt, e, &fval);

          // Compute the weight at this point
          TacsScalar kw = detJ*exp(ks_weight*(fval - ks_max_fail))/ks_fail_sum;

          // Add the derivative w.r.t. the design variables
          con->addFailureDVSens(pt, e, kw, dfdx, size);

          // Add the derivative w.r.t. the strain
          TacsScalar dfde[6];
          con->failureStrainSens(pt, e, dfde);

          // Add the derivative of the strain
          addStrainDeriv(pt, J, kw, dfde, dfdu_elem, dfdubar);
        }
      }
    }

    // Add the partial dertiv df/du term to the total deriv
    dfdu->setValues(len, nodes, dfdu_elem, TACS_ADD_VALUES);

    //
    // Compute A and (db/du)
    //
    for ( int c = 0, kk = 0; kk < order; kk++ ){
      for ( int jj = 0; jj < order; jj++ ){
        for ( int ii = 0; ii < order; ii++, c += 3 ){
          // Evaluate the knot locations
          double kt[3];
          kt[0] = knots[ii];
          kt[1] = knots[jj];
          kt[2] = knots[kk];

          // Compute the element shape functions at this point
          double N[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          double Na[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          double Nb[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          double Nc[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          interp_forest->evalInterp(kt, N, Na, Nb, Nc);

          // Evaluate the Jacobian transformation at this point
          TacsScalar Xd[9], J[9];
          computeJacobianTrans3D(Xpts, Na, Nb, Nc, Xd, J,
                                 (order+1)*(order+1)*(order+1));

          // Evaluate the enrichment shape functions
          double Nr[MAX_3D_ENRICH];
          double Nar[MAX_3D_ENRICH], Nbr[MAX_3D_ENRICH], Ncr[MAX_3D_ENRICH];
          if (order == 2){
            eval2ndEnrichmentFuncs3D(kt, Nr, Nar, Nbr, Ncr);
          }
          else if (order == 3){
            eval3rdEnrichmentFuncs3D(kt, Nr, Nar, Nbr, Ncr);
          }

          // Evaluate the shape functions and their derivatives
          forest->evalInterp(kt, N, Na, Nb, Nc);

          for ( int aa = 0; aa < num_nodes; aa++ ){
            // Compute and assemble (db/du)
            TacsScalar d[3];
            d[0] = Na[aa]*J[0] + Nb[aa]*J[1] + Nc[aa]*J[2];
            d[1] = Na[aa]*J[3] + Nb[aa]*J[4] + Nc[aa]*J[5];
            d[2] = Na[aa]*J[6] + Nb[aa]*J[7] + Nc[aa]*J[8];

            dbdu[neq*aa+c] = -wvals[ii]*wvals[jj]*wvals[kk]*d[0];
            dbdu[neq*aa+c+1] = -wvals[ii]*wvals[jj]*wvals[kk]*d[1];
            dbdu[neq*aa+c+2] = -wvals[ii]*wvals[jj]*wvals[kk]*d[2];
          }

          for ( int aa = 0; aa < nenrich; aa++ ){
            // Compute and assemble A
            TacsScalar dr[3];
            dr[0] = Nar[aa]*J[0] + Nbr[aa]*J[1] + Ncr[aa]*J[2];
            dr[1] = Nar[aa]*J[3] + Nbr[aa]*J[4] + Ncr[aa]*J[5];
            dr[2] = Nar[aa]*J[6] + Nbr[aa]*J[7] + Ncr[aa]*J[8];

            A[neq*aa+c] = wvals[ii]*wvals[jj]*wvals[kk]*dr[0];
            A[neq*aa+c+1] = wvals[ii]*wvals[jj]*wvals[kk]*dr[1];
            A[neq*aa+c+2] = wvals[ii]*wvals[jj]*wvals[kk]*dr[2];
          }
        }
      }
    }

    // Compute dubar/du and dubar/duderiv
    addEnrichDeriv(A, dbdu, dubardu, dubar_duderiv);

    // Compute the product (df/dubar)(dubar/du)
    memset(dfdu_elem, 0, 3*p*sizeof(TacsScalar));

    for ( int ii = 0; ii < m; ii++ ){
      for ( int jj = 0; jj < p; jj++ ){
        for ( int c = 0; c < 3; c++ ){
          dfdu_elem[3*jj+c] += dfdubar[3*ii+c]*dubardu[m*jj+ii];
        }
      }
    }

    // Add the product (df/dubar)(dubar/du) to the total deriv
    dfdu->setValues(len, nodes, dfdu_elem, TACS_ADD_VALUES);

    // Compute the product (df/duderiv) = (df/dubar)(dubar/duderiv)
    memset(dfduderiv_elem, 0, 3*n*sizeof(TacsScalar));
    for ( int ii = 0; ii < n; ii++ ){
      for ( int jj = 0; jj < m; jj++ ){
        for ( int c = 0; c < 3; c++ ){
          dfduderiv_elem[9*(ii/3) + 3*c + (ii % 3)] +=
            dfdubar[3*jj+c]*dubar_duderiv[m*ii+jj];
        }
      }
    }

    // Add the contributions to the derivative of the function
    // w.r.t. the derivatives at the nodes
    dfduderiv->setValues(len, nodes, dfduderiv_elem, TACS_ADD_VALUES);
  }

  // Add the values across all processors
  dfduderiv->beginSetValues(TACS_ADD_VALUES);
  dfduderiv->endSetValues(TACS_ADD_VALUES);

  // Distribute the values so that we can call getValues
  dfduderiv->beginDistributeValues();
  dfduderiv->endDistributeValues();

  //
  // Compute the product of (df/duderiv)(duderiv/du)
  //

  // Allocate a temporary array to store a component of the derivative
  TacsScalar *dUd = new TacsScalar[ 3*vars_per_node ];

  // Perform the reconstruction for the local
  for ( int elem = 0; elem < nelems; elem++ ){
    // Get the element nodes
    int len = 0;
    const int *nodes = NULL;
    tacs->getElement(elem, &len, &nodes);

    // Get the local weight values for this element
    TacsScalar welem[MAX_ORDER*MAX_ORDER*MAX_ORDER];
    weights->getValues(len, nodes, welem);

    // Get the values of the derivatives
    dfduderiv->getValues(len, nodes, dfduderiv_elem);

    // Now get the node locations for the locally refined mesh
    const int interp_elem_size = (order+1)*(order+1)*(order+1);
    for ( int j = 0; j < interp_elem_size; j++ ){
      int c = conn[interp_elem_size*elem + j];
      int node = interp_forest->getLocalNodeNumber(c);
      Xpts[3*j] = X[node].x;
      Xpts[3*j+1] = X[node].y;
      Xpts[3*j+2] = X[node].z;
    }

    // Set the pointer for the derivatives of the components of the
    // variables along each of the 3-coordinate directions
    const TacsScalar *d = dfduderiv_elem;

    // Zero the element derivative
    memset(dfdu_elem, 0, 3*p*sizeof(TacsScalar));

    // Compute the contributions to the derivative from this side of
    // the element
    for ( int kk = 0; kk < order; kk++ ){
      for ( int jj = 0; jj < order; jj++ ){
        for ( int ii = 0; ii < order; ii++ ){
          double pt[3];
          pt[0] = knots[ii];
          pt[1] = knots[jj];
          pt[2] = knots[kk];

          // Evaluate the the shape functions
          double N[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          double Na[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          double Nb[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          double Nc[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          interp_forest->evalInterp(pt, N, Na, Nb, Nc);

          // Evaluate the Jacobian transformation at this point
          TacsScalar Xd[9], J[9];
          computeJacobianTrans3D(Xpts, Na, Nb, Nc, Xd, J,
                                 (order+1)*(order+1)*(order+1));

          // Evaluate the interpolation
          forest->evalInterp(pt, N, Na, Nb, Nc);

          // Accumulate the derivatives w.r.t. x/y/z for each value at
          // the independent nodes
          TacsScalar winv = 1.0/welem[ii + jj*order + kk*order*order];
          if (nodes[ii + jj*order + kk*order*order] >= 0){
            for ( int k = 0; k < vars_per_node; k++ ){
              dUd[3*k]   = winv*(J[0]*d[0] + J[3]*d[1] + J[6]*d[2]);
              dUd[3*k+1] = winv*(J[1]*d[0] + J[4]*d[1] + J[7]*d[2]);
              dUd[3*k+2] = winv*(J[2]*d[0] + J[5]*d[1] + J[8]*d[2]);
              d += 3;
            }

            // Compute the derivatives from the interpolated solution
            for ( int k = 0; k < vars_per_node; k++ ){
              TacsScalar *ue = &dfdu_elem[k];
              for ( int i = 0; i < order*order*order; i++ ){
                ue[0] += (Na[i]*dUd[3*k] + Nb[i]*dUd[3*k+1] + Nc[i]*dUd[3*k+2]);
                ue += vars_per_node;
              }
            }
          }
          else {
            d += 3*vars_per_node;
          }
        }
      }
    }

    // Get the local element variables
    dfdu->setValues(len, nodes, dfdu_elem, TACS_ADD_VALUES);
  }

  dfdu->beginSetValues(TACS_ADD_VALUES);
  dfdu->endSetValues(TACS_ADD_VALUES);

  tacs->applyBCs(dfdu);

  // Free allocated memory
  delete [] dfdu_elem;
  delete [] dfdubar;
  delete [] dubardu;
  delete [] A;
  delete [] dbdu;
  delete [] dubar_duderiv;
  delete [] duderiv_du;
  delete [] dUd;
}
*/

/*
  Evaluate the strain
*/
/*
TacsScalar TMRStressConstraint::evalStrain( const double pt[],
                                            const TacsScalar *Xpts,
                                            const TacsScalar *vars,
                                            const TacsScalar *ubar,
                                            TacsScalar J[],
                                            TacsScalar e[] ){
  // Evaluate the product of the adjoint
  double N[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  double Na[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  double Nb[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  double Nc[MAX_ORDER*MAX_ORDER*MAX_ORDER];

  // Evaluate the basis functions on the original mesh
  forest->evalInterp(pt, N, Na, Nb, Nc);

  // First evaluate the contributions to the derivatives from
  // the regular element interpolation
  TacsScalar Ud[9], Xd[9];
  memset(Ud, 0, 9*sizeof(TacsScalar));
  memset(Xd, 0, 9*sizeof(TacsScalar));

  // Evaluate the derivative
  const TacsScalar *u = vars;
  const double *na = Na, *nb = Nb, *nc = Nc;
  const int ulen = order*order*order;
  for ( int i = 0; i < ulen; i++ ){
    // Compute the displacement gradient
    Ud[0] += Na[i]*u[0];
    Ud[1] += Nb[i]*u[0];
    Ud[2] += Nc[i]*u[0];

    Ud[3] += Na[i]*u[1];
    Ud[4] += Nb[i]*u[1];
    Ud[5] += Nc[i]*u[1];

    Ud[6] += Na[i]*u[2];
    Ud[7] += Nb[i]*u[2];
    Ud[8] += Nc[i]*u[2];
    u += 3;
  }

  // Evaluate the interpolation of the nodes using the basis functions
  // from the higher-order mesh
  interp_forest->evalInterp(pt, N, Na, Nb, Nc);

  const TacsScalar *x = Xpts;
  na = Na;  nb = Nb;  nc = Nc;
  const int xlen = (order+1)*(order+1)*(order+1);
  for ( int i = 0; i < xlen; i++ ){
    // Compute the inverse of the Jacobian transformation
    Xd[0] += Na[i]*x[0];
    Xd[1] += Nb[i]*x[0];
    Xd[2] += Nc[i]*x[0];

    Xd[3] += Na[i]*x[1];
    Xd[4] += Nb[i]*x[1];
    Xd[5] += Nc[i]*x[1];

    Xd[6] += Na[i]*x[2];
    Xd[7] += Nb[i]*x[2];
    Xd[8] += Nc[i]*x[2];
    x += 3;
  }

  // Invert the derivatives to obtain the Jacobian
  // transformation matrixx and its determinant
  TacsScalar detJ = inv3x3(Xd, J);

  // Evaluate the contribution from the enrichment functions
  double Nr[MAX_3D_ENRICH];
  double Nar[MAX_3D_ENRICH], Nbr[MAX_3D_ENRICH], Ncr[MAX_3D_ENRICH];
  if (order == 2){
    eval2ndEnrichmentFuncs3D(pt, Nr, Nar, Nbr, Ncr);
  }
  else if (order == 3){
    eval3rdEnrichmentFuncs3D(pt, Nr, Nar, Nbr, Ncr);
  }

  // Set the number of enrichment functions
  const int nenrich = getNum3dEnrich(order);

  // Add the contributions from the enrichment functions
  u = ubar;
  na = Nar; nb = Nbr; nc = Ncr;
  for ( int i = 0; i < nenrich; i++ ){
    Ud[0] += ubar[0]*na[i];
    Ud[1] += ubar[0]*nb[i];
    Ud[2] += ubar[0]*nc[i];

    Ud[3] += ubar[1]*na[i];
    Ud[4] += ubar[1]*nb[i];
    Ud[5] += ubar[1]*nc[i];

    Ud[6] += ubar[2]*na[i];
    Ud[7] += ubar[2]*nb[i];
    Ud[8] += ubar[2]*nc[i];

    ubar += 3;
  }

  // Compute the displacement gradient
  TacsScalar Ux[9];
  Ux[0] = Ud[0]*J[0] + Ud[1]*J[3] + Ud[2]*J[6];
  Ux[3] = Ud[3]*J[0] + Ud[4]*J[3] + Ud[5]*J[6];
  Ux[6] = Ud[6]*J[0] + Ud[7]*J[3] + Ud[8]*J[6];

  Ux[1] = Ud[0]*J[1] + Ud[1]*J[4] + Ud[2]*J[7];
  Ux[4] = Ud[3]*J[1] + Ud[4]*J[4] + Ud[5]*J[7];
  Ux[7] = Ud[6]*J[1] + Ud[7]*J[4] + Ud[8]*J[7];

  Ux[2] = Ud[0]*J[2] + Ud[1]*J[5] + Ud[2]*J[8];
  Ux[5] = Ud[3]*J[2] + Ud[4]*J[5] + Ud[5]*J[8];
  Ux[8] = Ud[6]*J[2] + Ud[7]*J[5] + Ud[8]*J[8];

  // Compute the strain
  e[0] = Ux[0];
  e[1] = Ux[4];
  e[2] = Ux[8];

  e[3] = Ux[5] + Ux[7];
  e[4] = Ux[2] + Ux[6];
  e[5] = Ux[1] + Ux[3];

  return detJ;
}
*/
/*
  Add the element contribution to the strain derivative
*/
/*
TacsScalar TMRStressConstraint::addStrainDeriv( const double pt[],
                                                const TacsScalar J[],
                                                const TacsScalar alpha,
                                                const TacsScalar dfde[],
                                                TacsScalar dfdu[],
                                                TacsScalar dfdubar[] ){
  // Evaluate the product of the adjoint
  double N[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  double Na[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  double Nb[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  double Nc[MAX_ORDER*MAX_ORDER*MAX_ORDER];
  forest->evalInterp(pt, N, Na, Nb, Nc);

  // Evaluate the contribution from the enrichment functions
  double Nr[MAX_3D_ENRICH];
  double Nar[MAX_3D_ENRICH], Nbr[MAX_3D_ENRICH], Ncr[MAX_3D_ENRICH];
  if (order == 2){
    eval2ndEnrichmentFuncs3D(pt, Nr, Nar, Nbr, Ncr);
  }
  if (order == 3){
    eval3rdEnrichmentFuncs3D(pt, Nr, Nar, Nbr, Ncr);
  }

  // Set the number of enrichment functions
  const int nenrich = getNum3dEnrich(order);

  // Evaluate the derivative
  const int len = order*order*order;
  const double *na = Na, *nb = Nb, *nc = Nc;
  for ( int i = 0; i < len; i++ ){
    TacsScalar Dx = na[0]*J[0] + nb[0]*J[3] + nc[0]*J[6];
    TacsScalar Dy = na[0]*J[1] + nb[0]*J[4] + nc[0]*J[7];
    TacsScalar Dz = na[0]*J[2] + nb[0]*J[5] + nc[0]*J[8];

    dfdu[0] += alpha*(dfde[0]*Dx + dfde[4]*Dz + dfde[5]*Dy);
    dfdu[1] += alpha*(dfde[1]*Dy + dfde[3]*Dz + dfde[5]*Dx);
    dfdu[2] += alpha*(dfde[2]*Dz + dfde[3]*Dy + dfde[4]*Dx);

    dfdu += 3;
    na++; nb++; nc++;
  }

  // Add the contributions from the enrichment functions
  na = Nar; nb = Nbr; nc = Ncr;
  for ( int i = 0; i < nenrich; i++ ){
    TacsScalar Dx = na[0]*J[0] + nb[0]*J[3] + nc[0]*J[6];
    TacsScalar Dy = na[0]*J[1] + nb[0]*J[4] + nc[0]*J[7];
    TacsScalar Dz = na[0]*J[2] + nb[0]*J[5] + nc[0]*J[8];

    dfdubar[0] += alpha*(dfde[0]*Dx + dfde[4]*Dz + dfde[5]*Dy);
    dfdubar[1] += alpha*(dfde[1]*Dy + dfde[3]*Dz + dfde[5]*Dx);
    dfdubar[2] += alpha*(dfde[2]*Dz + dfde[3]*Dy + dfde[4]*Dx);

    dfdubar += 3;
    na++; nb++; nc++;
  }

  return 0.0;
}
*/
/*
  Add the element contribution to the derivative dubar/du
*/
/*
void TMRStressConstraint::addEnrichDeriv( TacsScalar A[],
                                          TacsScalar dbdu[],
                                          TacsScalar dubardu[],
                                          TacsScalar dubar_duderiv[] ){

  // Get the number of enrichment functions
  const int nenrich = getNum3dEnrich(order);

  // Get vars per node and other dimensions
  const int vars_per_node = tacs->getVarsPerNode();
  const int num_nodes = order*order*order;
  const int neq = num_nodes*vars_per_node;

  // Set the matrix dimensions
  int m = nenrich;
  int n = neq;
  int p = num_nodes;

  TacsScalar *ATA = new TacsScalar[m*m]; // store A^T A
  TacsScalar *dbduT_A = new TacsScalar[p*m];

  // Compute A^T A
  TacsScalar a = 1.0, b = 0.0;
  BLASgemm("T", "N", &m, &m, &n, &a, A, &n, A, &n, &b, ATA, &m);

  // Factor A^T A
  int * ipiv = new int[ nenrich ];
  int info;
  LAPACKgetrf(&m, &m, ATA, &m, ipiv, &info);

  // Invert A^T A
  int lwork = 10*18;
  TacsScalar work[10*18];
  LAPACKgetri(&m, ATA, &m, ipiv, work, &lwork, &info);

  // Compute dubar_duderiv = (A^T A)^-1 A^T
  BLASgemm("N", "T", &m, &n, &m, &a, ATA, &m, A, &n, &b, dubar_duderiv, &m);

  // Evaluate dubar/du as (dubar/duderiv)(db/du)
  BLASgemm("N", "N", &m, &p, &n, &a, dubar_duderiv, &m, dbdu, &n, &b, dubardu,
&m);

  // Delete variables
  delete [] ATA;
  delete [] dbduT_A;
  delete [] ipiv;
}
*/

/*
  Output the von Misess stress from the reconstructed solution to a
  tecplot file
*/
/*
void TMRStressConstraint::writeReconToTec( TACSBVec *_uvec,
                                           const char *fname,
                                           TacsScalar ys ){
  const int vars_per_node = tacs->getVarsPerNode();

  // Copy the values
  uvec->copyValues(_uvec);

  // Distribute the variable values so that the non-owned values can
  // be accessed locally
  uvec->beginDistributeValues();
  uvec->endDistributeValues();

  // Compute the derivatives at the nodes
  computeNodeDeriv3D(forest, tacs, uvec, weights, uderiv);

  // Number of local elements
  const int nelems = tacs->getNumElements();

  // Get the quadrature points/weights
  const double *gaussPts, *gaussWts;
  int num_quad_pts =
    FElibrary::getGaussPtsWts(LOBATTO_QUADRATURE, order+1,
                              &gaussPts, &gaussWts);

  // Get the local connectivity for the higher-order mesh
  const int *conn = NULL;
  interp_forest->getNodeConn(&conn);

  // Get the higher-order points
  TMRPoint *X;
  interp_forest->getPoints(&X);

  // Create file to write out the von Misses stress to .dat file
  FILE *fp = fopen(fname, "w");
  fprintf(fp, "TITLE = \"Reconstruction Solution\"\n");
  fprintf(fp, "FILETYPE = FULL\n");
  fprintf(fp, "VARIABLES = X, Y, Z, svm\n");
  // fprintf(fp, "VARIABLES = \"X\", \"Y\", \"Z\", ");
  // fprintf(fp, "\"exx\", \"eyy\", \"ezz\", \"exy\", \"eyz\", \"exz\",
\"svm\"\n"); int num_tec_elems =
(num_quad_pts-1)*(num_quad_pts-1)*(num_quad_pts-1)*nelems; int num_tec_pts =
num_quad_pts*num_quad_pts*num_quad_pts*nelems; fprintf(fp, "ZONE ZONETYPE =
FEBRICK, N = %d, E = %d, DATAPACKING = POINT\n", num_tec_pts, num_tec_elems);

  for ( int i = 0; i < nelems; i++ ){
    // Get the element class and the variables associated with it
    TACSElement *elem = tacs->getElement(i, Xpts, vars, dvars, ddvars);

    // Get the constitutive relationship
    TACSConstitutive *con = elem->getConstitutive();

    // Get the node numbers and node locations for this element
    int len;
    const int *nodes;
    tacs->getElement(i, &len, &nodes);
    tacs->getElement(i, Xpts);

    // Retrieve the nodal values and nodal derivatives
    uvec->getValues(len, nodes, vars);
    uderiv->getValues(len, nodes, varderiv);

    // Now get the node locations for the locally refined mesh
    const int interp_elem_size = (order+1)*(order+1)*(order+1);
    for ( int j = 0; j < interp_elem_size; j++ ){
      int c = conn[interp_elem_size*i + j];
      int node = interp_forest->getLocalNodeNumber(c);
      Xpts[3*j] = X[node].x;
      Xpts[3*j+1] = X[node].y;
      Xpts[3*j+2] = X[node].z;
    }

    // Compute the values of the enrichment coefficient for each
    // degree of freedom
    computeElemRecon3D(vars_per_node, forest, interp_forest,
                       Xpts, vars, varderiv, ubar, tmp);

    // For each quadrature point, evaluate the strain at the
    // quadrature point and evaluate the stress constraint
    for ( int kk = 0; kk < num_quad_pts; kk++ ){
      for ( int jj = 0; jj < num_quad_pts; jj++ ){
        for ( int ii = 0; ii < num_quad_pts; ii++ ){
          // Pick the quadrature point at which we will
          // evaluate the strain
          double pt[3];
          pt[0] = gaussPts[ii];
          pt[1] = gaussPts[jj];
          pt[2] = gaussPts[kk];

          // Evaluate the strain
          TacsScalar J[9], e[6];
          evalStrain(pt, Xpts, vars, ubar, J, e);

          // Evaluate the failure criteria
          TacsScalar fval;
          con->failure(pt, e, &fval);

          // Compute von Misess stress from failure
          TacsScalar svm = fval*ys;

          // Write the value of von Misess stress
          TacsScalar Xpt[3] = {0.0, 0.0, 0.0};
          double N[MAX_ORDER*MAX_ORDER*MAX_ORDER];
          interp_forest->evalInterp(pt, N);
          for ( int k = 0; k < interp_elem_size; k++ ){
            Xpt[0] += Xpts[3*k]*N[k];
            Xpt[1] += Xpts[3*k+1]*N[k];
            Xpt[2] += Xpts[3*k+2]*N[k];
          }

          fprintf(fp, "%e %e %e %e\n",
                  Xpt[0], Xpt[1], Xpt[2], svm);
          // fprintf(fp, "%f %f %f %e %e %e %e %e %e %e\n",
          //        Xpt[0], Xpt[1], Xpt[2],
          //        e[0], e[1], e[2], e[3], e[4], e[5], svm);
        }
      }
    }
  }

  // Seperate the point data from the connectivity by a blank line
  fprintf(fp, "\n");

  // Write the element connectivity
  for ( int i = 0; i < nelems; i++ ){
    // Write the connectivity
    for ( int kk = 0; kk < num_quad_pts-1; kk++ ){
      for ( int jj = 0; jj < num_quad_pts-1; jj++ ){
        for ( int ii = 0; ii < num_quad_pts-1; ii++ ){
          int off = num_quad_pts*num_quad_pts*num_quad_pts*i + 1;
          fprintf(fp, "%d %d %d %d %d %d %d %d\n",
                  off + ii + jj*num_quad_pts + kk*num_quad_pts*num_quad_pts,
                  off + ii+1 + jj*num_quad_pts + kk*num_quad_pts*num_quad_pts,
                  off + ii+1 + (jj+1)*num_quad_pts +
kk*num_quad_pts*num_quad_pts, off + ii + (jj+1)*num_quad_pts +
kk*num_quad_pts*num_quad_pts, off + ii + jj*num_quad_pts +
(kk+1)*num_quad_pts*num_quad_pts, off + ii+1 + jj*num_quad_pts +
(kk+1)*num_quad_pts*num_quad_pts, off + ii+1 + (jj+1)*num_quad_pts +
(kk+1)*num_quad_pts*num_quad_pts, off + ii + (jj+1)*num_quad_pts +
(kk+1)*num_quad_pts*num_quad_pts);
        }
      }
    }
  }

  // Close the file
  fclose(fp);
}
*/
