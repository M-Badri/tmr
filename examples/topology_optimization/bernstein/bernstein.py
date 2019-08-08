from __future__ import print_function
from mpi4py import MPI
from tmr import TMR, TopOptUtils
from paropt import ParOpt
from tacs import TACS, elements, constitutive, functions
import numpy as np
import argparse
import os

class QuadConformCreator(TMR.QuadConformTopoCreator):
    def __init__(self, bcs, forest, order, interp, props=None):
        # Set the interpolation for the new filter
        TMR.QuadConformTopoCreator.__init__(bcs, forest, order, interp)
        
        # Create the array of properties
        self.props = props
        
    def createElement(self, order, quadrant, index, filtr):
        '''Create the element'''
        stiff = TMR.ThermoQuadStiffness(self.props, index, None, filtr)
        elem = elements.PSThermoelasticQuad(order, stiff)
        return elem

class CreatorCallback:
    def __init__(self, bcs, props):
        self.bcs = bcs
        self.props = props

    def creator_callback(self, forest):
        order = forest.getMeshOrder()-1
        interp = TMR.BERNSTEIN_POINTS
        creator = QuadConformCreator(self.bcs, forest, order, interp, self.props)
        return creator, creator.getFilter()

def create_forest(comm, depth, htarget):
    # Load the geometry model
    geo = TMR.LoadModel('biclamped_traction.stp')

    # Mark the boundary condition faces
    verts = geo.getVertices()
    edges = geo.getEdges()
    faces = geo.getFaces()

    edges[1].setName('fixed')
    edges[9].setName('fixed')
    edges[4].setName('traction')

    # Set the boundary conditions for the problem
    bcs = TMR.BoundaryConditions()
    bcs.addBoundaryCondition('fixed', [0,1,2], [0.0,0.0,0.])

    # Create the mesh
    mesh = TMR.Mesh(comm, geo)

    # Set the meshing options
    opts = TMR.MeshOptions()

    # Create the surface mesh
    mesh.mesh(htarget, opts)

    # Create a model from the mesh
    model = mesh.createModelFromMesh()

    # Create the corresponding mesh topology from the mesh-model 
    topo = TMR.Topology(comm, model)

    # Create the quad forest and set the topology of the forest
    forest = TMR.QuadForest(comm)
    forest.setTopology(topo)

    # Set parameters for later usage
    forest.createTrees(depth)

    return forest

def create_problem(forest, bcs, props, nlevels):
    # Allocate the creator callback function
    obj = CreatorCallback(bcs, props)

    # Create a conforming filter
    filter_type = 'conform'

    # Create the problem and filter object
    problem = TopOptUtils.createTopoProblem(forest,
        obj.creator_callback, filter_type, nlevels=nlevels, lowest_order=3,
        design_vars_per_node=design_vars_per_node)

    # Get the assembler object we just created
    assembler = problem.getAssembler()

    # Set the constraint type
    funcs = [functions.StructuralMass(assembler)]

    # Create the traction objects that will be used later..
    T = 2.5e6
    tx = np.zeros(args.order)
    ty = T*np.ones(args.order)

    thermal_tractions = []
    for findex in range(4):
        thermal_tractions.append(elements.PSThermoQuadTraction(findex, tx, ty))

    # Allocate a thermal traction boundary condition
    force1 = TopOptUtils.computeTractionLoad('traction', forest, assembler,
        thermal_tractions)

    # Set the load cases
    problem.setLoadCases([force1])

    # Set the mass constraint
    # (m_fixed - m(x))/m_fixed >= 0.0
    problem.addConstraints(0, funcs, [-m_fixed], [-1.0/m_fixed])
    problem.setObjective(obj_array, [functions.Compliance(assembler)])

    # Initialize the problem and set the prefix
    problem.initialize()

    # Set the output file name
    flag = (TACS.ToFH5.NODES | TACS.ToFH5.DISPLACEMENTS | TACS.ToFH5.EXTRAS)
    problem.setF5OutputFlags(5, TACS.PY_PLANE_STRESS, flag)

    return problem

# Set the optimization parameters
optimization_options = {
    # Parameters for the trust region method
    'tr_init_size': 0.01,
    'tr_max_size': 0.05,
    'tr_min_size': 1e-6,
    'tr_eta': 0.25,
    'tr_penalty_gamma': 20.0,

    # Parameters for the interior point method (used to solve the
    # trust region subproblem)
    'max_qn_subspace': 2,
    'tol': 1e-8,
    'maxiter': 500,
    'norm_type': 'L1',
    'barrier_strategy': 'Complementarity fraction',
    'start_strategy': 'Affine step'}

prefix = 'results'

# Create an argument parser to read in arguments from the command line
p = argparse.ArgumentParser()
p.add_argument('--prefix', type=str, default='./results')
p.add_argument('--vol_frac', type=float, default=0.25)
p.add_argument('--htarget', type=float, default=2.5e-3)
p.add_argument('--max_opt_iters', type=int, nargs='+',
               default=[50])
p.add_argument('--init_depth', type=int, default=1)
p.add_argument('--mg_levels', type=int, nargs='+', default=[2])
p.add_argument('--use_L1', action='store_true', default=True)
p.add_argument('--use_Linf', action='store_true', default=False)
p.add_argument('--order', type=int, default=4)
p.add_argument('--q_penalty', type=float, default=8.0)
p.add_argument('--omega', type=float, default=1.0)
args = p.parse_args()

# Set the communicator
comm = MPI.COMM_WORLD

# Print out all of the arguments to the command line
if comm.rank == 0:
    for arg in vars(args):
        print('%-20s'%(arg), getattr(args, arg))

# Set the max nmber of iterations
mg_levels = args.mg_levels
max_iterations = len(mg_levels)

# Input-dependent parameters
optimization_options['output_file'] = os.path.join(args.prefix, 'output_file.dat')
optimization_options['tr_output_file'] = os.path.join(args.prefix, 'tr_output_file.dat')

# Compute the volume of the bracket
r = 0.06
a = 0.04
vol = r*a
vol_frac = args.vol_frac

# Set the values of the objective array
obj_array = [ 1e0 ]
thickness = 1.0e-2
rho = [2600*thickness, 1300.*thickness]
E = [70e9*thickness, 35e9*thickness]
nu = [0.3, 0.3]
aT = [23.5e-6, 23.5e-6*0.5]
kcond = [130.0*thickness, 65.0*thickness]
ys = [450e6*thickness, 275e6*thickness]

# Set the fixed mass
max_density = sum(rho)/len(rho)
initial_mass = vol*max_density
m_fixed = vol_frac*initial_mass

# Set the number of variables per node
design_vars_per_node = 1
if (len(rho) > 1):
    design_vars_per_node = 1+len(rho)

# Create the stiffness properties object
props = TMR.QuadStiffnessProperties(rho, E, nu, _aT=aT, _kcond=kcond,
                                    k0=1e-3, eps=0.2,
                                    q=args.q_penalty, qtemp=0.0,
                                    qcond=0.0)

# Set the boundary conditions for the problem
bcs = TMR.BoundaryConditions()
bcs.addBoundaryCondition('fixed', [0,1,2], [0.0,0.0,0.])

time_array = np.zeros(sum(args.max_opt_iters[:]))
t0 = MPI.Wtime()

# Create the initial forest
forest = create_forest(comm, args.init_depth, args.htarget)
forest.setMeshOrder(args.order, TMR.GAUSS_LOBATTO_POINTS)

# Set the original filter to NULL
orig_filter = None
xopt = None

for step in range(max_iterations):
    # Create the TMRTopoProblem instance
    nlevels = mg_levels[step]
    problem = create_problem(forest, bcs, props, nlevels)
    problem.setPrefix(args.prefix)

    # Extract the filter to interpolate design variables
    filtr = problem.getFilter()

    if orig_filter is not None:
        # Create one of the new design vectors
        x = problem.createDesignVec()
        TopOptUtils.interpolateDesignVec(orig_filter, xopt, filtr, x)
        problem.setInitDesignVars(x)

    # Set the new original filter
    orig_filter = filtr

    # Set the max number of iterations
    optimization_options['maxiter'] = args.max_opt_iters[step]

    # Optimize the problem
    opt = TopOptUtils.TopologyOptimizer(problem, optimization_options)
    xopt = opt.optimize()

    # Create refinement array
    assembler = problem.getAssembler()
    num_elems = assembler.getNumElements()
    refine = np.zeros(num_elems, dtype=np.int32)

    # Refine based solely on the value of the density variable
    elems = assembler.getElements()
    
    for i in range(num_elems):        
        c = elems[i].getConstitutive()
        if c is not None:
            if design_vars_per_node == 1:
                density = c.getDVOutputValue(0, np.zeros(3, dtype=float))
            else:
                density = 1.0 - c.getDVOutputValue(2, np.zeros(3, dtype=float))

            # Refine things differently depending on whether the
            # density is above or below a threshold
            if density >= 0.1:
                refine[i] = int(1)
            elif density < 0.05:
                refine[i] = int(-1)
    
    # Refine the forest
    forest.refine(refine, min_lev=0)

# Do an averaging of all the values and write to text file
new_array=np.zeros(len(time_array))
comm.Allreduce(time_array, new_array,op=MPI.SUM)
new_array /= comm.size
if comm.rank == 0:
    np.savetxt(os.path.join(args.prefix,'time.out'), new_array, fmt='%1.6e')
