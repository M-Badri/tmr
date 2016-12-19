#ifndef TMR_GEOMETRY_H
#define TMR_GEOMETRY_H

#include "TMRBase.h"

/*
  The following header file contains the interface for the geometry/
  topology for the TMR objects. These vertex/edge/surface and volume
  objects are used to map the 

  The vertex, edge, face and volume classes are used in conjunction
  with the TMROct(Quad)Forest class to evaluate nodal locations with
  the mesh. These interfaces are designed to be overriden with an
  external geometry engine.
*/

// Declare all of the geometry base classes
class TMRCurve;
class TMRSurface;

/*
  The vertex class: Note that this is used to store both the
  point and to represent the underlying geometry
*/
class TMRVertex : public TMREntity {
 public:
  TMRVertex(){}
  virtual ~TMRVertex(){}

  // Evalue the point
  virtual int evalPoint( TMRPoint *p ) = 0;

 private:
  // The topology information associated
  class CurveList {
  public:
    TMRCurve *curve;
    CurveList *next;
  } *curves;
};

/*
  The parametrization for a curve
*/
class TMRCurve : public TMREntity {
 public:
  TMRCurve(){ v1 = v2 = NULL; faces = NULL; }
  TMRCurve( TMRVertex *v1, TMRVertex *v2 );
  virtual ~TMRCurve();

  // Get the parameter range for this edge
  virtual void getRange( double *tmin, double *tmax ) = 0;
  
  // Given the parametric point, evaluate the x,y,z location
  virtual int evalPoint( double t, TMRPoint *X ) = 0;

  // Given the point, find the parametric location
  virtual int invEvalPoint( TMRPoint X, double *t );

  // Given the parametric point, evaluate the derivative 
  virtual int evalDeriv( double t, TMRPoint *Xt );
  
  // Retrive the connectivity
  void getVertices( TMRVertex **_v1, TMRVertex **_v2 );
  int addAdjSurface( TMRSurface *_surf );

  // Integrate along the edge and return an array containing
  // the parametric locations to provide an even spacing
  double integrate( double t1, double t2, double tol,
                    double **tvals, double **dist, int *nvals );

 private:
  // Set the step size
  static double deriv_step_size;

  // The start/end vertices of the curve
  TMRVertex *v1, *v2;
  
  // The list of adjacent surfaces referencing this curve
  class SurfaceList {
  public:
    TMRSurface *surf;
    SurfaceList *next;
  } *faces;
};

/*
  The parametrization of a surface
*/
class TMRSurface : public TMREntity {
 public:
  // Get the parameter range for this surface
  virtual void getRange( double *umin, double *vmin,
                         double *umax, double *vmax ) = 0;
 
  // Given the parametric point, compute the x,y,z location
  virtual int evalPoint( double u, double v, TMRPoint *X ) = 0;
  
  // Perform the inverse evaluation
  virtual int invEvalPoint( TMRPoint p, double *u, double *v ) = 0;

  // Given the parametric point, evaluate the first derivative 
  virtual int evalDeriv( double u, double v, 
                         TMRPoint *Xu, TMRPoint *Xv ) = 0;

 private:
  // Set the step size
  static double deriv_step_size;

  // The curves adjacent to this surface
  class CurveList {
    TMRCurve *curve;
    CurveList *next;
  } *curves;
};

/*
  Set the TMRVertex location based on a parametric location along a
  curve.  

  This takes either a parametric point or does an inverse evaluation
  first to determine the parametric location.
*/
class TMRVertexFromCurve : public TMRVertex {
 public:
  TMRVertexFromCurve( TMRCurve *_curve, double _t );
  TMRVertexFromCurve( TMRCurve *_curve, TMRPoint p );
  ~TMRVertexFromCurve();
  int evalPoint( TMRPoint *p );

 private:
  double t;
  TMRCurve *curve;
};

/*
  Evaluate a vertex location based on its parametric location on
  a surface.
*/
class TMRVertexFromSurface : public TMRVertex {
 public:
  TMRVertexFromSurface( TMRSurface *_surface, double _u, double _v );
  TMRVertexFromSurface( TMRSurface *_surface, TMRPoint p );
  ~TMRVertexFromSurface();
  int evalPoint( TMRPoint *p );

 private:
  double u, v;
  TMRSurface *surface;
};

/*
  Project a curve onto a surface and evaluate the surface location
*/
class TMRCurveFromSurfaceProjection : public TMRCurve {
  TMRCurveFromSurfaceProjection( TMRSurface *_surface, 
                                 TMRCurve *_curve );
  ~TMRCurveFromSurfaceProjection();  
  void getRange( double *tmin, double *tmax );
  int evalPoint( double t, TMRPoint *p );

 private:
  TMRCurve *curve;
  TMRSurface *surface;
};

#endif // TMR_GEOMETRY_H
