from mpi4py import MPI
from tacs import TACS, elements
from tmr import TMR
from paropt import ParOpt
import numpy as np
from six import iteritems

try:
    from scipy.optimize import minimize
except:
    minimize = None


def createTopoProblem(
    forest,
    callback,
    filter_type,
    nlevels=2,
    repartition=True,
    design_vars_per_node=1,
    r0=0.05,
    N=10,
    lowest_order=2,
    ordering=TACS.MULTICOLOR_ORDER,
    use_galerkin=False,
    scale_coordinate_factor=1.0,
):
    """
    Create a topology optimization problem instance and a hierarchy of meshes.
    This code takes in the OctForest or QuadForest on the finest mesh level
    and creates a series of coarser meshes for analysis and optimization.
    The discretization at each level is created via a callback function that
    generates the appropriate TACSCreator object and its associated filter (the
    QuadForest or OctForest on which the design parametrization is defined.)
    The code then creates a TMRTopoFilter class which stores information about
    the design parametrization and hierarchy. It creates a multigrid object and
    finally a TMRTopoProblem instance for optimization.

    The callback function takes in a forest object, corresponding to the finite-
    element discretization and returns a creator object and a filter object in
    the following form:

    creator, filter = callback(forest)

    Args:
        callback: A callback function that takes in the forest and
                  returns the filter and the associated creator class
        filter_type (str): Type of filter to create
        forest (TMROctForest or TMRQuadForest): Forest type
        repartition (bool): Repartition the mesh
        design_vars_per_node (int): number of design variables for each node
        r0 (float): Helmholtz/matrix filter radius
        N (int): Matrix filter approximation parameter
        lowest_order (int): Lowest order mesh to create
        ordering: TACS Assembler ordering type
        use_galerkin: Use Galerkin projection to obtain coarse grid operators
        scale_coordinate_factor (float): Scale all coordinates by this factor

    Returns:
        problem (TopoProblem): The allocated topology optimization problem
    """

    # Store data
    forests = []
    filters = []
    assemblers = []

    # Balance the forest and repartition across processors
    forest.balance(1)
    if repartition:
        forest.repartition()

    # Create the forest object
    creator, filtr = callback(forest)
    forests.append(forest)
    filters.append(filtr)
    assemblers.append(creator.createTACS(forest, ordering))

    for i in range(nlevels - 1):
        order = forests[-1].getMeshOrder()
        interp = forests[-1].getInterpType()
        if order > lowest_order:
            forest = forests[-1].duplicate()
            order = order - 1
            forest.setMeshOrder(order, interp)
        else:
            forest = forests[-1].coarsen()
            forest.setMeshOrder(order, interp)

            # Balance and repartition if needed
            forest.balance(1)
            if repartition:
                forest.repartition()

        # Create the forest object
        creator, filtr = callback(forest)
        forests.append(forest)
        filters.append(filtr)
        assemblers.append(creator.createTACS(forest, ordering))

    # Scale the coordinates by scale_coordinates factor if it is != 1.0
    if scale_coordinate_factor != 1.0:
        for assembler in assemblers:
            X = assembler.createNodeVec()
            assembler.getNodes(X)
            X.scale(scale_coordinate_factor)
            assembler.setNodes(X)

    # Create the multigrid object
    mg = TMR.createMg(assemblers, forests, use_galerkin=use_galerkin)

    # Create the TMRTopoFilter object
    filter_obj = None
    if callable(filter_type):
        filter_obj = filter_type(assemblers, filters)
    elif isinstance(filter_type, str):
        if filter_type == "lagrange":
            filter_obj = TMR.LagrangeFilter(assemblers, filters)
        elif filter_type == "matrix":
            filter_obj = TMR.MatrixFilter(r0, N, assemblers, filters)
        elif filter_type == "conform":
            filter_obj = TMR.ConformFilter(assemblers, filters)
        elif filter_type == "helmholtz":
            filter_obj = TMR.HelmholtzFilter(r0, assemblers, filters)

    problem = TMR.TopoProblem(filter_obj, mg)

    return problem


def computeVertexLoad(name, forest, assembler, point_force):
    """
    Add a load at vertices with the given name value. The assembler object must
    be created from the forest. The point_force must be equal to the number of
    variables per node in the assembler object.

    Args:
        name (str): Name of the surface where the traction will be added
        forest (QuadForest or OctForest): Forest for the finite-element mesh
        assembler (Assembler): TACSAssembler object for the finite-element problem
        point_force (list): List of point forces to apply at the vertices

    Returns:
        Vec: A force vector containing the point load
    """

    # Get the number of variable per node from the assembler
    vars_per_node = assembler.getVarsPerNode()
    if vars_per_node != len(point_force):
        raise ValueError("Point force length must be equal to vars_per_node")

    # Create the force vector and extract the array
    force = assembler.createVec()
    force_array = force.getArray()

    # Retrieve the node numbers from the forest
    nodes = forest.getNodesWithName(name)

    comm = assembler.getMPIComm()
    node_range = forest.getNodeRange()

    # Add the point force into the force arrays
    for node in nodes:
        if (node >= node_range[comm.rank]) and (node < node_range[comm.rank + 1]):
            index = node - node_range[comm.rank]
            force_array[
                vars_per_node * index : vars_per_node * (index + 1)
            ] += point_force[:]

    # Match the ordering of the vector
    assembler.reorderVec(force)

    return force


def computeTractionLoad(names, forest, assembler, trac):
    """
    Add a surface traction to all quadrants or octants that touch a face or edge with
    the given name. The assembler must be created from the provided forest. The list
    trac must have a traction for each face (6) for octants or each edge (4) for
    quadrants.

    Note: This code uses the fact that the getOctsWithName or getQuadsWithName returns
    the local face or edge index touching the surface or edge in the info member.

    Args:
        names (str) or list[(str)]: Name or list of names of the surface(s) where the traction will be added
        forest (QuadForest or OctForest): Forest for the finite-element mesh
        assembler (Assembler): TACSAssembler object for the finite-element problem
        trac (list): List of tractions, one for each possible face/edge orientation

    Returns:
        Vec: A force vector containing the traction
    """

    if isinstance(forest, TMR.OctForest):
        octants = forest.getOctants()
        if isinstance(names, str):
            face_octs = forest.getOctsWithName(names)
        else:
            face_octs = []
            for name in names:
                face_octs.extend(forest.getOctsWithName(name))
    elif isinstance(forest, TMR.QuadForest):
        octants = forest.getQuadrants()
        if isinstance(names, str):
            face_octs = forest.getQuadsWithName(names)
        else:
            face_octs = []
            for name in names:
                face_octs.extend(forest.getQuadsWithName(name))

    # Create the force vector and zero the variables in the assembler
    force = assembler.createVec()
    assembler.zeroVariables()
    assembler.zeroDotVariables()
    assembler.zeroDDotVariables()

    # Create the auxiliary element class
    aux = TACS.AuxElements()

    for i in range(len(face_octs)):
        index = face_octs[i].tag
        if index is not None:
            aux.addElement(index, trac[face_octs[i].info])

    # Keep auxiliary elements already set in the assembler
    # aux_tmp = assembler.getAuxElements()
    assembler.setAuxElements(aux)

    # Compute the residual where force = -residual
    assembler.assembleRes(force)
    force.scale(-1.0)

    # Reset the auxiliary elements
    assembler.setAuxElements(None)  # (aux_tmp)

    return force


def compute3DTractionLoad(name, forest, assembler, tr):
    """
    Add a constant surface traction to all octants that touch a face or edge with
    the given name.

    Args:
        forest (QuadForest or OctForest): Forest for the finite-element mesh
        name (str): Name of the surface where the traction will be added
        assembler (Assembler): TACSAssembler object for the finite-element problem
        tr (list): The 3D components of the traction.

    Returns:
        Vec: A force vector containing the traction
    """

    # Get the basis
    element = assembler.getElements()[0]
    basis = element.getElementBasis()

    # Get the number of variables per node
    vars_per_node = assembler.getVarsPerNode()

    trac = []
    for findex in range(6):
        trac.append(elements.Traction3D(vars_per_node, findex, basis, tr))

    return computeTractionLoad(name, forest, assembler, trac)


def interpolateDesignVec(orig_filter, orig_vec, new_filter, new_vec):
    """
    This function interpolates a design vector from the original design space defined
    on an OctForest or QuadForest and interpolates it to a new OctForest or QuadForest.

    This function is used after a mesh adaptation step to get the new design space.

    Args:
        orig_filter (OctForest or QuadForest): Original filter Oct or QuadForest object
        orig_vec (PVec): Design variables on the original mesh in a ParOpt.PVec
        new_filter (OctForest or QuadForest): New filter Oct or QuadForest object
        new_vec (PVec): Design variables on the new mesh in a ParOpt.PVec (set on ouput)
    """

    # Convert the PVec class to TACSBVec
    orig_x = TMR.convertPVecToVec(orig_vec)
    if orig_x is None:
        raise ValueError("Original vector must be generated by TMR.TopoProblem")
    new_x = TMR.convertPVecToVec(new_vec)
    if new_x is None:
        raise ValueError("New vector must be generated by TMR.TopoProblem")

    if orig_x.getVarsPerNode() != new_x.getVarsPerNode():
        raise ValueError("Number of variables per node must be consistent")

    orig_map = orig_x.getNodeMap()
    new_map = new_x.getNodeMap()
    vars_per_node = orig_x.getVarsPerNode()

    # Create the interpolation class
    interp = TACS.VecInterp(orig_map, new_map, vars_per_node)
    new_filter.createInterpolation(orig_filter, interp)
    interp.initialize()

    # Perform the interpolation
    interp.mult(orig_x, new_x)

    return


def addNaturalFrequencyConstraint(problem, omega_min, **kwargs):
    """
    Add a natural frequency constraint to a TopoProblem optimization problem

    This function automatically sets good default arguments that can be
    overridden with keyword arguments passed in through kwargs.

    Args:
        problem (TopoProblem): TopoProblem optimization problem
        omega_min (float): Minimum natural frequency, Hz
        **kwargs: Frequency constraint parameters; check
                TMR documentation for more detail
    """
    # Convert the provided minimum natural frequency from
    # Hz to rad/s, square it, and make it negative to fit the
    # constraint form: omega^2 - offset >= 0.0
    offset = -((2.0 * np.pi * omega_min) ** 2)

    # Define all the possible arguments and set defaults
    opts = {
        "use_jd": True,
        "num_eigs": 10,
        "ks_weight": 50.0,
        "offset": offset,
        "sigma": -offset,
        "scale": -0.75 / offset,
        "max_lanczos": 100,
        "tol": 1e-30,
        "eig_tol": 5e-7,
        "eig_rtol": 1e-6,
        "eig_atol": 1e-12,
        "num_recycle": 10,
        "fgmres_size": 8,
        "max_jd_size": 50,
        "recycle_type": "num_recycling",
    }

    # Apply the user defined parameters
    for key, value in kwargs.items():
        if key in opts:
            opts[key] = value
        else:
            raise ValueError("%s is not a valid option" % (key))

    if opts["use_jd"]:
        # Set the recycling strategy
        if opts["recycle_type"] == "num_recycling":
            recycle_type = TACS.NUM_RECYCLE
        else:
            recycle_type = TACS.SUM_TWO

        problem.addFrequencyConstraint(
            opts["sigma"],
            opts["num_eigs"],
            opts["ks_weight"],
            opts["offset"],
            opts["scale"],
            opts["max_jd_size"],
            opts["eig_tol"],
            opts["use_jd"],
            opts["fgmres_size"],
            opts["eig_rtol"],
            opts["eig_atol"],
            opts["num_recycle"],
            recycle_type,
        )
    else:  # use the Lanczos method
        problem.addFrequencyConstraint(
            opts["sigma"],
            opts["num_eigs"],
            opts["ks_weight"],
            opts["offset"],
            opts["scale"],
            opts["max_lanczos"],
            opts["tol"],
            0,
            0,
            0,
            0,
            0,
            TACS.SUM_TWO,
            opts["track_eigen_iters"],
        )

    return


def densityBasedRefine(
    forest,
    assembler,
    index=0,
    lower=0.05,
    upper=0.5,
    reverse=False,
    min_lev=0,
    max_lev=TMR.MAX_LEVEL,
):
    """
    Apply a density-based refinement criteria.

    This function takes in a Quad or OctForest that has been used for analysis and its
    corresponding Assembler object. It then uses the data set in the constitutive object
    to extract the density within each element. If the density falls below the the bound
    *lower* the element is coarsened, if the density exceeds *upper* the element is
    refined. If *reverse* is set, this scheme is reversed so low design values are
    refined. The refinement is applied directly to the forest.

    Args:
        forest (QuadForest or OctForest): OctForest or QuadForest to refine
        assembler (Assembler): The TACS.Assembler object associated with forest
        index (int): The component index of the design vector used to indicate material
        lower (float): the lower limit used for coarsening
        upper (float): the upper limit used for refinement
        reverse (bool): Reverse the refinement scheme
        min_lev (int): Minimum refinement level
        max_lev (int): Maximum refinement level
    """

    # Create refinement array
    num_elems = assembler.getNumElements()
    refine = np.zeros(num_elems, dtype=np.int32)

    # Get the elements from the Assembler object
    elems = assembler.getElements()

    for i in range(num_elems):
        # Extract the design variables from the element
        dvs_per_node = elems[i].getDesignVarsPerNode()
        dvs = elems[i].getDesignVars(i)

        # Apply the refinement criteria
        if reverse:
            value = np.min(dvs[index::dvs_per_node])
            if value >= upper:
                refine[i] = -1
            elif value <= lower:
                refine[i] = 1
        else:
            value = np.max(dvs[index::dvs_per_node])
            if value >= upper:
                refine[i] = 1
            elif value <= lower:
                refine[i] = -1

    # Refine the forest
    forest.refine(refine, min_lev=min_lev, max_lev=max_lev)

    return


def approxDistanceRefine(
    forest,
    fltr,
    assembler,
    refine_distance,
    index=0,
    domain_length=1.0,
    tfactor=0.05,
    cutoff=0.15,
    filename=None,
    min_lev=0,
    max_lev=TMR.MAX_LEVEL,
):
    """
    Apply a distance-based refinement criteria.

    This function takes in a forest associated with the analysis, a filter associated
    with the design variables and the corresponding assembler object. An approximate
    distance function is computed using TMR which gives an approximation of the distance
    to the closest point on the domain boundary. In this case, the domain boundary is
    approximated as those points that are intermediate in [cutoff, 1-cutoff]. Since these
    are applied to the filtered (not projected) states, there will be intermediate density
    values. Finally, all elements that contain values that are within refine_distance to
    the approximate boundary are refined, while all other elements are coarseend.

    Notes: The index controls which component of the design variable is used to estimate
    the distance (useful for multimaterial cases). The tfactor controls the approximation,
    larger values of tfactor lead to more diffusive approximations, but small values may
    lead to numerical issues. The actual factor value is determined baesd on the domain
    length parameter which gives the characteristic length of the domain.

    Args:
        forest (QuadForest or OctForest): OctForest or QuadForest to refine
        filtr (QuadForest or OctForest): OctForest or QuadForest for the filter object
        assembler (Assembler): The TACS.Assembler object associated with forest
        refine_distance (float): Refine all elements within this distance
        index (int): The design variable component index (!= 0 for multimaterial cases)
        tfactor (float): Factor applied to the domain_length for computing the approx dist.
        cutoff (float): Cutoff to indicate structural interface
        min_lev (int): Minimum refinement level
        max_lev (int): Maximum refinement level
    """

    # Set up and solve for an approximate level set function
    x = assembler.createDesignVec()
    assembler.getDesignVars(x)

    # Approximate the distance to the boundary
    dist = TMR.ApproximateDistance(
        fltr,
        x,
        index=index,
        cutoff=cutoff,
        t=tfactor * domain_length,
        filename=filename,
    )

    # Create refinement array
    num_elems = assembler.getNumElements()
    refine = np.zeros(num_elems, dtype=np.int32)

    for i in range(num_elems):
        # Apply the refinement criteria
        if dist[i] <= refine_distance:
            refine[i] = 1
        else:
            refine[i] = -1

    # Refine the forest
    forest.refine(refine, min_lev=min_lev, max_lev=max_lev)

    return


def targetRefine(
    forest,
    fltr,
    assembler,
    refine_distance,
    interface_lev=2,
    interior_lev=1,
    interface_index=-1,
    interior_index=0,
    reverse=False,
    domain_length=1.0,
    tfactor=0.05,
    cutoff=0.15,
    filename=None,
    min_lev=0,
    max_lev=TMR.MAX_LEVEL,
):
    """
    Apply a target-based refinement strategy.

    This refinement strategy employs a targeted refinement strategy. The goal is to
    refine the interface elements, defined from an approximate distance calculation,
    and the interior elements, defined as those elements with a given threshold of
    the density field that are not close to the interface, to a prescribed level at
    the first iteration. All other elements are coarsened aggressively.

    Note: The interface and interior can be computed using different indices in
    multimaterial optimization. When the interface index is negative, all materials are
    considered during the interface distance calculation.

    Args:
        forest (QuadForest or OctForest): OctForest or QuadForest to refine
        filtr (QuadForest or OctForest): OctForest or QuadForest for the filter object
        assembler (Assembler): The TACS.Assembler object associated with forest
        refine_distance (float): Refine all elements within this distance
        interface_lev (int): Target interface refinement level
        interior_lev (int): Target interior refinement level
        interface_index (int): Design variable component index for the interface problem
        interior_index (int): Design variable component index for the interior
        reverse (boolean): Reverse the sense of the interior refinement
        tfactor (float): Factor applied to the domain_length for computing the approx dist.
        cutoff (float): Cutoff to indicate structural interface
        filename (str): File name for the approximate distance calculation
        min_lev (int): Minimum refinement level
        max_lev (int): Maximum refinement level
    """

    # Set up and solve for an approximate level set function
    x = assembler.createDesignVec()
    assembler.getDesignVars(x)

    # Approximate the distance to the boundary
    dist = TMR.ApproximateDistance(
        fltr,
        x,
        index=interface_index,
        cutoff=cutoff,
        t=tfactor * domain_length,
        filename=filename,
    )

    # Create refinement array
    num_elems = assembler.getNumElements()
    refine = np.zeros(num_elems, dtype=np.int32)

    # Compute the levels
    if isinstance(forest, TMR.OctForest):
        octants = forest.getOctants()
        lev = np.zeros(len(octants))
        for i, oc in enumerate(octants):
            lev[i] = oc.level
    elif isinstance(forest, TMR.QuadForest):
        quads = forest.getQuadrants()
        lev = np.zeros(len(quads))
        for i, quad in enumerate(quads):
            lev[i] = quad.level

    # Get the elements from the Assembler object
    elems = assembler.getElements()

    for i in range(num_elems):
        # Apply the refinement criteria
        if dist[i] <= refine_distance:
            refine[i] = interface_lev - lev[i]
        else:
            # Now check whether this is in the interior or exterior of
            # the domain
            dvs_per_node = elems[i].getDesignVarsPerNode()
            dvs = elems[i].getDesignVars(i)

            # Apply the refinement criteria
            if reverse:
                value = np.min(dvs[interior_index::dvs_per_node])
                if value >= 1.0 - cutoff:
                    refine[i] = -1
                elif value <= cutoff:
                    refine[i] = interior_lev - lev[i]
            else:
                value = np.max(dvs[interior_index::dvs_per_node])
                if value >= 1.0 - cutoff:
                    refine[i] = interior_lev - lev[i]
                elif value <= cutoff:
                    refine[i] = -1

    # Refine the forest
    forest.refine(refine, min_lev=min_lev, max_lev=max_lev)

    return


class OptFilterWeights:
    def __init__(self, diag, X, H):
        """
        Compute an approximation of the coefficients of a Helmholtz filter.

        Args:
            diag (int): The index of the diagonal (base point) of the stencil
            X (np.ndarray): An array of the node positions
            H (np.ndarray): Symmetric matrix of second derivatives for the filter
        """
        self.diag = diag
        self.X = X
        self.n = self.X.shape[0]

        # Compute the normalization
        if len(self.X.shape) == 1:
            self.delta = np.max(np.absolute(self.X - self.X[self.diag]))
        else:
            self.delta = np.sqrt(
                np.max(
                    np.sum(
                        (self.X - self.X[self.diag, :])
                        * (self.X - self.X[self.diag, :]),
                        axis=1,
                    )
                )
            )

        self.dim = 3
        if len(self.X.shape) == 1 or self.X.shape[1] == 1:
            self.dim = 1

            # Compute the constraint matrix
            A = np.zeros((2, self.n - 1))

            # Populate the b vector
            b = np.zeros(2)
            b[1] = H[0, 0]

            index = 0
            for i in range(self.n):
                if i != self.diag:
                    dx = (self.X[i] - self.X[self.diag]) / self.delta

                    A[0, index] = dx
                    A[1, index] = 0.5 * dx**2
                    index += 1

        elif self.X.shape[1] == 2:
            self.dim = 2

            # Compute the constraint matrix
            A = np.zeros((5, self.n - 1))

            # Populate the b vector
            b = np.zeros(5)
            b[2] = H[0, 0]
            b[3] = H[1, 1]
            b[4] = 2.0 * H[0, 1]

            index = 0
            for i in range(self.n):
                if i != self.diag:
                    dx = (self.X[i, 0] - self.X[self.diag, 0]) / self.delta
                    dy = (self.X[i, 1] - self.X[self.diag, 1]) / self.delta

                    A[0, index] = dx
                    A[1, index] = dy
                    A[2, index] = 0.5 * dx**2
                    A[3, index] = 0.5 * dy**2
                    A[4, index] = dx * dy
                    index += 1
        else:
            # Compute the constraint matrix
            A = np.zeros((9, self.n - 1))

            # Populate the b vector
            b = np.zeros(9)
            b[3] = H[0, 0]
            b[4] = H[1, 1]
            b[5] = H[2, 2]
            b[6] = 2.0 * H[1, 2]
            b[7] = 2.0 * H[0, 2]
            b[8] = 2.0 * H[0, 1]

            index = 0
            for i in range(self.n):
                if i != self.diag:
                    dx = (self.X[i, 0] - self.X[self.diag, 0]) / self.delta
                    dy = (self.X[i, 1] - self.X[self.diag, 1]) / self.delta
                    dz = (self.X[i, 2] - self.X[self.diag, 2]) / self.delta

                    A[0, index] = dx
                    A[1, index] = dy
                    A[2, index] = dz

                    A[3, index] = 0.5 * dx**2
                    A[4, index] = 0.5 * dy**2
                    A[5, index] = 0.5 * dz**2

                    A[6, index] = dy * dz
                    A[7, index] = dx * dz
                    A[8, index] = dx * dy
                    index += 1

        self.b = b
        self.A = A

        return

    def obj_func(self, w):
        """Evaluate the sum square of the weights"""
        return 0.5 * np.sum(w**2)

    def obj_func_der(self, w):
        """Evaluate the derivative of the sum square of weights"""
        return w

    def con_func(self, w):
        """Compute the interpolation constraints"""
        return np.dot(self.A, w) - self.b

    def con_func_der(self, w):
        """Compute the derivative of the interpolation ocnstraints"""
        return self.A

    def set_alphas(self, w, alpha):
        """Compute the interpolating coefficients based on the weights"""
        alpha[:] = 0.0
        index = 0
        for i in range(self.n):
            if i != self.diag:
                alpha[i] = w[index] / self.delta**2
                alpha[self.diag] += w[index] / self.delta**2
                index += 1

        alpha[self.diag] += 1.0

        return


class Mfilter(TMR.HelmholtzPUFilter):
    def __init__(self, N, assemblers, filters, vars_per_node=1, dim=2, r=0.01):
        """
        Create an M-filter: A type of Helmholtz partition of unity filter that
        approximates the Helmholtz PDE-based filter and maintains positive
        coefficients over a range of meshes.

        Args:
            N (int): Number of terms in the approximate Neumann inverse
            assemblers (list): List of TACS.Assembler objects
            filters (list): List of TMR.QuadForest or TMR.OctForest objects
            vars_per_node (int): Number of design variables at each node
            dim (int): Spatial dimension of the problem
            r (float): Filter radius

        Note: You must call initialize() on the filter before use.
        """
        self.r = r
        self.dim = dim
        return

    def getInteriorStencil(self, diag, X, alpha):
        """Get the weights for an interior stencil point"""
        H = self.r**2 * np.eye(3)

        # Reshape the values in the matrix
        X = X.reshape((-1, 3))
        n = X.shape[0]

        if self.dim == 2:
            X = X[:, :2]

        # Set up the optimization problem
        opt = OptFilterWeights(diag, X, H)

        # Set the bounds and initial point
        w0 = np.ones(n - 1)
        bounds = []
        for i in range(n - 1):
            bounds.append((0, None))

        res = minimize(
            opt.obj_func,
            w0,
            jac=opt.obj_func_der,
            method="SLSQP",
            bounds=bounds,
            constraints={"type": "eq", "fun": opt.con_func, "jac": opt.con_func_der},
        )

        # Set the optimized alpha values
        opt.set_alphas(res.x, alpha)

        return

    def getBoundaryStencil(self, diag, normal, X, alpha):
        """Get a sentcil point on the domain boundary"""
        H = self.r**2 * np.eye(2)

        # Reshape the values in the matrix
        X = X.reshape((-1, 3))
        n = X.shape[0]
        if self.dim == 2:
            X = X[:, :2]

            t = np.array([normal[1], -normal[0]])
            Xt = np.dot(X - X[diag, :], t)
        elif self.dim == 3:
            # Reduce the problem to a 2d problem on linearization of the
            # the domain boundary. First, compute an arbitrary direction
            # that is not aligned along the normal direction
            index = np.argmin(np.absolute(normal))
            t = np.zeros(3)
            t[index] = 1.0

            # Compute the in-plane directions (orthogonal to the normal direction)
            t2 = np.cross(t, normal)
            t1 = np.cross(normal, t2)

            # Reduce the problem on the boundary
            Xt = np.zeros((n, 2))
            Xt[:, 0] = np.dot(X - X[diag, :], t1)
            Xt[:, 1] = np.dot(X - X[diag, :], t2)

        # Set up the optimization problem
        opt = OptFilterWeights(diag, Xt, H)

        # Set the bounds and initial point
        w0 = np.ones(n - 1)
        bounds = []
        for i in range(n - 1):
            bounds.append((0, None))

        res = minimize(
            opt.obj_func,
            w0,
            jac=opt.obj_func_der,
            method="SLSQP",
            bounds=bounds,
            constraints={"type": "eq", "fun": opt.con_func, "jac": opt.con_func_der},
        )

        # Set the optimized alpha values
        opt.set_alphas(res.x, alpha)

        return


def setSurfaceBounds(
    problem, comm, forest, names, face_lb=0.99, face_ub=1.0, constrain_octs=True
):
    """
    Set upper and lower bounds on specific faces to
    "require" material on certain boundaries

    Args:
        problem: TopoProblem object
        comm: MPI communicator object
        forest: TMROct(or Quad)Forest object
        names (list): list of surface names where these
                      bounds should be applied
        face_lb: lower bound value to apply
        face_ub: upper bound value to apply
        constrain_octs (bool): if True, constrain
            the octants/quadrants on the surface;
            If False, only constrain the boundary
            nodes.

    """
    assembler = problem.getAssembler()

    x_vec = assembler.createDesignVec()
    assembler.getDesignVars(x_vec)
    x = x_vec.getArray()

    dv = problem.createDesignVec()
    lb = problem.createDesignVec()
    ub = problem.createDesignVec()
    dv[:] = x[:]
    lb[:] = 1e-3
    ub[:] = 1.0

    face_dv = 0.5 * (face_lb + face_ub)
    for name in names:
        mpi_rank = comm.Get_rank()
        node_range = forest.getNodeRange()

        if constrain_octs:
            if isinstance(forest, TMR.OctForest):
                octs = forest.getOctsWithName(name)
            else:
                octs = forest.getQuadsWithName(name)
            conn = forest.getMeshConn()
            node_octs = np.array([])
            for oc in octs:
                node_octs = np.append(node_octs, conn[oc.tag, :])

        else:
            node_octs = forest.getNodesWithName(name)
        node_octs = node_octs.astype(int)

        for i in range(len(node_octs)):
            if (node_octs[i] >= node_range[mpi_rank]) and (
                node_octs[i] < node_range[mpi_rank + 1]
            ):
                index = int(node_octs[i] - node_range[mpi_rank])
                dv[index] = face_dv
                lb[index] = face_lb
                ub[index] = face_ub

    problem.setInitDesignVars(dv, lbvec=lb, ubvec=ub)
    return


def createNonDesignMassVec(assembler, names, comm, forest, m0=0.5):
    """
    Create a design vector in ParOptVec type that contains the non-design
    mass on nodes with given names

    Args:
        names (list): list of surface names where non-design mass should be added
        m0 (float): non-design mass

    Return:
        mvec (BVec): a ParOpt design vector that contains non-design mass information
    """
    mvec = assembler.createDesignVec()
    mvals = mvec.getArray()

    for name in names:
        mpi_rank = comm.Get_rank()
        node_range = forest.getNodeRange()

        node_octs = forest.getNodesWithName(name)
        node_octs = node_octs.astype(int)

        for i in range(len(node_octs)):
            if (node_octs[i] >= node_range[mpi_rank]) and (
                node_octs[i] < node_range[mpi_rank + 1]
            ):
                index = int(node_octs[i] - node_range[mpi_rank])
                mvals[index] = m0

    return mvec
