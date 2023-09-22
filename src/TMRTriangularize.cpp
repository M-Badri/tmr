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

#include "TMRTriangularize.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "TMRHashFunction.h"
#include "TMRMesh.h"
#include "TMRMeshSmoothing.h"
#include "TMRPerfectMatchInterface.h"
#include "predicates.h"
#include "tmrlapack.h"

// Include C++ stdlib for priority queue
#include <queue>
#include <vector>

// Define the maximum distance
const double MAX_QUAD_DISTANCE = 1e40;

/*
  Compare coordinate pairs of points. This uses a Morton ordering
  comparison to facilitate sorting/searching the list of edges.

  This function can be used by the stdlib functions qsort and
  bsearch to sort/search the edge pairs.
*/
static int compare_edges(const void *avoid, const void *bvoid) {
  // Cast the input to uint32_t types
  const uint32_t *a = static_cast<const uint32_t *>(avoid);
  const uint32_t *b = static_cast<const uint32_t *>(bvoid);

  // Extract the x/y locations for the a and b points
  uint32_t ax = a[0], ay = a[1];
  uint32_t bx = b[0], by = b[1];

  uint32_t xxor = ax ^ bx;
  uint32_t yxor = ay ^ by;
  uint32_t sor = xxor | yxor;

  // Note that here we do not distinguish between levels
  // Check for the most-significant bit
  int discrim = 0;
  if (xxor > (sor ^ xxor)) {
    discrim = ax - bx;
  } else {
    discrim = ay - by;
  }

  return discrim;
}

/*
  A light-weight queue for keeping track of groups of triangles
*/
class TriQueueNode {
 public:
  TMRTriangle *tri;
  TriQueueNode *next;
};

/*
  The triangle priority queue

  This enables the storage of a list of triangles that can
  be accessed
*/
class TriQueue {
 public:
  // Initialize the priority queue without and data
  TriQueue() {
    start = end = NULL;
    size = 0;
  }

  // Free all of the queue entries
  ~TriQueue() {
    while (start) {
      TriQueueNode *tmp = start->next;
      delete start;
      start = tmp;
    }
  }

  // Pop the first triangle pointer off the queue and delete its
  // corresponding node. This is used to extract the first node in the
  // list.
  TMRTriangle *pop() {
    TMRTriangle *t = NULL;
    if (start) {
      t = start->tri;
      TriQueueNode *tmp = start;
      start = start->next;
      delete tmp;
      size--;
    }
    return t;
  }

  // Append the given triangle pointer to the end of the priority
  // queue. If the size of the queue is zero, then readjust the
  // start/end pointers and the size accordingly.
  void append(TMRTriangle *t) {
    if (size == 0) {
      end = new TriQueueNode();
      end->tri = t;
      end->next = NULL;
      start = end;
      size++;
    } else {
      end->next = new TriQueueNode();
      end = end->next;
      end->tri = t;
      end->next = NULL;
      size++;
    }
  }

  // Delete the *next* entry in the list after the provided
  // pointer. Note that this is required since we only have a pointer
  // to the next entry in the list (and not the previous one). This
  // function returns 1 for success and 0 for failure.
  int deleteNext(TriQueueNode *node) {
    if (node && node->next) {
      // Pointer to the entry to be deleted
      TriQueueNode *next = node->next;

      // Adjust the 'next' pointer to point past the entry that will
      // be deleted
      node->next = next->next;

      // Decrease the length of the queue
      size--;

      // Delete the entry
      delete next;
      return 1;
    }

    // The node cannot be deleted
    return 0;
  }

  // Note that we grant public access to these members. Use this
  // access wisely!
  int size;
  TriQueueNode *start;
  TriQueueNode *end;
};

/*
  The following class implments a simple quadtree data structure for fast
  geometric searching queries.
*/
TMRQuadNode::TMRQuadNode(TMRQuadDomain *_domain) {
  initialize(_domain, 0, 0, 0);
}

/*
  Create a child node
*/
TMRQuadNode::TMRQuadNode(TMRQuadDomain *_domain, uint32_t _u, uint32_t _v,
                         int _level) {
  initialize(_domain, _u, _v, _level);
}

/*
  Initialize the data for the quadtree root/node/leaf
*/
void TMRQuadNode::initialize(TMRQuadDomain *_domain, uint32_t _u, uint32_t _v,
                             int _level) {
  // Set the domain
  domain = _domain;

  // Set the level of this quadndoe
  level = _level;

  // Set the domain level
  u = _u;
  v = _v;

  // Set the maximum length in parameter space for
  // the domain
  const uint32_t hmax = 1 << MAX_DEPTH;
  const uint32_t h = 1 << (MAX_DEPTH - level - 1);

  // Compute the distance along each edge for this node
  // within the quadtree
  double ax = 1.0 * (u + h) / hmax;
  double ay = 1.0 * (v + h) / hmax;

  // Set the new planes that spatially divide this node
  x = (1.0 - ax) * domain->xlow + ax * domain->xhigh;
  y = (1.0 - ay) * domain->ylow + ay * domain->yhigh;

  // Set the pointers to the different children (all to NULL for now)
  low_left = NULL;
  low_right = NULL;
  up_left = NULL;
  up_right = NULL;

  // Allocate space for the points that will be added
  num_points = 0;
  pts = new double[2 * NODES_PER_LEVEL];
  pt_nums = new uint32_t[NODES_PER_LEVEL];
}

/*
  Free the quadtree node and its data
*/
TMRQuadNode::~TMRQuadNode() {
  if (pts) {
    delete[] pts;
  }
  if (pt_nums) {
    delete[] pt_nums;
  }
  if (low_left) {
    delete low_left;
    delete low_right;
    delete up_left;
    delete up_right;
  }
}

/*
  Add a node to the quadtree.

  This code does not check for duplicated geometric entities at the
  same point or duplicated indices. It is the user's responsibility
  not to add duplicates.
*/
void TMRQuadNode::addNode(uint32_t num, const double pt[]) {
  // If any of the children have been allocated, searh them for
  // the place where the node should be added
  if (low_left) {
    if (pt[0] <= x && pt[1] <= y) {
      low_left->addNode(num, pt);
    } else if (pt[0] <= x) {
      up_left->addNode(num, pt);
    } else if (pt[1] <= y) {
      low_right->addNode(num, pt);
    } else {
      up_right->addNode(num, pt);
    }
    return;
  }

  // Check if adding this node would exceed the number of nodes that
  // are allowed per level
  if (num_points < NODES_PER_LEVEL) {
    pts[2 * num_points] = pt[0];
    pts[2 * num_points + 1] = pt[1];
    pt_nums[num_points] = num;
    num_points++;
    return;
  } else {
    // Allocate new children for the new nodes
    const uint32_t h = 1 << (MAX_DEPTH - level - 1);

    low_left = new TMRQuadNode(domain, u, v, level + 1);
    low_right = new TMRQuadNode(domain, u + h, v, level + 1);
    up_left = new TMRQuadNode(domain, u, v + h, level + 1);
    up_right = new TMRQuadNode(domain, u + h, v + h, level + 1);

    // Add the points to myself
    for (int k = 0; k < NODES_PER_LEVEL; k++) {
      addNode(pt_nums[k], &pts[2 * k]);
    }
    addNode(num, pt);

    // Free the space that was allocated
    delete[] pt_nums;
    delete[] pts;
    pt_nums = NULL;
    pts = NULL;
    num_points = 0;
  }
}

/*
  Delete a point from the quadtree.

  Note that the coordinate provided is used to match the point, but
  needs to be used to scan through the quadtree hierarchy to find the
  correct quadrant location.
*/
int TMRQuadNode::deleteNode(uint32_t num, const double pt[]) {
  if (low_left) {
    if (pt[0] <= x && pt[1] <= y) {
      return low_left->deleteNode(num, pt);
    } else if (pt[0] <= x) {
      return up_left->deleteNode(num, pt);
    } else if (pt[1] <= y) {
      return low_right->deleteNode(num, pt);
    } else {
      return up_right->deleteNode(num, pt);
    }
  }

  // The node could not be deleted because no nodes exist on this
  // level.
  if (num_points == 0) {
    return 0;
  }

  // Scan through the list of nodes, and check for the one that
  // needs to be deleted.
  int i = 0;
  for (; i < num_points; i++) {
    if (pt_nums[i] == num) {
      break;
    }
  }

  // We ended up past the last entry, therefore the point was not found
  if (i == num_points) {
    return 0;
  }

  // Increment the index one past the node that will be eliminated
  i++;
  for (; i < num_points; i++) {
    // Move all the data back to cover the deleted entry
    pt_nums[i - 1] = pt_nums[i];
    pts[2 * (i - 1)] = pts[2 * i];
    pts[2 * (i - 1) + 1] = pts[2 * i + 1];
  }
  // Decrease the number of points to account for the deleted node
  num_points--;

  // The point was found
  return 1;
}

/*
  Find the closest indexed point to the provided (x,y) location

  This function is the top-level function that calls the recursive
  version after initializing the data.
*/
uint32_t TMRQuadNode::findClosest(const double pt[], double *_dist) {
  double dist = MAX_QUAD_DISTANCE;
  uint32_t index = 0;

  // Make the recursive call
  findClosest(pt, &index, &dist);
  if (_dist) {
    *_dist = dist;
  }
  return index;
}

/*
  The recursive call to find the closest point

  This scans each quadrant, testing whether it will be necessary to
  scan multiple quadrants to ensure that we have, in fact, located the
  closest point to the provided point.
*/
void TMRQuadNode::findClosest(const double pt[], uint32_t *index,
                              double *dist) {
  // Scan through the quadtree
  if (low_left) {
    if (pt[0] <= x && pt[1] <= y) {
      low_left->findClosest(pt, index, dist);
      if (x - pt[0] <= *dist) {
        low_right->findClosest(pt, index, dist);
      }
      if (y - pt[1] <= *dist) {
        up_left->findClosest(pt, index, dist);
      }
      if (x - pt[0] <= *dist && y - pt[1] <= *dist) {
        up_right->findClosest(pt, index, dist);
      }
    } else if (pt[0] <= x) {
      up_left->findClosest(pt, index, dist);
      if (x - pt[0] <= *dist) {
        up_right->findClosest(pt, index, dist);
      }
      if (pt[1] - y <= *dist) {
        low_left->findClosest(pt, index, dist);
      }
      if (x - pt[0] <= *dist && pt[1] - y <= *dist) {
        low_right->findClosest(pt, index, dist);
      }
    } else if (pt[1] <= y) {
      low_right->findClosest(pt, index, dist);
      if (pt[0] - x <= *dist) {
        low_left->findClosest(pt, index, dist);
      }
      if (y - pt[1] <= *dist) {
        up_right->findClosest(pt, index, dist);
      }
      if (pt[0] - x <= *dist && y - pt[1] <= *dist) {
        up_left->findClosest(pt, index, dist);
      }
    } else {
      up_right->findClosest(pt, index, dist);
      if (pt[0] - x <= *dist) {
        up_left->findClosest(pt, index, dist);
      }
      if (pt[1] - y <= *dist) {
        low_right->findClosest(pt, index, dist);
      }
      if (pt[0] - x <= *dist && pt[1] - y <= *dist) {
        low_left->findClosest(pt, index, dist);
      }
    }
    return;
  }

  // This is a leaf, search the points
  for (int i = 0; i < num_points; i++) {
    double dx = pt[0] - pts[2 * i];
    double dy = pt[1] - pts[2 * i + 1];
    double d = sqrt(dx * dx + dy * dy);
    if (d < *dist) {
      *dist = d;
      *index = pt_nums[i];
    }
  }
}

/*
  Create the triangularization object.

  This code uses Schewchuk's geometric predicates for testing whether
  points lie within a triangle's circumcircle or whether a point forms
  a properly oriented triangle (orient2d).

  This object takes in a series of points, a nuber of holes, and a
  series of segments. The points are in parametric space while the
  face defines the mapping between parameter space and physical
  space. Meshing is performed by using a local metric within each
  triangle to define distance. The incircle operations that test
  whether a point lies within the circumcircle of a triangle are
  modified to reflect this local metric. (This is not completely
  justified theoretically, but it works in practice.) The orient2d
  operations still take place in parameter space, since their purpose
  is to test whether the point lies within the triangle.

  The holes in the domain must be specified so that the triangulation
  does not cover them. The boundary of the hole itself must be
  represented by the segments and must be closed. The points
  indicating the location of the holes must be numbered last in the
  point list and cannot be included in the segment edges.

  If no face object is provided, then the meshing takes place in
  the parameter space directly.

  input:
  npts:   the number of points (including holes) in the domain
  inpts:  the parametric input points
  nholes: the number of holes that are specified
  nsegs:  the number of segments
  segs:   the segments: consecutive point numbers indicating edges
  surf:   the non-optional face
*/
TMRTriangularize::TMRTriangularize(int npts, const double inpts[], int nsegs,
                                   const int segs[], TMRFace *surf) {
  initialize(npts, inpts, 0, nsegs, segs, surf);
}

/*
  Alternate constructor: Triangularize with holes
*/
TMRTriangularize::TMRTriangularize(int npts, const double inpts[], int nholes,
                                   int nsegs, const int segs[], TMRFace *surf) {
  initialize(npts, inpts, nholes, nsegs, segs, surf);
}

/*
  The 'true' constructor. This is called, depending on which arguments
  are provided.
*/
void TMRTriangularize::initialize(int npts, const double inpts[], int nholes,
                                  int nsegs, const int segs[], TMRFace *surf) {
  // Initialize the predicates code
  exactinit();

  // Set the surface
  face = surf;
  face->incref();

  // Allocate and initialize/zero the hash table data for the edges
  num_hash_nodes = 0;
  num_buckets = 100;
  buckets = new EdgeHashNode *[num_buckets];
  memset(buckets, 0, num_buckets * sizeof(EdgeHashNode *));

  // Set the initial root/current location of the doubly-linked
  // triangle list structure. These are allocated as we add new
  // triangles.
  list_start = NULL;
  list_end = NULL;

  // Keep track of the total number of triangles
  num_triangles = 0;

  // Set the initial number of points
  init_boundary_points = npts - nholes;
  num_points = FIXED_POINT_OFFSET + npts;

  // Set the initial for the maximum number of points
  max_num_points = 1024;
  if (max_num_points < num_points) {
    max_num_points = num_points;
  }

  // Allocate the initial set of points
  pts = new double[2 * max_num_points];
  pts_to_tris = new TMRTriangle *[max_num_points];

  // If we have a face object
  X = new TMRPoint[max_num_points];

  // Find the maximum domain size
  domain.xlow = domain.xhigh = inpts[0];
  domain.ylow = domain.yhigh = inpts[1];
  for (int i = 1; i < npts; i++) {
    if (inpts[2 * i] < domain.xlow) {
      domain.xlow = inpts[2 * i];
    }
    if (inpts[2 * i + 1] < domain.ylow) {
      domain.ylow = inpts[2 * i + 1];
    }
    if (inpts[2 * i] > domain.xhigh) {
      domain.xhigh = inpts[2 * i];
    }
    if (inpts[2 * i + 1] > domain.yhigh) {
      domain.yhigh = inpts[2 * i + 1];
    }
  }

  // Re-adjust the domain boundary to ensure that it is sufficiently
  // large
  double xsmall = 10.0 * (domain.xhigh - domain.xlow);
  domain.xhigh += xsmall;
  domain.xlow -= xsmall;

  double ysmall = 10.0 * (domain.yhigh - domain.ylow);
  domain.yhigh += ysmall;
  domain.ylow -= ysmall;

  // Allocate the new root mode
  root = new TMRQuadNode(&domain);
  search_tag = 0;

  // Set up the PSLG edges
  setUpPSLGEdges(nsegs, segs);

  // Set the initial points
  num_points = FIXED_POINT_OFFSET;

  // Set the point (xlow, ylow)
  pts[0] = domain.xlow;
  pts[1] = domain.ylow;
  // Set the point (xhigh, ylow)
  pts[2] = domain.xhigh;
  pts[3] = domain.ylow;
  // Set the point (xlow, yhigh)
  pts[4] = domain.xlow;
  pts[5] = domain.yhigh;
  // Set the point (xhigh, yhigh)
  pts[6] = domain.xhigh;
  pts[7] = domain.yhigh;

  // Evaluate the points on the face
  for (int i = 0; i < num_points; i++) {
    X[i].x = X[i].y = X[i].z = 0.0;
  }

  // Add the extreme points to the quadtree
  for (int i = 0; i < FIXED_POINT_OFFSET; i++) {
    root->addNode(i, &pts[2 * i]);
  }

  // Add the initial triangles
  addTriangle(TMRTriangle(0, 1, 2));
  addTriangle(TMRTriangle(2, 1, 3));

  // Add the points to the triangle. This creates a CDT of the
  // original set of points.
  for (int i = 0; i < npts; i++) {
    addPointToMesh(&inpts[2 * i], NULL);
  }

  // Ensure that all the segments are in the triangulation to
  // recover a CDT
  for (int i = 0; i < nsegs; i++) {
    uint32_t u = 0, v = 0;
    if (segs[2 * i] >= 0) {
      u = segs[2 * i] + FIXED_POINT_OFFSET;
    }
    if (segs[2 * i + 1] >= 0) {
      v = segs[2 * i + 1] + FIXED_POINT_OFFSET;
    }

    // Check to see if the corresponding triangle exists
    TMRTriangle *tri;
    completeMe(u, v, &tri);
    if (!tri) {
      insertSegment(u, v);
    }
  }

  // Set the triangle tags to zero
  setTriangleTags(0);

  // Mark all the triangles in the list that contain or touch nodes
  // that are in the FIXED_POINT_OFFSET list that are not separated by
  // a PSLG edge. These triangles will be deleted.
  TriListNode *node = list_start;
  uint32_t max_node_num = num_points - nholes;
  while (node) {
    if (node->tri.status != DELETE_ME && node->tri.tag == 0 &&
        ((node->tri.u < FIXED_POINT_OFFSET ||
          node->tri.v < FIXED_POINT_OFFSET ||
          node->tri.w < FIXED_POINT_OFFSET) ||
         (node->tri.u >= max_node_num || node->tri.v >= max_node_num ||
          node->tri.w >= max_node_num))) {
      tagTriangles(&node->tri);
    }
    node = node->next;
  }

  // Free the triangles that have been tagged
  node = list_start;
  while (node) {
    if (node->tri.tag == 1) {
      deleteTriangle(node->tri);
    }
    node = node->next;
  }

  // Free the trianlges marked for deletion from the list
  deleteTrianglesFromList();

  // Free the points and holes from the quadtree
  for (int num = 0; num < FIXED_POINT_OFFSET; num++) {
    root->deleteNode(num, &pts[2 * num]);
  }

  // Free the points associated with the number of holes
  for (int num = num_points - nholes; num < num_points; num++) {
    root->deleteNode(num, &pts[2 * num]);
  }
  num_points -= nholes;

  // Perform the delaunay edge flip algorithm
  delaunayEdgeFlip();

  // Free the trianlges marked for deletion from the list
  deleteTrianglesFromList();

  // Reset the node->traingle pointers to avoid referring to a
  // triangle that belonged to a hole and was deleted.
  node = list_start;
  memset(pts_to_tris, 0, num_points * sizeof(TMRTriangle *));
  while (node) {
    pts_to_tris[node->tri.u] = &(node->tri);
    pts_to_tris[node->tri.v] = &(node->tri);
    pts_to_tris[node->tri.w] = &(node->tri);
    node = node->next;
  }
}

/*
  Free the triangularization object
*/
TMRTriangularize::~TMRTriangularize() {
  if (root) {
    delete root;
  }
  delete[] pts;
  delete[] X;
  delete[] pts_to_tris;

  // Dereference the face
  face->decref();

  // Free the PSLG edges
  if (pslg_edges) {
    delete[] pslg_edges;
  }

  // Free the data for the edge hash table
  for (int i = 0; i < num_buckets; i++) {
    EdgeHashNode *node = buckets[i];
    while (node) {
      EdgeHashNode *tmp = node;
      node = node->next;
      delete tmp;
    }
  }
  delete[] buckets;

  // Free the doubly linked edge list
  while (list_start) {
    TriListNode *tmp = list_start;
    list_start = list_start->next;
    delete tmp;
  }
}

/*
  Construct a delaunay triangulation using the edge flip algorithm.

  This code adds all edges that are not in the PSLG to a queue. As
  edges are popped from the queue, they are checked if the adjacent
  triangles are delaunay. If they fail the test, then an edge flip is
  performed.

  This code performs an edge flip with the edge (u, v). If the edge is
  on the boundary or in the PSLG, then the edge flip is not
  performed. If the resulting triangles from the edge flip are not
  well-oriented, then the edge flip is not performed.
*/
void TMRTriangularize::delaunayEdgeFlip() {
  std::queue<TriEdge> q;

  TriListNode *node = list_start;
  while (node) {
    uint32_t u = node->tri.u;
    uint32_t v = node->tri.v;
    uint32_t w = node->tri.w;

    // Push only the internal edges that have the first node number
    // less than the second node number
    if (!edgeInPSLG(u, v)) {
      if (u < v) {
        q.push(TriEdge(u, v));
      }
    }
    if (!edgeInPSLG(v, w)) {
      if (v < w) {
        q.push(TriEdge(v, w));
      }
    }
    if (!edgeInPSLG(w, u)) {
      if (w < u) {
        q.push(TriEdge(w, u));
      }
    }
    node = node->next;
  }

  while (!q.empty()) {
    // Pop the front edge object off the queue and extract the (u, v)
    // node numbers for the edge
    const TriEdge edge = q.front();
    uint32_t u = edge.u;
    uint32_t v = edge.v;
    q.pop();

    // Find the two triangles with the u-v and v-u edges
    TMRTriangle *t1, *t2;
    completeMe(u, v, &t1);
    completeMe(v, u, &t2);

    // If both t1 and t2 exist and the edge is not in the planar
    // straight-line graph then the edge is a candidate for flipping.
    if (t1 && t2 && !edgeInPSLG(u, v)) {
      // There are two existing triangles in the mesh (u, v, w) and
      // (v, u, x). First we find the w and x node numbers.
      uint32_t w = 0, x = 0;
      if (v == t1->u) {
        w = t1->v;
      } else if (v == t1->v) {
        w = t1->w;
      } else {
        w = t1->u;
      }

      if (u == t2->u) {
        x = t2->v;
      } else if (u == t2->v) {
        x = t2->w;
      } else {
        x = t2->u;
      }

      // Delete the existing triangles from the mesh. And add the
      // triangles (x, w, u) and (w, x, v) if they form right-handed
      // triangles.
      if (orient2d(&pts[2 * x], &pts[2 * w], &pts[2 * u]) > 0.0 &&
          orient2d(&pts[2 * w], &pts[2 * x], &pts[2 * v]) > 0.0) {
        // Perform the incircle test. This test passes only if both
        // triangles agree that they are incircled. And the new
        // triangles agree that they are not encircled
        int not_delaunay = (inCircle(u, v, w, x, face) >= 0.0 &&
                            inCircle(v, u, x, w, face) >= 0.0);
        int delaunay = (inCircle(x, w, u, v, face) < 0.0 &&
                        inCircle(w, x, v, u, face) < 0.0);

        if (not_delaunay && delaunay) {
          // Delete the existing triangles
          deleteTriangle(*t1);
          deleteTriangle(*t2);

          // Flip the edges
          addTriangle(TMRTriangle(x, w, u));
          addTriangle(TMRTriangle(w, x, v));

          // Add the new triangles
          q.push(TriEdge(u, x));
          q.push(TriEdge(x, v));
          q.push(TriEdge(v, w));
          q.push(TriEdge(w, u));
        }
      }
    }
  }
}

/*
  Compare sorted edges

  This code compares two edges together. This relies on the edges being
  pre-modified such that the lowest edge number appears first. The first
  index in the edge is compared first, and the second edge number second.
*/
static int compare_degen_edges(const void *avoid, const void *bvoid) {
  // Cast the input to uint32_t types
  const uint32_t *a = static_cast<const uint32_t *>(avoid);
  const uint32_t *b = static_cast<const uint32_t *>(bvoid);

  // Extract the x/y locations for the a and b points
  uint32_t ax = a[0], ay = a[1];
  uint32_t bx = b[0], by = b[1];

  if (ax == bx) {
    return ax - bx;
  }
  return ay - by;
}

/*
  Remove degenerate edges and extra nodes from the triangulation

  Note that this code adjusts the degen[] array so it is not constant.
*/
void TMRTriangularize::removeDegenerateEdges(int num_degen, const int degen[]) {
  if (num_degen > 0) {
    // Sort the degenerate edges
    uint32_t *sorted_degen = new uint32_t[2 * num_degen];
    for (int i = 0; i < num_degen; i++) {
      sorted_degen[2 * i] = degen[2 * i] + FIXED_POINT_OFFSET;
      sorted_degen[2 * i + 1] = degen[2 * i + 1] + FIXED_POINT_OFFSET;

      // Flip the degenerate edge order so that the larger number
      // appears first in the edge list
      if (sorted_degen[2 * i + 1] > sorted_degen[2 * i]) {
        uint32_t tmp = sorted_degen[2 * i];
        sorted_degen[2 * i] = sorted_degen[2 * i + 1];
        sorted_degen[2 * i + 1] = tmp;
      }
    }
    qsort(sorted_degen, num_degen, 2 * sizeof(uint32_t), compare_degen_edges);

    for (int i = 0; i < num_degen; i++) {
      uint32_t u = sorted_degen[2 * i];
      uint32_t v = sorted_degen[2 * i + 1];

      // Keep track of whether we actually delete a triangle. If not,
      // we may run into problems so we report an error.
      int fail = 1;

      TMRTriangle *t;
      completeMe(u, v, &t);
      if (t) {
        deleteTriangle(*t);
        fail = 0;
      }
      completeMe(v, u, &t);
      if (t) {
        deleteTriangle(*t);
        fail = 0;
      }
      if (fail) {
        fprintf(stderr,
                "TMRTriangularize Error: "
                "Failed to find degenerate edge (%d, %d)\n",
                degen[2 * i], degen[2 * i + 1]);
      }
    }

    // Free the deleted triangles from the list
    deleteTrianglesFromList();

    // Find a mapping between the old node numbers and the new
    // condensed node number list
    uint32_t *old_to_new = new uint32_t[num_points];
    int count = 0;
    for (int i = 0, j = 0; i < num_points; i++) {
      uint32_t num = i;
      if (j < num_degen && sorted_degen[2 * j] == num) {
        old_to_new[i] = old_to_new[sorted_degen[2 * j + 1]];
        j++;
      } else {
        old_to_new[i] = count;
        if (count != i) {
          X[count] = X[i];
          pts[2 * count] = pts[2 * i];
          pts[2 * count + 1] = pts[2 * i + 1];
        }
        count++;
      }
    }

    // Readjust the number of points
    num_points = count;

    // Now, readjust the node numbers in the triangle
    TriListNode *node = list_start;
    while (node) {
      node->tri.u = old_to_new[node->tri.u];
      node->tri.v = old_to_new[node->tri.v];
      node->tri.w = old_to_new[node->tri.w];
      node = node->next;
    }

    // Free the data
    delete[] old_to_new;
    delete[] sorted_degen;
  }
}

/*
  Retrieve the underlying mesh
*/
void TMRTriangularize::getMesh(int *_num_points, int *_num_triangles,
                               int **_conn, double **_pts, TMRPoint **_X) {
  // Set the number of points/triangles
  int npts = (num_points - FIXED_POINT_OFFSET);
  if (_num_points) {
    *_num_points = npts;
  }
  if (_num_triangles) {
    *_num_triangles = num_triangles;
  }

  // Set the parametric point locations
  if (_pts) {
    *_pts = new double[2 * npts];
    memcpy(*_pts, &pts[2 * FIXED_POINT_OFFSET], 2 * npts * sizeof(double));
  }

  // Set the physical node locations
  if (_X) {
    *_X = new TMRPoint[npts];
    memcpy(*_X, &X[FIXED_POINT_OFFSET], npts * sizeof(TMRPoint));
  }

  if (_conn) {
    // Set the pointer into the connectivity array
    *_conn = new int[3 * num_triangles];
    int *t = *_conn;

    // Determine the connectivity
    TriListNode *node = list_start;
    while (node) {
      t[0] = node->tri.u - FIXED_POINT_OFFSET;
      t[1] = node->tri.v - FIXED_POINT_OFFSET;
      t[2] = node->tri.w - FIXED_POINT_OFFSET;
      t += 3;
      node = node->next;
    }
  }
}

/*
  Reset the tags of all the triangles within the list
*/
void TMRTriangularize::setTriangleTags(uint32_t tag) {
  TriListNode *node = list_start;
  while (node) {
    node->tri.tag = tag;
    node = node->next;
  }
}

/*
  Mark triangles that should be deleted.

  This is used to mark triangles that are in holes
*/
void TMRTriangularize::tagTriangles(TMRTriangle *tri) {
  // Set the combinations of edge pairs that will be added
  // to the hash table
  uint32_t edge_pairs[][2] = {
      {tri->u, tri->v}, {tri->v, tri->w}, {tri->w, tri->u}};
  for (int k = 0; k < 3; k++) {
    if (!edgeInPSLG(edge_pairs[k][0], edge_pairs[k][1])) {
      TMRTriangle *t;
      completeMe(edge_pairs[k][1], edge_pairs[k][0], &t);
      if (t && t->tag == 0) {
        t->tag = 1;
        tagTriangles(t);
      }
    }
  }
}

/*
  Write out the triangularization to a file
*/
void TMRTriangularize::writeToVTK(const char *filename, const int param_space) {
  FILE *fp = fopen(filename, "w");
  if (fp) {
    fprintf(fp, "# vtk DataFile Version 3.0\n");
    fprintf(fp, "vtk output\nASCII\n");
    fprintf(fp, "DATASET UNSTRUCTURED_GRID\n");

    // Write out the points
    fprintf(fp, "POINTS %d float\n", num_points);
    if (param_space) {
      for (int k = 0; k < num_points; k++) {
        fprintf(fp, "%e %e 0\n", pts[2 * k], pts[2 * k + 1]);
      }
    } else {
      for (int k = 0; k < num_points; k++) {
        fprintf(fp, "%e %e %e\n", X[k].x, X[k].y, X[k].z);
      }
    }

    // Write out the cell values
    fprintf(fp, "\nCELLS %d %d\n", num_triangles, 4 * num_triangles);

    TriListNode *node = list_start;
    while (node) {
      if (node->tri.status != DELETE_ME) {
        fprintf(fp, "3 %d %d %d\n", node->tri.u, node->tri.v, node->tri.w);
      }
      node = node->next;
    }

    // All quadrilaterals
    fprintf(fp, "\nCELL_TYPES %d\n", num_triangles);
    for (int k = 0; k < num_triangles; k++) {
      fprintf(fp, "%d\n", 5);
    }

    // Print out the rest as fields one-by-one
    fprintf(fp, "CELL_DATA %d\n", num_triangles);
    fprintf(fp, "SCALARS status float 1\n");
    fprintf(fp, "LOOKUP_TABLE default\n");
    node = list_start;
    while (node) {
      if (node->tri.status != DELETE_ME) {
        fprintf(fp, "%d\n", node->tri.status);
      }
      node = node->next;
    }

    fprintf(fp, "SCALARS quality float 1\n");
    fprintf(fp, "LOOKUP_TABLE default\n");
    node = list_start;
    while (node) {
      if (node->tri.status != DELETE_ME) {
        double quality = node->tri.quality;
        if (quality != quality) {
          quality = -1e20;
        }
        fprintf(fp, "%e\n", quality);
      }
      node = node->next;
    }

    fclose(fp);
  }
}

/*
  Retrieve the edge hash
*/
inline uint32_t TMRTriangularize::getEdgeHash(uint32_t x, uint32_t y) {
  return TMRIntegerPairHash(x, y);
}

/*
  Remove/delete the deleted triangles from the list
*/
void TMRTriangularize::deleteTrianglesFromList() {
  TriListNode *ptr = list_start;
  while (ptr) {
    TriListNode *next = ptr->next;

    if (ptr->tri.status == DELETE_ME) {
      if (ptr == list_start) {
        list_start = next;
        if (list_start) {
          list_start->prev = NULL;
        }
      } else {
        // Set the pointers from the previous and next objects in the
        // list so that they point past the ptr member
        if (ptr->prev) {
          ptr->prev->next = ptr->next;
        }
        if (ptr->next) {
          ptr->next->prev = ptr->prev;
        }
      }

      delete ptr;
    }

    ptr = next;
  }

  // Point the list back to the end of the list
  list_end = list_start;
  while (list_end && list_end->next) {
    list_end = list_end->next;
  }
}

/*
  Add a triangle to the mesh
*/
int TMRTriangularize::addTriangle(TMRTriangle tri) {
  int success = 1;

  // Add the triangle to the list of nodes
  if (!list_start) {
    // Create the root triangle node
    list_start = new TriListNode();
    list_start->tri = tri;
    list_start->next = NULL;
    list_start->prev = NULL;

    // Update the current node
    list_end = list_start;
  } else {
    // Create the new member in the triangle list
    TriListNode *next = new TriListNode();
    next->tri = tri;
    next->next = NULL;
    next->prev = list_end;

    // Update the current member in the triangle list
    list_end->next = next;
    list_end = list_end->next;
  }

  list_end->tri.tag = 0;
  list_end->tri.status = NO_STATUS;

  // Set the pointer to the list of triangles
  pts_to_tris[tri.u] = &(list_end->tri);
  pts_to_tris[tri.v] = &(list_end->tri);
  pts_to_tris[tri.w] = &(list_end->tri);

  // Add the triangle to the triangle count
  num_triangles++;

  // Redistribute the members in the hash table if required
  if (num_hash_nodes > 10 * num_buckets) {
    // Create a new array of buckets, twice the size of the old array
    // of buckets and re-insert the entries back into the new array
    int num_old_buckets = num_buckets;
    num_buckets *= 2;

    // Create a new array pointing to the new buckets and the
    // end of the bucket array
    EdgeHashNode **new_buckets = new EdgeHashNode *[num_buckets];
    EdgeHashNode **end_buckets = new EdgeHashNode *[num_buckets];
    memset(new_buckets, 0, num_buckets * sizeof(EdgeHashNode *));
    memset(end_buckets, 0, num_buckets * sizeof(EdgeHashNode *));

    // Assign the new buckets
    for (int i = 0; i < num_old_buckets; i++) {
      EdgeHashNode *node = buckets[i];

      while (node) {
        // Get the new hash values
        EdgeHashNode *tmp = node->next;
        uint32_t value = getEdgeHash(node->u, node->v);
        uint32_t bucket = value % num_buckets;

        // If the new bucket linked list does not exist, create a new
        // one and set the end_buckets pointer
        if (!new_buckets[bucket]) {
          new_buckets[bucket] = node;
          node->next = NULL;
          end_buckets[bucket] = new_buckets[bucket];
        } else {
          end_buckets[bucket]->next = node;
          end_buckets[bucket] = end_buckets[bucket]->next;
          node->next = NULL;
        }

        node = tmp;
      }
    }

    delete[] end_buckets;
    delete[] buckets;
    buckets = new_buckets;
  }

  // Set the combinations of edge pairs that will be added
  // to the hash table
  uint32_t edge_pairs[][2] = {{tri.u, tri.v}, {tri.v, tri.w}, {tri.w, tri.u}};

  // Add the triangle to the hash table
  for (int k = 0; k < 3; k++) {
    // Add a hash for each pair of edges around the triangle
    uint32_t u = edge_pairs[k][0];
    uint32_t v = edge_pairs[k][1];
    uint32_t value = getEdgeHash(u, v);
    uint32_t bucket = value % num_buckets;
    if (!buckets[bucket]) {
      // Create the new buckets node and assign the values
      buckets[bucket] = new EdgeHashNode();
      buckets[bucket]->u = u;
      buckets[bucket]->v = v;
      buckets[bucket]->tri_node = list_end;
      buckets[bucket]->next = NULL;
      num_hash_nodes++;
    } else {
      // Scan through to the end of the array and determine if the
      // edge exists in the hash table already or not
      EdgeHashNode *node = buckets[bucket];
      while (node) {
        // The edge already exists -- overwrite it
        if (node->u == u && node->v == v) {
          // The edge already exists, it will be overwritten, but
          // we'll call this a failure...
          success = 0;

          // Overwite the triangle
          node->tri_node = list_end;

          // Set the pointer to NULL so that the new node will not be
          // created
          node = NULL;
          break;
        }

        // If the next node does not exist, then break
        if (!node->next) {
          break;
        }

        // Increment the node to the next position
        node = node->next;
      }

      if (node) {
        // Create the new hash node
        node->next = new EdgeHashNode();
        node = node->next;

        // Set the data into the object
        node->u = u;
        node->v = v;
        node->tri_node = list_end;
        node->next = NULL;
        num_hash_nodes++;
      } else {
        success = 0;
      }
    }
  }

  return success;
}

/*
  Delete the triangle from the mesh.

  This deletes the triangle from the hash table but not the list.
*/
int TMRTriangularize::deleteTriangle(TMRTriangle tri) {
  // Keep track of whether we successfully delete all of the edges, or
  // just some of the edges (deleting only 1 or 2 out of 3 is bad!)
  int success = 1;

  // Set the combinations of edge pairs that will be added
  // to the hash table
  uint32_t edge_pairs[][2] = {{tri.u, tri.v}, {tri.v, tri.w}, {tri.w, tri.u}};

  // Keep track if this is the first edge we find
  int first = 1;

  // Remove the triangle from the hash table
  for (int k = 0; k < 3; k++) {
    // Add a hash for each pair of edges around the triangle
    uint32_t u = edge_pairs[k][0];
    uint32_t v = edge_pairs[k][1];

    // Keep track of whether we've deleted this edge
    int edge_success = 0;

    // Get the hash values and access the first entry of the bucket
    // it's listed under
    uint32_t value = getEdgeHash(u, v);
    uint32_t bucket = value % num_buckets;
    EdgeHashNode *node = buckets[bucket];
    EdgeHashNode *prev = node;
    while (node) {
      // The edge matches, we have to delete this triangle
      if (u == node->u && v == node->v) {
        // If this is the first edge we've found, delete the
        // corresponding list entry as well.
        if (first) {
          // This triangle will be deleted. Adjust the triangle
          // count to reflect this
          num_triangles--;

          // Mark the triangle in the edge list so that it is
          // 'deleted'. The memory is not freed at this point.
          node->tri_node->tri.status = DELETE_ME;

          // We've already encountered this triangle once.
          first = 0;
        }

        // Delete the edge from the hash table
        if (node == prev) {
          buckets[bucket] = node->next;
          delete node;
          num_hash_nodes--;
        } else {
          prev->next = node->next;
          delete node;
          num_hash_nodes--;
        }

        edge_success = 1;
        break;
      }

      prev = node;
      node = node->next;
    }

    // Keep track of each individual edge
    success = success && edge_success;
  }

  return success;
}

/*
  Retrieve the triangle hash
*/
inline uint32_t TMRTriangularize::getTriangleHash(TMRTriangle *tri) {
  return TMRIntegerTripletHash(tri->u, tri->v, tri->w);
}

/*
  Complete the adjacent triangle

  You complete me: Find the triangle that completes the specified
  edge. This can be used to find the triangle that is adjacent to
  another triangle.
*/
void TMRTriangularize::completeMe(uint32_t u, uint32_t v, TMRTriangle **tri) {
  *tri = NULL;

  // Retrieve the hash value/bucket for this edge
  uint32_t value = getEdgeHash(u, v);
  uint32_t bucket = value % num_buckets;
  EdgeHashNode *node = buckets[bucket];

  // Loop over the edges until we find the matching triangle
  while (node) {
    if (node->u == u && node->v == v) {
      *tri = &(node->tri_node->tri);
      break;
    }

    // Increment the pointer to the next member of this bucket
    node = node->next;
  }
}

/*
  Create the list of PSLG edges
*/
void TMRTriangularize::setUpPSLGEdges(int nsegs, const int segs[]) {
  num_pslg_edges = 2 * nsegs;
  pslg_edges = new uint32_t[2 * num_pslg_edges];

  for (int i = 0; i < nsegs; i++) {
    uint32_t u = 0, v = 0;
    if (segs[2 * i] >= 0) {
      u = segs[2 * i] + FIXED_POINT_OFFSET;
    }
    if (segs[2 * i + 1] >= 0) {
      v = segs[2 * i + 1] + FIXED_POINT_OFFSET;
    }

    pslg_edges[4 * i] = u;
    pslg_edges[4 * i + 1] = v;
    pslg_edges[4 * i + 2] = v;
    pslg_edges[4 * i + 3] = u;
  }

  qsort(pslg_edges, num_pslg_edges, 2 * sizeof(uint32_t), compare_edges);
}

/*
  Search the list of sorted edges
*/
int TMRTriangularize::edgeInPSLG(uint32_t u, uint32_t v) {
  uint32_t edge[2];
  edge[0] = u;
  edge[1] = v;

  int result = (bsearch(edge, pslg_edges, num_pslg_edges, 2 * sizeof(uint32_t),
                        compare_edges) != NULL);

  return result;
}

/*
  Does the given triangle enclose the point?
*/
inline int TMRTriangularize::enclosed(const double p[], uint32_t u, uint32_t v,
                                      uint32_t w) {
  double pt[2] = {p[0], p[1]};
  if (orient2d(&pts[2 * u], &pts[2 * v], pt) >= 0.0 &&
      orient2d(&pts[2 * v], &pts[2 * w], pt) >= 0.0 &&
      orient2d(&pts[2 * w], &pts[2 * u], pt) >= 0.0) {
    return 1;
  }

  return 0;
}

/*
  Does the final given point lie within the circumcircle of the
  remaining points?

  This function returns a positive value if the point lies within the
  circumdisk of the triangle formed by u, v, w. A negative value is
  returned if the point lies outside the circumdisk. If the point lies
  on the circumdisk, zero is returned.
*/
inline double TMRTriangularize::inCircle(uint32_t u, uint32_t v, uint32_t w,
                                         uint32_t x, TMRFace *metric) {
  double pu[2];
  pu[0] = pts[2 * u];
  pu[1] = pts[2 * u + 1];

  double pv[2];
  pv[0] = pts[2 * v];
  pv[1] = pts[2 * v + 1];

  double pw[2];
  pw[0] = pts[2 * w];
  pw[1] = pts[2 * w + 1];

  double px[2];
  px[0] = pts[2 * x];
  px[1] = pts[2 * x + 1];

  if (metric) {
    // Compute the metric components at the center of the triangle (u,v,w)
    TMRPoint X, Xu, Xv;
    metric->evalDeriv(px[0], px[1], &X, &Xu, &Xv);
    double g11 = Xu.dot(Xu);
    double g12 = Xu.dot(Xv);
    double g22 = Xv.dot(Xv);

    // Compute a multiplicative decomposition such that G = L*L^{T}
    // [l11    ][l11 l21] = [g11  g12]
    // [l21 l22][    l22]   [g12  g22]
    // l11 = sqrt(g11)
    // l11*l21 = g12 => l21 = g12/l11
    // l21*l21 + l22^2 = g22 => l22 = sqrt(g22 - l21*l21);
    double l11 = sqrt(g11);
    double inv11 = 1.0 / l11;
    double l21 = inv11 * g12;
    double l22 = sqrt(g22 - l21 * l21);

    // Compute p' = L^{T}*p to transform into the local coordinates
    pu[0] = l11 * pts[2 * u] + l21 * pts[2 * u + 1];
    pu[1] = l22 * pts[2 * u + 1];

    pv[0] = l11 * pts[2 * v] + l21 * pts[2 * v + 1];
    pv[1] = l22 * pts[2 * v + 1];

    pw[0] = l11 * pts[2 * w] + l21 * pts[2 * w + 1];
    pw[1] = l22 * pts[2 * w + 1];

    px[0] = l11 * pts[2 * x] + l21 * pts[2 * x + 1];
    px[1] = l22 * pts[2 * x + 1];
  }

  // No metric is defined, so simply take the circumcircle check from
  // Shewchuk's geometric predicates
  return incircle(pu, pv, pw, px);
}

/*
  Add a point to the point list
*/
uint32_t TMRTriangularize::addPoint(const double pt[]) {
  // If the size of the array is exceeded, multiply it by 2
  if (num_points >= max_num_points) {
    max_num_points *= 2;

    // Allocate a new array for the parametric locations of all the
    // points
    double *new_pts = new double[2 * max_num_points];
    memcpy(new_pts, pts, 2 * num_points * sizeof(double));
    delete[] pts;
    pts = new_pts;

    // Allocate a new array for the pointer from the triangle vertices
    // to an attaching triangle
    TMRTriangle **new_pts_to_tris = new TMRTriangle *[2 * max_num_points];
    memcpy(new_pts_to_tris, pts_to_tris, num_points * sizeof(TMRTriangle *));
    delete[] pts_to_tris;
    pts_to_tris = new_pts_to_tris;

    // Allocate the space for the triangle vertices in physical space
    TMRPoint *new_X = new TMRPoint[max_num_points];
    memcpy(new_X, X, num_points * sizeof(TMRPoint));
    delete[] X;
    X = new_X;
  }

  // Add the point to the quadtree
  root->addNode(num_points, pt);

  // Set the new point location
  pts[2 * num_points] = pt[0];
  pts[2 * num_points + 1] = pt[1];

  // No new triangle has been assigned yet
  pts_to_tris[num_points] = NULL;

  // Evaluate the face location
  face->evalPoint(pt[0], pt[1], &X[num_points]);

  // Increase the number of points by one
  num_points++;

  // Return the new point index
  return num_points - 1;
}

/*
  Add the vertex to the underlying Delaunay triangularization.
*/
void TMRTriangularize::addPointToMesh(const double pt[], TMRFace *metric) {
  // Find the enclosing triangle
  TMRTriangle *tri;
  findEnclosing(pt, &tri);

  // Add the point to the quadtree
  uint32_t u = addPoint(pt);

  if (tri) {
    uint32_t v = tri->u;
    uint32_t w = tri->v;
    uint32_t x = tri->w;
    deleteTriangle(*tri);
    digCavity(u, v, w, metric);
    digCavity(u, w, x, metric);
    digCavity(u, x, v, metric);
  }

  return;
}

/*
  Add the point to the mesh, given that we've already found the
  triangle that encloses the given point.
*/
void TMRTriangularize::addPointToMesh(const double pt[], TMRTriangle *tri,
                                      TMRFace *metric) {
  uint32_t u = addPoint(pt);

  if (tri) {
    uint32_t v = tri->u;
    uint32_t w = tri->v;
    uint32_t x = tri->w;
    deleteTriangle(*tri);
    digCavity(u, v, w, metric);
    digCavity(u, w, x, metric);
    digCavity(u, x, v, metric);
  }
}

/*
  The following code tests whether the triangle formed from the point
  (u, v, w) is constrained Delaunay.

  If the edge (w, v) is in the PSLG, then the triangle is added immediately.
  If not, and if the point lies within the circumcircle of the triangle
  (u, w, v) with the directed edge (v, w).
*/
void TMRTriangularize::digCavity(uint32_t u, uint32_t v, uint32_t w,
                                 TMRFace *metric) {
  // If the edge is along the polynomial straight line graph, then we
  // add the triangle as it exists and we're done, even though it may
  // not be Delaunay (it will be constrained Delaunay). We cannot
  // split a PSLG edge.
  if (edgeInPSLG(w, v)) {
    addTriangle(TMRTriangle(u, v, w));
    return;
  }

  // Complete the triangle
  TMRTriangle *tri;
  completeMe(w, v, &tri);

  if (tri) {
    // Get the index of the final vertex
    uint32_t x = 0;
    if (tri->u == w && tri->v == v) {
      x = tri->w;
    } else if (tri->v == w && tri->w == v) {
      x = tri->u;
    } else if (tri->w == w && tri->u == v) {
      x = tri->v;
    }

    // Check whether the point lies within the circumcircle
    if (inCircle(u, v, w, x, metric) > 0.0) {
      deleteTriangle(*tri);
      digCavity(u, v, x, metric);
      digCavity(u, x, w, metric);
      return;
    }
  }

  addTriangle(TMRTriangle(u, v, w));
}

/*
  Inser the segment on the PSLG into the triangulation
*/
void TMRTriangularize::insertSegment(uint32_t u, uint32_t v) {
  // Identify and delete all the triangles between u and v
  TMRTriangle *t = pts_to_tris[u];
  TMRTriangle *tri = NULL;

  // Find the triangle that the point intersects
  uint32_t w = 0;  // The node on the negative side of (u, v)
  uint32_t x = 0;  // The node on the positive side of (u, v)

  // Find the triangle (u, w, x) where u is the first
  // node in the segment, w is below the segment and x
  // is above the segment
  while (t) {
    if (u == t->u) {
      w = t->v;
      x = t->w;
    } else if (u == t->v) {
      w = t->w;
      x = t->u;
    }
    if (u == t->w) {
      w = t->u;
      x = t->v;
    }

    // Check whether x is above the oriented edge (u, v)
    // and w is below the oriented edge (u, v)
    if (orient2d(&pts[2 * u], &pts[2 * v], &pts[2 * x]) >= 0.0 &&
        orient2d(&pts[2 * u], &pts[2 * v], &pts[2 * w]) <= 0.0) {
      tri = t;
      break;
    }

    completeMe(u, x, &t);
  }

  if (!t) {
    t = pts_to_tris[u];

    while (t) {
      if (u == t->u) {
        w = t->v;
        x = t->w;
      } else if (u == t->v) {
        w = t->w;
        x = t->u;
      }
      if (u == t->w) {
        w = t->u;
        x = t->v;
      }

      // Check whether x is above the oriented edge (u, v)
      // and w is below the oriented edge (u, v)
      if (orient2d(&pts[2 * u], &pts[2 * v], &pts[2 * x]) >= 0.0 &&
          orient2d(&pts[2 * u], &pts[2 * v], &pts[2 * w]) <= 0.0) {
        tri = t;
        break;
      }

      completeMe(w, u, &t);
    }
  }

  // Set the initial entries in the arrays
  int pos_count = 2;
  uint32_t pos[1028];
  pos[0] = u;
  pos[1] = x;

  int neg_count = 2;
  uint32_t neg[1028];
  neg[0] = u;
  neg[1] = w;

  // Delete the triangle
  deleteTriangle(*tri);

  // Now search through to find the next triangle
  while (1) {
    // Find the next triangle
    completeMe(x, w, &tri);

    if (!tri) {
      fprintf(stderr,
              "TMRTriangularize Error: No triangle found; "
              "Error in edge orientations\n");
      break;
    }

    // Find the last node y that completes the triangle (x, w, y)
    uint32_t y = 0;
    if (x == tri->u && w == tri->v) {
      y = tri->w;
    } else if (x == tri->w && w == tri->u) {
      y = tri->v;
    } else {
      y = tri->u;
    }

    // Free this triangle
    deleteTriangle(*tri);

    if (y == v) {
      pos[pos_count] = v;
      pos_count++;
      neg[neg_count] = v;
      neg_count++;
      break;
    } else {
      // Use the orientation check to determine which side of the
      // segment things should lie on...
      if (orient2d(&pts[2 * u], &pts[2 * v], &pts[2 * y]) >= 0.0) {
        pos[pos_count] = y;
        pos_count++;
        x = y;
      } else {
        neg[neg_count] = y;
        neg_count++;
        w = y;
      }
    }
  }

  // Convert the arrays
  giftWrap(pos, pos_count, 1);
  giftWrap(neg, neg_count, -1);
}

/*
  Gift-wrap algorithm for segments (does not consider visibility)
*/
void TMRTriangularize::giftWrap(const uint32_t v[], int size, int orient) {
  if (size == 2) {
    // There is only one segment left, we are done
    return;
  }

  // Find the node in the list that does not
  int index = 1;
  uint32_t t = v[1];
  for (int i = 2; i < size - 1; i++) {
    if (orient > 0) {
      if (inCircle(v[0], v[size - 1], t, v[i]) >= 0.0) {
        t = v[i];
        index = i;
      }
    } else {
      if (inCircle(v[size - 1], v[0], t, v[i]) >= 0.0) {
        t = v[i];
        index = i;
      }
    }
  }

  // Add the triangle that we just found
  if (orient > 0) {
    addTriangle(TMRTriangle(v[0], v[size - 1], t));
  } else {
    addTriangle(TMRTriangle(v[size - 1], v[0], t));
  }

  // Perform the gift wrapping on the remaining triangles
  giftWrap(&v[0], index + 1, orient);
  giftWrap(&v[index], size - index, orient);
}

/*
  Find the enclosing triangle within the mesh.

  This code uses a quadtree for geometric searching. First, we find
  the node that is closest to the query point. This node is not
  necessarily connected with the enclosing triangle that we
  want. Next, we find one triangle associated with this node. If this
  triangle does not contain the point, we march over the mesh, marking
  the triangles that we have visited.
*/
void TMRTriangularize::findEnclosing(const double pt[], TMRTriangle **ptr) {
  *ptr = NULL;
  if (search_tag == UINT_MAX) {
    search_tag = 0;
    setTriangleTags(0);
  }
  search_tag++;

  // Find the closest point to the given
  uint32_t u = root->findClosest(pt);

  // Obtain the triangle associated with the node u. This triangle may
  // not contain the node, but will hopefully be close to the node.
  // We'll walk the mesh to nearby elements until we find the proper
  // enclosing triangle.
  TMRTriangle *tri = pts_to_tris[u];

  if (enclosed(pt, tri->u, tri->v, tri->w)) {
    *ptr = tri;
    return;
  } else {
    tri->tag = search_tag;
  }

  // Members of the list do not enclose the point and have been
  // labeled that they are searched
  TriQueue queue;
  queue.append(tri);

  while (queue.size > 0) {
    // Pop the top member from the queue
    TMRTriangle *t = queue.pop();

    // Search the adjacent triangles across edges
    uint32_t edge_pairs[][2] = {{t->u, t->v}, {t->v, t->w}, {t->w, t->u}};

    // Search the adjacent triangles and determine whether they have
    // been tagged
    for (int k = 0; k < 3; k++) {
      TMRTriangle *t2;
      completeMe(edge_pairs[k][1], edge_pairs[k][0], &t2);
      if (t2 && t2->tag != search_tag) {
        // Check whether the point is enclosed by t2
        if (enclosed(pt, t2->u, t2->v, t2->w)) {
          *ptr = t2;
          return;
        }

        // If the triangle does not enclose the point, label this guy
        // as having been searched
        t2->tag = search_tag;

        // Append this guy to the queue
        queue.append(t2);
      }
    }
  }
}

/*
  Compute the circumcircle for the given triangle.

  This can be used to evaluate an effective 'h' value (based on the
  equilateral radius length r_eq = h/sqrt(3)) which  is used as a
  metric to determine whether to retain the triangle, or search for
  a better one.
*/
double TMRTriangularize::computeSizeRatio(uint32_t u, uint32_t v, uint32_t w,
                                          TMRElementFeatureSize *fs,
                                          double *_R) {
  // The circumcircle of an equilateral triangle is sqrt(3)*h where h
  // is the side-length of the triangle
  const double sqrt3 = 1.7320508075688772;

  // Compute the edge lengths in physical space
  TMRPoint d1;
  d1.x = X[v].x - X[u].x;
  d1.y = X[v].y - X[u].y;
  d1.z = X[v].z - X[u].z;

  TMRPoint d2;
  d2.x = X[w].x - X[u].x;
  d2.y = X[w].y - X[u].y;
  d2.z = X[w].z - X[u].z;

  // Compute the dot product
  double d1d = d1.dot(d1);
  double dot = d1.dot(d2) / d1d;

  // Compute the perpendicular component along the second direction
  // that we'll use to determine the center point
  TMRPoint n1;
  n1.x = d2.x - dot * d1.x;
  n1.y = d2.y - dot * d1.y;
  n1.z = d2.z - dot * d1.z;

  // Compute alpha = 0.5*(d2, d2 - d1)/(d2, n1)
  double alpha = 0.5 * (d2.x * (d2.x - d1.x) + d2.y * (d2.y - d1.y) +
                        d2.z * (d2.z - d1.z));
  alpha = alpha / d2.dot(n1);

  // Compute the distance from the point
  d1.x = 0.5 * d1.x + alpha * n1.x;
  d1.y = 0.5 * d1.y + alpha * n1.y;
  d1.z = 0.5 * d1.z + alpha * n1.z;

  // Compute the radius
  double R = sqrt(d1.dot(d1));
  if (_R) {
    *_R = R;
  }

  // Compute the center of the triangle
  d1.x = (X[u].x + X[v].x + X[w].x) / 3.0;
  d1.y = (X[u].y + X[v].y + X[w].y) / 3.0;
  d1.z = (X[u].z + X[v].z + X[w].z) / 3.0;
  double h = fs->getFeatureSize(d1);

  return (sqrt3 * R / h);
}

/*
  Class for comparing triangle quality that is used within the frontal
  code.
*/
// class TMRTriangleCompare {
//  public:
//   bool operator()(TMRTriangle* const& A,
//                   TMRTriangle* const& B){
//     return A->R < B->R;
//   }
// };

/*
  Perform a frontal mesh generation algorithm to create a constrained
  Delaunay triangularization of the generated mesh.

  The Delaunay triangularization based on the Bowyer-Watson mesh
  generation algorithm. The frontal mesh generation technique is based
  on Rebay's 1993 paper in JCP.
*/
void TMRTriangularize::frontal(TMRMeshOptions options,
                               TMRElementFeatureSize *fs) {
  // The queue of active (and sometimes deleted) triangles
  // std::priority_queue<TMRTriangle*, std::vector<TMRTriangle*>,
  //   TMRTriangleCompare> active;

  std::queue<TMRTriangle *> active;

  // Get the quality factor
  double frontal_quality_factor = options.frontal_quality_factor;
  if (options.frontal_quality_factor > 2.0) {
    frontal_quality_factor = 2.0;
  } else if (options.frontal_quality_factor < 1.01) {
    frontal_quality_factor = 1.01;
  }

  // Add the triangles to the active set that
  TriListNode *node = list_start;
  while (node) {
    if (node->tri.status != DELETE_ME) {
      // Set the status by default as waiting
      node->tri.status = WAITING;

      // Compute the 'quality' indicator for this triangle
      TMRTriangle t = node->tri;
      double R = 0.0;
      node->tri.quality = computeSizeRatio(t.u, t.v, t.w, fs, &R);
      node->tri.R = R;
      if (node->tri.quality < frontal_quality_factor) {
        node->tri.status = ACCEPTED;
      } else {
        // If any of the triangles touches an edge in the planar
        // straight line graph, change it to a waiting triangle
        uint32_t edge_pairs[][2] = {{node->tri.u, node->tri.v},
                                    {node->tri.v, node->tri.w},
                                    {node->tri.w, node->tri.u}};
        for (int k = 0; k < 3; k++) {
          if (edgeInPSLG(edge_pairs[k][0], edge_pairs[k][1])) {
            node->tri.status = ACTIVE;
            active.push(&node->tri);
            break;
          }
        }
      }
    }

    // Go to the next triangle in the list
    node = node->next;
  }

  // Iterate over the list again and add any triangles that are
  // adjacent to an ACCEPTED triangle to the ACTIVE set of triangles
  node = list_start;
  while (node) {
    if (node->tri.status == ACCEPTED) {
      // Check if any of the adjacent triangles are WAITING.  If so,
      // change their status to ACTIVE
      uint32_t edge_pairs[][2] = {{node->tri.u, node->tri.v},
                                  {node->tri.v, node->tri.w},
                                  {node->tri.w, node->tri.u}};

      for (int k = 0; k < 3; k++) {
        TMRTriangle *adjacent;
        completeMe(edge_pairs[k][1], edge_pairs[k][0], &adjacent);
        if (adjacent && adjacent->status == WAITING) {
          node->tri.status = ACTIVE;
          active.push(&node->tri);
          break;
        }
      }
    }

    // Increment the pointer to the next member of the list
    node = node->next;
  }

  if (options.triangularize_print_level > 0) {
    printf("%10s %10s %10s\n", "Iteration", "Triangles", "Active");
  }

  int num_newton_fail = 0;
  double t0 = MPI_Wtime();
  double t0_enclose = 0.0, t1_enclose = 0.0;
  double t0_update = 0.0, t1_update = 0.0;

  int iter = 0;
  while (1) {
    if (options.triangularize_print_level > 0 &&
        iter % options.triangularize_print_iter == 0) {
      int queue_size = active.size();
      printf("%10d %10d %10d\n", iter, num_triangles, queue_size);
      if (options.write_triangularize_intermediate) {
        char filename[256];
        if (face) {
          snprintf(filename, sizeof(filename),
                   "intermediate_triangle%d_iter%d.vtk", face->getEntityId(),
                   iter);
        } else {
          snprintf(filename, sizeof(filename),
                   "intermediate_triangle_iter%d.vtk", iter);
        }
        writeToVTK(filename);
      }
    }
    iter++;

    // The pointer to the triangle that we're going to use next
    TMRTriangle *tri = NULL;

    // Find the first active triangle that is not marked to be deleted
    while (active.size() > 0 && !tri) {
      // This wonderful syntax brought to you by C++! Constant
      // reference to a pointer, which we can just copy to a
      // non-constant pointer.
      TMRTriangle *const &tri_ptr = active.front();
      tri = tri_ptr;

      // Pop the top member of the priority queue, but only use it if
      // the triangle is still active (note: the queue can contain
      // non-active triangles)
      active.pop();
      if (tri->status != ACTIVE) {
        tri = NULL;
      }
    }

    // We've failed to find any active triangle. We're done
    if (!tri) {
      break;
    }

    int found = 0;
    uint32_t u = 0, v = 0;
    uint32_t edge_pairs[][2] = {
        {tri->u, tri->v}, {tri->v, tri->w}, {tri->w, tri->u}};

    // Check if the edge is on the PSLG
    for (int k = 0; k < 3; k++) {
      u = edge_pairs[k][0];
      v = edge_pairs[k][1];
      if (edgeInPSLG(u, v)) {
        found = 1;
        break;
      }
    }

    // Check if an adjacent triangle is accepted
    if (!found) {
      for (int k = 0; k < 3; k++) {
        u = edge_pairs[k][0];
        v = edge_pairs[k][1];

        // Compute the completed triangle
        TMRTriangle *t;
        completeMe(v, u, &t);
        if (t && t->status == ACCEPTED) {
          found = 1;
          break;
        }
      }
    }

    // Compute the location of the new point
    // | i   j   k |
    // | 0   0   1 | = - i*dy + j*dx
    // | dx  dy  0 |

    // Compute the parametric mid-point
    double m[2];
    m[0] = 0.5 * (pts[2 * u] + pts[2 * v]);
    m[1] = 0.5 * (pts[2 * u + 1] + pts[2 * v + 1]);

    // Get the derivatives of the surface at the mid-point
    TMRPoint Xpt, Xu, Xv;
    face->evalDeriv(m[0], m[1], &Xpt, &Xu, &Xv);

    // Compute the metric tensor components
    double g11 = Xu.dot(Xu);
    double g12 = Xu.dot(Xv);
    double g22 = Xv.dot(Xv);

    // Compute the inverse metric components
    double invdet = 1.0 / (g11 * g22 - g12 * g12);
    double G11 = invdet * g22;
    double G12 = -invdet * g12;
    double G22 = invdet * g11;

    // Compute the parametric direction along the curvilinear line
    // connecting from u to v
    double d[2];
    d[0] = (pts[2 * v] - pts[2 * u]);
    d[1] = (pts[2 * v + 1] - pts[2 * u + 1]);

    // Compute the orthogonal coordinate contributions to the vector
    double e[2];
    e[0] = G12 * d[0] - G11 * d[1];
    e[1] = G22 * d[0] - G12 * d[1];

    // Compute the direction in physical space
    TMRPoint dir;
    dir.x = e[0] * Xu.x + e[1] * Xv.x;
    dir.y = e[0] * Xu.y + e[1] * Xv.y;
    dir.z = e[0] * Xu.z + e[1] * Xv.z;

    // Compute the physical location along the parameter direction
    // based on the local feature size
    const double sqrt3 = 1.73205080757;
    double h = fs->getFeatureSize(Xpt);

    // The new point that will be added into the mesh (potentially)
    TMRTriangle *pt_tri = NULL;
    double pt[2] = {0.0, 0.0};
    double htrial = h;

    for (int trial = 0; trial < 2; trial++) {
      // Compute the side-edge length, given the prescribed mesh
      // spacing at this point in the domain.
      double de = 0.5 * sqrt3 * htrial;

      // Compute the ratio between the desired distance in physical
      // space and the length of the direction dir in physical space
      // for a unit change in parameter space.
      double f = de / sqrt(dir.dot(dir));
      pt[0] = m[0] + f * e[0];
      pt[1] = m[1] + f * e[1];

      int newton_fail = 1;
      const double rtol = 1e-5;
      const int max_newton_iters = 10;

      for (int k = 0; k < max_newton_iters; k++) {
        // Solve for the problem
        face->evalDeriv(pt[0], pt[1], &Xpt, &Xu, &Xv);

        // Solve for the surface location that satisfies
        // ||X - X[u]||_{2} = de and
        // ||X - X[v]||_{2} = de
        TMRPoint du;
        du.x = Xpt.x - X[u].x;
        du.y = Xpt.y - X[u].y;
        du.z = Xpt.z - X[u].z;

        TMRPoint dv;
        dv.x = Xpt.x - X[v].x;
        dv.y = Xpt.y - X[v].y;
        dv.z = Xpt.z - X[v].z;

        double r[2];
        r[0] = de * de - du.dot(du);
        r[1] = de * de - dv.dot(dv);

        if (fabs(r[0]) < rtol * de * de && fabs(r[1]) < rtol * de * de) {
          newton_fail = 0;
          break;
        }

        // Compute the Jacobian of the system of equations
        double A[4];
        A[0] = 2.0 * Xu.dot(du);
        A[2] = 2.0 * Xv.dot(du);

        A[1] = 2.0 * Xu.dot(dv);
        A[3] = 2.0 * Xv.dot(dv);

        // Solve the system of equations
        int n = 2, one = 1, ipiv[2], info = 0;
        TmrLAPACKdgetrf(&n, &n, A, &n, ipiv, &info);
        TmrLAPACKdgetrs("N", &n, &one, A, &n, ipiv, r, &n, &info);

        // Guard against moving the u/v coordinates outside the
        // domain of the surface
        double umin, umax, vmin, vmax;
        face->getRange(&umin, &vmin, &umax, &vmax);
        if (pt[0] + r[0] > umax) {
          pt[0] = umax;
        } else if (pt[0] + r[0] < umin) {
          pt[0] = umin;
        } else {
          pt[0] += r[0];
        }
        if (pt[1] + r[1] > vmax) {
          pt[1] = vmax;
        } else if (pt[1] + r[1] < vmin) {
          pt[1] = vmin;
        } else {
          pt[1] += r[1];
        }
      }

      // Reset the point
      if (newton_fail) {
        num_newton_fail++;
        pt[0] = m[0] + f * e[0];
        pt[1] = m[1] + f * e[1];
      }

      // Find the enclosing triangle for the new point
      pt_tri = tri;
      if (!enclosed(pt, pt_tri->u, pt_tri->v, pt_tri->w)) {
        t0_enclose += MPI_Wtime();
        findEnclosing(pt, &pt_tri);
        t1_enclose += MPI_Wtime();
      }

      // If no triangle is found, then we quit an mark the source
      // triangle as accepted. If a triangle is found and it is not
      // accepted, then we add the point. Otherwise, we try a new
      // point in the domain.
      if (!pt_tri || (pt_tri && pt_tri->status != ACCEPTED)) {
        break;
      } else {  // (pt_tri && pt_tri->status == ACCEPTED){
        // We've inserted a point at a location where the triangle has
        // already been accepted. This is not permitted, so we
        // continue...
        pt_tri = NULL;
        htrial *= 0.5;
      }
    }

    // Find the closest node to the point
    uint32_t w = root->findClosest(pt);

    // Evalute the point
    TMRPoint dpt;
    face->evalPoint(pt[0], pt[1], &dpt);
    dpt.x = dpt.x - X[w].x;
    dpt.y = dpt.y - X[w].y;
    dpt.z = dpt.z - X[w].z;

    // Reject points that are too close to other points
    double beta = 0.25;
    if (dpt.dot(dpt) < beta * h * h) {
      pt_tri = NULL;
    }

    if (!pt_tri) {
      // We've tried a new point and it was outside the domain.
      // That is not allowed, so, we quit and mark the triangle as
      // accepted.
      if (tri->status == WAITING || tri->status == ACTIVE) {
        tri->status = ACCEPTED;

        // Search from adjacent triangles
        for (int k = 0; k < 3; k++) {
          TMRTriangle *adjacent;
          completeMe(edge_pairs[k][1], edge_pairs[k][0], &adjacent);
          if (adjacent && adjacent->status == WAITING) {
            adjacent->status = ACTIVE;
            active.push(adjacent);
          }
        }
      }
    } else {  // (pt_tri){
      // Add up the update time
      t0_update += MPI_Wtime();

      // Set the pointer to the last member in the list
      TriListNode *list_marker = list_end;
      addPointToMesh(pt, pt_tri, face);
      pt_tri = NULL;

      // Compute the size ratio of the new triangles and check whether
      // they belong in the accepted category or not...
      TriListNode *ptr = list_start;
      if (list_marker) {
        ptr = list_marker->next;
      }
      while (ptr) {
        TMRTriangle t = ptr->tri;
        double R = 0.0;
        ptr->tri.quality = computeSizeRatio(t.u, t.v, t.w, fs, &R);
        ptr->tri.R = R;
        if (ptr->tri.quality < frontal_quality_factor) {
          ptr->tri.status = ACCEPTED;
        } else {
          ptr->tri.status = WAITING;
        }
        ptr = ptr->next;
      }

      // Complete me with the newly created triangle. This triangle
      // must be accepted (if it exists) to avoid recreating the exact
      // same point that we just added.
      completeMe(u, v, &pt_tri);
      if (pt_tri) {
        pt_tri->status = ACCEPTED;
      }

      // Scan through the list of the added triangles and mark which
      // ones are active/working/accepted.
      ptr = list_start;
      if (list_marker) {
        ptr = list_marker->next;
      }

      while (ptr) {
        if (ptr->tri.status != ACCEPTED) {
          // If any of the triangles touches an edge in the planar
          // straight line graph, change it to a waiting triangle
          int flag = 0;
          uint32_t edge_pairs[][2] = {{ptr->tri.u, ptr->tri.v},
                                      {ptr->tri.v, ptr->tri.w},
                                      {ptr->tri.w, ptr->tri.u}};

          // Loop over all of the edges in the triangle and check
          // whether they're in the PSLG
          for (int k = 0; k < 3; k++) {
            if (edgeInPSLG(edge_pairs[k][0], edge_pairs[k][1])) {
              ptr->tri.status = ACTIVE;
              active.push(&ptr->tri);
              flag = 1;
              break;
            }
          }

          // If any triangle is adjacent to a triangle that is accepted,
          // then change the status of the new triangle to be active
          if (!flag) {
            for (int k = 0; k < 3; k++) {
              TMRTriangle *adjacent;
              completeMe(edge_pairs[k][1], edge_pairs[k][0], &adjacent);
              if (adjacent && adjacent->status == ACCEPTED) {
                ptr->tri.status = ACTIVE;
                active.push(&ptr->tri);
                break;
              }
            }
          }
        }

        // Increment the pointer to the next member of the list
        ptr = ptr->next;
      }
      t1_update += MPI_Wtime();
    }
  }

  t0 = MPI_Wtime() - t0;

  if (face && options.mesh_type_default != TMR_TRIANGLE) {
    // Ensure that we do not have isolated triangles on the boundary
    // which will cause problems if we do a conversion to a
    // quadrilateral mesh. This will not do "good" things to the
    // triangularization.
    node = list_start;
    while (node) {
      if (node->tri.status == ACCEPTED) {
        const uint32_t u = node->tri.u;
        const uint32_t v = node->tri.v;
        const uint32_t w = node->tri.w;

        if ((u - FIXED_POINT_OFFSET < init_boundary_points) &&
            (v - FIXED_POINT_OFFSET < init_boundary_points) &&
            (w - FIXED_POINT_OFFSET < init_boundary_points)) {
          // Check if only one adjacent triangle exists
          TMRTriangle *t1, *t2, *t3;
          completeMe(v, u, &t1);
          completeMe(w, v, &t2);
          completeMe(u, w, &t3);

          if (!t1 && !t2) {
            TMRPoint p;
            p.x = 0.5 * (X[u].x + X[w].x);
            p.y = 0.5 * (X[u].y + X[w].y);
            p.z = 0.5 * (X[u].z + X[w].z);

            double pt[2];
            face->invEvalPoint(p, &pt[0], &pt[1]);
            addPointToMesh(pt, face);
          } else if (!t2 && !t3) {
            TMRPoint p;
            p.x = 0.5 * (X[u].x + X[v].x);
            p.y = 0.5 * (X[u].y + X[v].y);
            p.z = 0.5 * (X[u].z + X[v].z);

            double pt[2];
            face->invEvalPoint(p, &pt[0], &pt[1]);
            addPointToMesh(pt, face);
          } else if (!t1 && !t3) {
            TMRPoint p;
            p.x = 0.5 * (X[v].x + X[w].x);
            p.y = 0.5 * (X[v].y + X[w].y);
            p.z = 0.5 * (X[v].z + X[w].z);

            double pt[2];
            face->invEvalPoint(p, &pt[0], &pt[1]);
            addPointToMesh(pt, face);
          }
        }
      }
      node = node->next;
    }
  }

  // Free the deleted trianlges from the doubly linked list
  deleteTrianglesFromList();

  if (options.triangularize_print_level > 0) {
    printf("%10d %10d\n", iter, num_triangles);
  }
  if (options.triangularize_print_level > 1) {
    printf("Time breakdown\n");
    printf("findEnclosing:    %15.4e s\n", t1_enclose - t0_enclose);
    printf("update:           %15.4e s\n", t1_update - t0_update);
    printf("total:            %15.4e s\n", t0);
    printf("num_newton_fail:  %15d\n", num_newton_fail);
  }
}
