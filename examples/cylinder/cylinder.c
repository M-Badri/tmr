#include "TMRGeometry.h"
#include "TMRMesh.h"
#include "TMR_TACSCreator.h"
#include "TMRQuadForest.h"
#include "TACSAssembler.h"
#include "specialFSDTStiffness.h"
#include "MITCShell.h"
#include "TACSShellTraction.h"
#include "TACSToFH5.h"
#include "tacslapack.h"
#include <stdio.h>
#include <math.h>
#include "TMR_RefinementTools.h"

#ifdef TMR_HAS_OPENCASCADE

#include "TMROpenCascade.h"
#include <BRepPrimAPI_MakeCylinder.hxx>

/*
  Get the element and the auxiliary elements
*/
class TMRCylinderCreator : public TMRQuadTACSCreator {
 public:
  TMRCylinderCreator( double _alpha, double _beta, double _R,
                      double _load, FSDTStiffness *stiff ){
    alpha = _alpha;
    beta = _beta;
    R = _R;
    load = _load;
    elem2 = new MITCShell<2>(stiff); elem2->incref();
    elem3 = new MITCShell<3>(stiff); elem3->incref();
  }
  ~TMRCylinderCreator(){
    elem2->decref();
    elem3->decref();
  }

  TACSElement *createElement( int order,
                              TMRQuadForest *forest,
                              TMRQuadrant quad ){
    if (order == 2){ return elem2; }
    if (order == 3){ return elem3; }
  }
  TACSElement *createAuxElement( int order,
                                 TMRQuadForest *forest,
                                 TMRQuadrant quad ){
    // Get the points from the forest
    TMRPoint *Xp;
    forest->getPoints(&Xp);

    // Get the side-length of the quadrant
    const int32_t h = 1 << (TMR_MAX_LEVEL - quad.level - (order-2));

    // Set the tractions based on the node location
    TacsScalar tx[9], ty[9], tz[9];
    for ( int j = 0, n = 0; j < order; j++ ){
      for ( int i = 0; i < order; i++, n++ ){
        // Find the nodal index and determine the (x,y,z) location
        TMRQuadrant node;
        node.x = quad.x + h*i;
        node.y = quad.y + h*j;
        node.face = quad.face;
        forest->transformNode(&node);
        int index = forest->getNodeIndex(&node);
        
        // Set the pressure load
        double z = Xp[index].z;
        double theta =-R*atan2(Xp[index].y, Xp[index].x);
        double p = -load*sin(beta*z)*sin(alpha*theta);

        tx[n] = p*Xp[index].x/R;
        ty[n] = p*Xp[index].y/R;
        tz[n] = 0.0;
      }
    }

    // Create the traction class 
    TACSElement *trac = NULL;
    if (order == 2){
      trac = new TACSShellTraction<2>(tx, ty, tz);
    }
    else {
      trac = new TACSShellTraction<3>(tx, ty, tz);
    }

    return trac;
  }

 private:
  double alpha, beta;
  double R;
  double load;
  
  TACSElement *elem2, *elem3;
};

/*
  Compute the coefficients of a single term in a Fourier series for a
  specially orthotropic cylinder subjectedt to a sinusoidally varying
  pressure distribution specified as follows:

  p = sin(alpha*y)*cos(beta*x)

  Note that y = r*theta

  The coefficients U, V, W, theta and phi are for the expressions:
  
  u(x,y) = U*sin(alpha*y)*cos(beta*x)
  v(x,y) = V*cos(alpha*y)*sin(beta*x)
  w(x,y) = W*sin(alpha*y)*sin(beta*x)
  psi_x(x,y) = theta*sin(alpha*y)*cos(beta*x)
  psi_y(x,y) = phi*cos(alpha*y)*sin(beta*x)

  Note that u, v and w correspond to the axial, tangential and normal
  displacements in a reference frame attached to the shell surface -
  they are not the displacements expressed in the global reference
  frame. Likewise, psi_x and psi_y are the rotations of the normal
  along the x-direction and the tangential y-direction.
*/
void compute_coefficients( double *U, double *V, double *W, 
                           double *theta, double *phi,
                           double alpha, double beta, double ainv, 
                           double A11, double A12, double A22, double A33,
                           double D11, double D12, double D22, double D33,
                           double bA11, double bA22, double load ){

  double A[5*5], A2[5*5]; // 5 x 5 system of linear equations
  int ipiv[5];
  double rhs[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
  rhs[2] = - load;
 
  double B[8*5];
  memset(B, 0, sizeof(B));
  double * b = B;

  // Assign the columns for u
  b[0] = -beta;
  b[2] = alpha;
  b[5] = -alpha*ainv;
  b += 8;

  // Assign columns for v
  b[1] = -alpha;
  b[2] = beta;
  b[4] = alpha*ainv;
  b[6] = -ainv;
  b += 8;

  // Assign the columns for w
  b[1] = ainv;
  b[4] = -ainv*ainv;
  b[6] = alpha;
  b[7] = beta;
  b += 8;

  // Assign the columns for psi_x
  b[3] = -beta;
  b[5] = alpha;
  b[7] = 1.0;
  b += 8;

  // Assign the columns for psi_y
  b[4] = -alpha; 
  b[5] = beta;
  b[6] = 1.0;

  for ( int j = 0; j < 5; j++ ){
    double * bj = &B[8*j];
    for ( int i = 0; i < 5; i++ ){
      double * bi = &B[8*i];

      A2[i + 5*j] = 
        - ((bi[0]*(A11*bj[0] + A12*bj[1]) +  
            bi[1]*(A12*bj[0] + A22*bj[1]) + bi[2]*A33*bj[2]) +
           (bi[3]*(D11*bj[3] + D12*bj[4]) +
            bi[4]*(D12*bj[3] + D22*bj[4]) + bi[5]*D33*bj[5]) +
           bi[6]*bA11*bj[6] + bi[7]*bA22*bj[7]);
    }
  }

  // The first equation for u
  A[0]  = -(A11*beta*beta + A33*alpha*alpha) - D33*ainv*ainv*alpha*alpha;
  A[5]  = -(A33 + A12)*alpha*beta;
  A[10] = A12*beta*ainv;
  A[15] = D33*ainv*alpha*alpha;
  A[20] = D33*ainv*alpha*beta;

  // The second equation for v
  A[1]  = -(A12 + A33)*alpha*beta;
  A[6]  = -(A33*beta*beta + A22*alpha*alpha) - ainv*ainv*bA11 - D22*ainv*ainv*alpha*alpha;
  A[11] = (A22 + bA11)*ainv*alpha + D22*alpha*ainv*ainv*ainv;
  A[16] = D12*ainv*alpha*beta;
  A[21] = bA11*ainv + D22*ainv*alpha*alpha;

  // The third equation for w
  A[2]  = A12*beta*ainv;
  A[7]  = (bA11 + A22)*alpha*ainv + D22*alpha*ainv*ainv*ainv;
  A[12] = -(bA11*alpha*alpha + bA22*beta*beta) - A22*ainv*ainv - D22*ainv*ainv*ainv*ainv;
  A[17] = -bA22*beta - D12*beta*ainv*ainv;
  A[22] = -bA11*alpha - D22*alpha*ainv*ainv;

  // Fourth equation for theta
  A[3]  = D33*ainv*alpha*alpha;
  A[8]  = D12*ainv*alpha*beta;
  A[13] = -bA22*beta - D12*beta*ainv*ainv;
  A[18] = -(D11*beta*beta + D33*alpha*alpha) - bA22;
  A[23] = -(D12 + D33)*alpha*beta;

  // Fifth equation for phi
  A[4]  =  D33*ainv*alpha*beta;
  A[9]  =  bA11*ainv + D22*ainv*alpha*alpha;
  A[14] = -bA11*alpha - D22*alpha*ainv*ainv;
  A[19] = -(D33 + D12)*alpha*beta;
  A[24] = -(D33*beta*beta + D22*alpha*alpha) - bA11;

  int info = 0;
  int n = 5;
  int nrhs = 1;
  LAPACKgesv(&n, &nrhs, A2, &n, ipiv, rhs, &n, &info);

  // Solve for the coefficients 
  *U = rhs[0];
  *V = rhs[1];
  *W = rhs[2];
  *theta = rhs[3];
  *phi = rhs[4];
}

int main( int argc, char *argv[] ){
  MPI_Init(&argc, &argv);
  TMRInitialize();

  MPI_Comm comm = MPI_COMM_WORLD;
  int mpi_size, mpi_rank;
  MPI_Comm_rank(comm, &mpi_rank);
  MPI_Comm_size(comm, &mpi_size);

  int orthotropic_flag = 0;
  double htarget = 10.0;
  for ( int i = 0; i < argc; i++ ){
    if (sscanf(argv[i], "h=%lf", &htarget) == 1){
      printf("htarget = %f\n", htarget);
    }
  }

  // Set the shell geometry parameters
  double t = 1.0;
  double L = 100.0;
  double R = 100.0/M_PI;

  // Set the alpha/beta parameters
  double alpha = 4.0/R;
  double beta = 3*M_PI/L;

  // Set the load parameter
  double load = 1.0e3;

  // Set the yield stress
  double ys = 350e6;

  OrthoPly *ply = NULL;
  if (orthotropic_flag){
    // Set the material properties to use
    double rho = 1.0;
    double E1 = 100.0e9;
    double E2 =   5.0e9;
    double nu12 = 0.25;
    double G12 = 10.0e9;
    double G13 = 10.0e9;
    double G23 = 4.0e9;

    double Xt = 100.0e6;
    double Xc = 50.0e6;
    double Yt = 2.5e6;
    double Yc = 10.0e6;
    double S12 = 8.0e6;

    ply = new OrthoPly(t, rho, E1, E2, nu12, 
                       G12, G23, G13, 
                       Xt, Xc, Yt, Yc, S12);
    printf("Using orthotropic material properties: \n");
  }
  else {
    // Set the material properties to use
    double rho = 2700.0;
    double E = 70e9;
    double nu = 0.3;
  
    ply = new OrthoPly(t, rho, E, nu, ys);
    printf("Using isotropic material properties: \n");
  }

  // Create the stiffness relationship
  double kcorr = 5.0/6.0;
  FSDTStiffness *stiff = 
    new specialFSDTStiffness(ply, orthotropic_flag, t, kcorr);
  stiff->incref();
  ply->incref();

  // Set up the creator object - this facilitates creating the
  // TACSAssembler objects for different geometries
  TMRCylinderCreator *creator = new TMRCylinderCreator(alpha, beta, R,
                                                       load, stiff);

  // Create the geometry
  // Geom_CylindricalSurface(ax3, R);

  BRepPrimAPI_MakeCylinder cylinder(R, L);
  TopoDS_Compound compound;
  BRep_Builder builder;
  builder.MakeCompound(compound);
  builder.Add(compound, cylinder.Shape());

  // Load in the geometry
  TMRModel *geo = TMR_LoadModelFromCompound(compound);

  int num_edges;
  TMREdge **edges;
  geo->getEdges(&num_edges, &edges);
  edges[0]->setAttribute("Edge1");
  edges[1]->setAttribute("Edge2");
  edges[2]->setAttribute("Edge3");

  // Set the boundary conditions
  int bcs[] = {0, 1, 2, 3, 4, 5};
  creator->addBoundaryCondition("Edge1", 6, bcs);
  creator->addBoundaryCondition("Edge3", 6, bcs);

  if (geo){
    geo->incref();

    // Get the faces that have been created - if any
    // and write them all to different VTK files
    int num_faces;
    TMRFace **faces;
    geo->getFaces(&num_faces, &faces);

    // Allocate the new mesh
    TMRMesh *mesh = new TMRMesh(comm, geo);
    mesh->incref();
    mesh->mesh(htarget);
    mesh->writeToVTK("cylinder-mesh.vtk");

    TMRModel *model = mesh->createModelFromMesh();
    model->incref();

    TMRQuadForest *forest = new TMRQuadForest(comm);
    forest->incref();

    TMRTopology *topo = new TMRTopology(comm, model);
    forest->setTopology(topo);
    forest->createTrees(1);

    double target_err = 1e-4;

    for ( int k = 0; k < 3; k++ ){
      // Create the TACSAssembler object associated with this forest
      TACSAssembler *tacs = creator->createTACS(3, forest);
      tacs->incref();
    
      // Create matrix and vectors 
      TACSBVec *ans = tacs->createVec();
      TACSBVec *res = tacs->createVec();
      FEMat *mat = tacs->createFEMat();

      // Increment reference count to the matrix/vectors
      ans->incref();
      res->incref();
      mat->incref();
    
      // Allocate the factorization
      int lev = 10000;
      double fill = 10.0;
      int reorder_schur = 1;
      PcScMat *pc = new PcScMat(mat, lev, fill, reorder_schur); 
      pc->incref();
    
      // Assemble and factor the stiffness/Jacobian matrix
      double alpha = 1.0, beta = 0.0, gamma = 0.0;
      tacs->assembleJacobian(alpha, beta, gamma, res, mat);
      pc->factor();

      // Get solution and store in ans
      pc->applyFactor(res, ans);
      ans->scale(-1.0);
      tacs->setVariables(ans);

      // Create and write out an fh5 file
      unsigned int write_flag = (TACSElement::OUTPUT_NODES |
                                 TACSElement::OUTPUT_DISPLACEMENTS);
      TACSToFH5 *f5 = new TACSToFH5(tacs, SHELL, write_flag);
      f5->incref();
      
      // Write out the solution
      f5->writeToFile("output.f5");
      f5->decref();

      TMR_StrainEnergyRefine(tacs, forest, target_err);

      tacs->decref();
      mat->decref();
      res->decref();
      ans->decref();
      pc->decref();
    }

    forest->decref();
    model->decref();
    mesh->decref();
    geo->decref();
  }

  TMRFinalize();
  MPI_Finalize();
  return (0);
}

#endif // TMR_HAS_OPENCASCADE
