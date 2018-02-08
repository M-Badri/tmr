#ifndef TMR_OCTANT_FOREST_H
#define TMR_OCTANT_FOREST_H

/*
  Copyright (c) 2016 Graeme Kennedy. All rights reserved. 
*/

#include "TMRTopology.h"
#include "TMROctant.h"
#include "BVecInterp.h"

/*
  TMR Forest class

  This class defines a forest of octrees. The octrees within the
  forest can be distributed across processors. The connectivity
  between octrees is defined on all processors by setting a octree to
  node connectivity.
  
  The octrees can be redistributed across processors by using the
  repartition function. This destroys the nodes that may have been
  created (but can easily be recomputed).
  
  The duplicate() and coarsen() functions create a forest that is
  aligned with the parallel distribution of octrees. This facilitates
  the construction of the interpolation operators that can be used for
  multigrid solution algorithms.
*/
class TMROctForest : public TMREntity {
 public:
  TMROctForest( MPI_Comm _comm, int mesh_order=2,
                TMRInterpolationType interp_type=TMR_GAUSS_LOBATTO_POINTS );
  ~TMROctForest();

  // Get the MPI communicator
  // ------------------------
  MPI_Comm getMPIComm(){ return comm; }

  // Set the topology (and determine the connectivity)
  // -------------------------------------------------
  void setTopology( TMRTopology *_topo );
  TMRTopology *getTopology();

  // Set the connectivity
  // --------------------
  void setConnectivity( int _num_nodes,
                        const int *_block_conn,
                        int _num_blocks );
  void setFullConnectivity( int _num_nodes, int _num_edges,
                            int _num_faces, int _num_blocks,
                            const int *_block_conn,
                            const int *_block_edge_conn,
                            const int *_block_face_conn );

  // Re-partition the octrees based on element count
  // -----------------------------------------------
  void repartition();
  
  // Create the forest of octrees
  // ----------------------------
  void createTrees( int refine_level );
  void createRandomTrees( int nrand=10, 
                          int min_level=0, int max_level=8 );

  // Duplicate or coarsen the forest
  // -------------------------------
  TMROctForest *duplicate();
  TMROctForest *coarsen();

  // Refine the mesh
  // ---------------
  void refine( const int refinement[]=NULL,
               int min_level=0, int max_level=TMR_MAX_LEVEL );

  // Balance the octree meshes
  // -------------------------
  void balance( int balance_corner=0 );

  // Create and order the nodes
  // --------------------------
  void createNodes();

  // Retrieve the dependent mesh nodes
  // ---------------------------------
  void getNodeConn( const int **_conn=NULL, 
                    int *_num_elements=NULL,
                    int *_num_owned_nodes=NULL,
                    int *_num_local_nodes=NULL );
  int getDepNodeConn( const int **_ptr, const int **_conn,
                      const double **_weights );
 
  // Create interpolation/restriction operators
  // ------------------------------------------
  void createInterpolation( TMROctForest *coarse,
                            TACSBVecInterp *interp );

  // Get the nodes or elements with certain attributes
  // -------------------------------------------------
  TMROctantArray* getOctsWithAttribute( const char *attr );
  int getNodesWithAttribute( const char *attr, int **_nodes );
  
  // Get the mesh order
  // ------------------
  int getMeshOrder();

  // Get the node-processor ownership range
  // --------------------------------------
  int getOwnedNodeRange( const int **_node_range );

  // Get the octants and the nodes
  // -----------------------------
  void getOctants( TMROctantArray **_octants );
  int getNodeNumbers( const int **_node_numbers );
  int getPoints( TMRPoint **_X );
  int getLocalNodeNumber( int node );

  // Retrieve the connectivity information
  // -------------------------------------
  void getConnectivity( int *_nblocks, int *_nfaces, 
                        int *_nedges, int *_nnodes, 
                        const int **_block_conn, 
                        const int **_block_face_conn, 
                        const int **_block_edge_conn,
                        const int **_block_face_ids );
  void getInverseConnectivity( const int **_node_block_conn,
                               const int **_node_block_ptr,
                               const int **_edge_block_conn,
                               const int **_edge_block_ptr,
                               const int **_face_block_conn,
                               const int **_face_block_ptr );

  // Find the octant enclosing the given node
  // ----------------------------------------
  TMROctant* findEnclosing( TMROctant *node );

  // Transform the octant to the global order
  // ----------------------------------------
  void transformNode( TMROctant *oct, int *edge_reversed=NULL,
                      int *src_face_id=NULL );

  // Distribute the octant array in parallel to other processors
  // -----------------------------------------------------------
  TMROctantArray *distributeOctants( TMROctantArray *list,
                                     int use_tags=0,
                                     int **oct_ptr=NULL, 
                                     int **oct_recv_ptr=NULL,
                                     int include_local=0,
                                     int use_node_index=0 );

  // Send the octants back to their original processors (dual of distribute)
  // -----------------------------------------------------------------------
  TMROctantArray *sendOctants( TMROctantArray *list,
                               const int *oct_ptr,
                               const int *oct_recv_ptr,
                               int use_node_index=0 );

  // Write out files showing the connectivity
  // ----------------------------------------
  void writeToVTK( const char *filename );
  void writeToTecplot( const char *filename );
  void writeForestToVTK( const char *filename );

 private:
  // Labels for the nodes
  static const int TMR_OCT_NODE_LABEL = 0;
  static const int TMR_OCT_EDGE_LABEL = 1;
  static const int TMR_OCT_FACE_LABEL = 2;
  static const int TMR_OCT_BLOCK_LABEL = 3;

  // Free the internally stored data and zero things
  void freeData();
  void freeMeshData( int free_quads=1, int free_owners=1 );
  void copyData( TMROctForest *copy );
  
  // Compute the node connectivity information
  void computeNodesToBlocks();

  // Compute the connectivity information
  void computeEdgesFromNodes();
  void computeFacesFromNodes();

  // Compute the inverse connectivities
  void computeEdgesToBlocks();
  void computeFacesToBlocks();
  
  // Set the owners - this determines how the mesh will be ordered
  void computeBlockOwners();

  // Get the octant owner
  int getOctantMPIOwner( TMROctant *oct );

  // match the ownership intervals
  void matchOctantIntervals( TMROctant *array,
                             int size, int *ptr );
  void matchMPIIntervals( TMROctant *array,
                          int size, int *ptr );

  // Balance-related routines
  // ------------------------
  // Balance the octant across the local tree and the forest
  void balanceOctant( TMROctant *oct,
                      TMROctantHash *hash, TMROctantHash *ext_hash,
                      TMROctantQueue *queue,
                      const int balance_corner,
                      const int balance_tree );

  // Add adjacent octants to the hashes/queues for balancing
  void addFaceNeighbors( int face_index, 
                         TMROctant p,
                         TMROctantHash *hash,
                         TMROctantHash *ext_hash,
                         TMROctantQueue *queue );
  void addEdgeNeighbors( int edge_index, 
                         TMROctant p,
                         TMROctantHash *hash,
                         TMROctantHash *ext_hash,
                         TMROctantQueue *queue );
  void addCornerNeighbors( int corner, 
                           TMROctant p,
                           TMROctantHash *hash,
                           TMROctantHash *ext_hash,
                           TMROctantQueue *queue );

  // Add octants to adjacent non-owner processor queues
  void addAdjacentFaceToQueue( int face_index,
                               TMROctant p,
                               TMROctantQueue *queue, 
                               TMROctant orig );
  void addAdjacentEdgeToQueue( int edge_index,
                               TMROctant p,
                               TMROctantQueue *queue, 
                               TMROctant orig );
  void addAdjacentCornerToQueue( int corner,
                                 TMROctant p,
                                 TMROctantQueue *queue, 
                                 TMROctant orig );

  // Exchange non-local octant neighbors
  void computeAdjacentOctants();

  // Find the dependent faces and edges in the mesh
  void computeDepFacesAndEdges();
  int checkAdjacentFaces( int face_index, TMROctant *neighbor );
  int checkAdjacentEdges( int edge_index, TMROctant *neighbor );

  // Label the dependent nodes on the locally owned blocks
  void labelDependentNodes( int *nodes );

  // Create the global node ownership data
  TMROctantArray* createLocalNodes();

  // Create the local connectivity based on the input node array
  void createLocalConn( TMROctantArray *nodes, const int *node_offset );

  // Get the local node numbers associated with an edge/face
  void getEdgeNodes( TMROctant *oct, int edge_index, 
                     TMROctantArray *nodes, const int *node_offset,
                     int *edge_nodes );
  void getFaceNodes( TMROctant *oct, int face_index,
                     TMROctantArray *nodes, const int *node_offset,
                     int *face_nodes );

  // Create the dependent node connectivity
  void createDependentConn( const int *node_nums,
                            TMROctantArray *nodes, 
                            const int *node_offset );
  
  // Compute the node locations
  void evaluateNodeLocations();

  // The communicator
  MPI_Comm comm;
  int mpi_rank, mpi_size;

  // Information about the type of interpolation
  TMRInterpolationType interp_type;
  double *interp_knots;

  // The owner octants which dictates the partitioning of the octants
  // across processors
  TMROctant *owners;

  // The following data is the same across all processors
  // ----------------------------------------------------
  // Set the nodes/edges/faces/blocks
  int num_nodes, num_edges, num_faces, num_blocks;

  // Information for the face/edge/node connectivity
  int *block_conn, *block_face_conn, *block_edge_conn;
  int *node_block_ptr, *node_block_conn;
  int *edge_block_ptr, *edge_block_conn;
  int *face_block_ptr, *face_block_conn;

  // Store the face/edge/node owners
  int *face_block_owners;
  int *edge_block_owners;
  int *node_block_owners;

  // Information to enable transformations between faces
  int *block_face_ids;

  // Information about the mesh
  int mesh_order;
  int *conn;

  // Set the range of nodes owned by each processor
  int *node_range;

  // The nodes are organized as follows
  // |--- dependent nodes -- | ext_pre | -- owned local -- | - ext_post -|

  // The following data is processor-local
  int *node_numbers; // All the local node numbers ref'd on this proc
  int num_local_nodes; // Total number of locally ref'd nodes
  int num_dep_nodes; // Number of dependent nodes
  int num_owned_nodes; // Number of nodes that are owned by me
  int ext_pre_offset; // Number of nodes before pre

  // The dependent node information
  int *dep_ptr, *dep_conn;
  double *dep_weights;

  // The array of all octants
  TMROctantArray *octants;
  
  // The octants that are adjacent to this processor
  TMROctantArray *adjacent;

  // The array of all the nodes
  TMRPoint *X;

  // The topology of the underlying model (if any)
  TMRTopology *topo;
};

#endif // TMR_OCTANT_FOREST_H
