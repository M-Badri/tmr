from tmr import TMR, TopOptUtils
from tacs import TACS, elements, functions
from paropt import ParOpt
from egads4py import egads
import numpy as np
import openmdao.api as om
import os
import sys
from mpi4py import MPI

sys.path.append("../eigenvalue")
from utils import OctCreator, CreatorCallback, MFilterCreator, OutputCallback


class FrequencyConstr:
    """
    A class that evaluates the smallest eigenvalue, the objective is evaluated
    using an objective callback. We also add non-design mass to loaded nodes in
    order to form a well-posed mass minimization problem under frequency constriant

    this constraint takes the following form:
        c = ks >= 0
    """

    def __init__(
        self,
        prefix,
        domain,
        forest,
        len0,
        AR,
        ratio,
        iter_offset,
        lambda0,
        eig_scale=1.0,
        num_eigenvalues=10,
        max_jd_size=100,
        max_gmres_size=30,
        ksrho=50,
        add_non_design_mass=True,
        mscale=10.0,
        kscale=1.0,
    ):
        """
        Args:
            eig_scale: scale the eigenvalues internally in order to acquire better
                       KS approximation with smaller skrho
            num_eigenvalues: number of smallest eigenvalues to compute
            ksrho: KS parameter
        """

        # Set objects
        self.forest = forest

        # Set up parameters
        self.prefix = prefix
        self.domain = domain
        self.iter_offset = iter_offset
        self.len0 = len0
        self.AR = AR
        self.lx = len0 * AR
        self.ly = len0
        self.lz = len0
        if domain == "lbracket":
            self.ly = len0 * ratio
        self.ratio = ratio
        self.lambda0 = lambda0
        self.eig_scale = eig_scale
        self.num_eigenvalues = num_eigenvalues
        self.max_jd_size = max_jd_size
        self.max_gmres_size = max_gmres_size
        self.ksrho = ksrho
        self.add_non_design_mass = add_non_design_mass
        self.mscale = mscale
        self.kscale = kscale

        self.old_min_eigval = 0.0

        self.fltr = None
        self.mg = None
        self.assembler = None
        self.comm = None
        self.oper = None
        self.jd = None
        self.eig = None

        # TACS Vectors
        self.eigv = None
        self.rho = None
        self.rho_original = None
        self.update = None
        self.temp = None
        self.mvec = None

        # TACS Matrices
        self.mmat = None
        self.m0mat = None
        self.k0mat = None
        self.Amat = None
        self.mgmat = None

        # We keep track of failed qn correction
        self.curvs = []

        # Time qn step
        self.qn_time = []

        return

    def constraint(self, fltr, mg):
        """
        Evaluate the KS aggregation of the smallest eigenvalue for the generalized
        eigenvalue problem:

        ks = -1.0/rho * ln tr exp(-rho*A)

        """

        if self.fltr is None:
            self.mg = mg
            self.mgmat = mg.getMat()
            self.fltr = fltr
            self.assembler = self.fltr.getAssembler()
            self.svec = self.assembler.createDesignVec()
            self.svec_vals = self.svec.getArray()
            self.comm = self.assembler.getMPIComm()
            self.rank = self.comm.rank

            # Initialize space for matrices and vectors
            self.mmat = self.assembler.createMat()
            self.Amat = self.assembler.createMat()
            self.eig = np.zeros(self.num_eigenvalues)
            self.eigv = []
            for i in range(self.num_eigenvalues):
                self.eigv.append(self.assembler.createVec())
            self.deig = []
            for i in range(self.num_eigenvalues):
                self.deig.append(self.assembler.createDesignVec())

            # Allocate vectors for qn correction
            self.rho = self.assembler.createDesignVec()
            self.rho_original = self.assembler.createDesignVec()
            self.update = self.assembler.createDesignVec()
            self.update_vals = self.update.getArray()
            self.temp = self.assembler.createDesignVec()
            self.temp_vals = self.temp.getArray()

            # Set up Jacobi-Davidson eigensolver:
            # Create the operator with given matrix and multigrid preconditioner
            self.oper = TACS.JDSimpleOperator(self.assembler, self.Amat, self.mg)

            # Create the eigenvalue solver and set the number of recycling eigenvectors
            self.jd = TACS.JacobiDavidson(
                self.oper, self.num_eigenvalues, self.max_jd_size, self.max_gmres_size
            )
            self.jd.setTolerances(eig_rtol=1e-6, eig_atol=1e-6, rtol=1e-6, atol=1e-12)
            self.jd.setThetaCutoff(0.01)

            # Compute non-design matrices
            if self.add_non_design_mass:
                indices = getFixedDVIndices(
                    self.forest, self.domain, self.len0, self.AR, self.ratio
                )
                self.computeNonDesignMat(
                    indices, mscale=self.mscale, kscale=self.kscale, save_f5=True
                )

        # Assemble the mass matrix
        self.assembler.assembleMatType(TACS.MASS_MATRIX, self.mmat)

        # Reference the matrix associated with the multigrid preconditioner
        # We finally want:
        # A     = K - lambda0*M + I - old*I
        # mgmat = K - lambda0*M + I - old*I  (positive definite)

        # Assemble the stiffness matrix
        self.assembler.assembleMatType(TACS.STIFFNESS_MATRIX, self.Amat)

        # Apply non-design mass to M and K
        if self.add_non_design_mass:
            self.addNonDesignMat(self.mmat, self.Amat)

        # Assemble A matrix for the simple eigenvalue problem and apply bcs
        # A = K - lambda0*M
        self.Amat.axpy(-self.lambda0, self.mmat)

        # We may shift the eigenvalues of A by adding value to diagonal entries:
        # A <- A - (old-1.0)*I
        # so that all eigenvalues for A are likely to be positive (and no smaller than 1.0)
        self.Amat.addDiag(1.0 - self.old_min_eigval)

        # Apply bcs to A
        self.assembler.applyMatBCs(self.Amat)

        # Copy over from A to mgmat
        self.mgmat.copyValues(self.Amat)

        # Factor the multigrid preconditioner
        self.mg.assembleGalerkinMat()
        self.mg.factor()

        """
        Solve the eigenvalue problem
        """
        self.jd.setRecycle(self.num_eigenvalues)
        self.jd.solve(print_flag=True, print_level=1)

        # Check if succeeded, otherwise try again
        nconvd = self.jd.getNumConvergedEigenvalues()
        if nconvd < self.num_eigenvalues:
            if self.comm.rank == 0:
                print(
                    "[Warning] Jacobi-Davidson failed to converge"
                    " for the first run, starting rerun..."
                )

            self.jd.setRecycle(nconvd)

            # Update mgmat so that it's positive definite
            eig0, err = self.jd.extractEigenvalue(0)
            if eig0 > 0:
                if self.comm.rank == 0:
                    print(
                        "[mgmat] Smallest eigenvalue is already positive, don't update mgmat!"
                    )
            else:
                self.mgmat.addDiag(-eig0)
                self.assembler.applyMatBCs(self.mgmat)
                self.mg.assembleGalerkinMat()
                self.mg.factor()

            # Rerun the solver
            self.jd.solve(print_flag=True, print_level=1)
            nconvd = self.jd.getNumConvergedEigenvalues()

            # If it still fails, raise error, save fail f5 and exit
            if nconvd < self.num_eigenvalues:
                msg = "No enough eigenvalues converged! ({:d}/{:d})".format(
                    nconvd, self.num_eigenvalues
                )

                # set the unconverged eigenvector as state variable for visualization
                for i in range(self.num_eigenvalues):
                    self.eig[i], error = self.jd.extractEigenvector(i, self.eigv[i])
                self.assembler.setVariables(self.eigv[nconvd])

                flag_fail = (
                    TACS.OUTPUT_CONNECTIVITY
                    | TACS.OUTPUT_NODES
                    | TACS.OUTPUT_DISPLACEMENTS
                    | TACS.OUTPUT_EXTRAS
                )
                f5_fail = TACS.ToFH5(self.assembler, TACS.SOLID_ELEMENT, flag_fail)
                f5_fail.writeToFile(os.path.join(self.prefix, "fail.f5"))

                raise ValueError(msg)

        # Extract eigenvalues and eigenvectors
        for i in range(self.num_eigenvalues):
            self.eig[i], error = self.jd.extractEigenvector(i, self.eigv[i])

        # Adjust eigenvalues and shift matrices back:
        # A <- A - I + old*I
        # i.e. now A = K - lambda0*M
        for i in range(self.num_eigenvalues):
            self.eig[i] += self.old_min_eigval - 1.0
        self.Amat.addDiag(self.old_min_eigval - 1.0)
        self.assembler.applyMatBCs(self.Amat)

        # Set the shift value for next optimization iteration
        self.old_min_eigval = self.eig[0]  # smallest eigenvalue

        # Debug: print out residuals
        debug_initialized = 0
        if debug_initialized == 0:
            debug_initialized = 1
            res = self.assembler.createVec()
            one = self.assembler.createVec()
            Av = self.assembler.createVec()
            one_arr = one.getArray()
            one_arr[:] = 1.0
            debug_counter = 0

            residual = np.zeros(self.num_eigenvalues)
            eigvec_l1 = np.zeros(self.num_eigenvalues)
            eigvec_l2 = np.zeros(self.num_eigenvalues)

        for i in range(self.num_eigenvalues):
            self.Amat.mult(self.eigv[i], Av)  # Compute Av

            res.copyValues(Av)
            res.axpy(-self.eig[i], self.eigv[i])  # Compute res = Av - lambda*v

            residual[i] = res.norm()
            eigvec_l1[i] = self.eigv[i].dot(one)  # Compute l1 norm
            eigvec_l2[i] = self.eigv[i].dot(self.eigv[i]) ** 0.5  # Compute l2 norm

        debug_counter += 1
        if self.assembler.getMPIComm().rank == 0:
            print("Optimization iteration:{:4d}".format(debug_counter))
            print(
                "{:4s}{:15s}{:15s}{:15s}".format(
                    "No", "Eig Res", "Eigv l1 norm", "Eigv l2 norm"
                )
            )
            for i in range(self.num_eigenvalues):
                print(
                    "{:4d}{:15.5e}{:15.5e}{:15.5e}".format(
                        i, residual[i], eigvec_l1[i], eigvec_l2[i]
                    )
                )

        # Set first eigenvector as state variable for visualization
        self.assembler.setVariables(self.eigv[0])

        # Scale eigenvalues for a better KS approximation
        self.eig[:] *= self.eig_scale

        # Compute the minimal eigenvalue
        eig_min = np.min(self.eig)

        # Compute KS aggregation
        self.eta = np.exp(-self.ksrho * (self.eig - eig_min))
        self.beta = np.sum(self.eta)
        ks = eig_min - np.log(self.beta) / self.ksrho
        self.eta = self.eta / self.beta

        # Scale eigenvalue back
        self.eig[:] /= self.eig_scale

        # Print values
        if self.comm.rank == 0:
            print("{:30s}{:20.10e}".format("[Constr] KS eigenvalue:", ks))
            print("{:30s}{:20.10e}".format("[Constr] min eigenvalue:", eig_min))

        return [ks]

    def constraint_gradient(self, fltr, mg, vecs, index=0):
        """
        gradient of the spectral aggregate
        g = sum_k eta_k phi^T dAdx phi
        """

        # We only have one constraint
        dcdrho = vecs[index]

        # Zero out the gradient vector
        dcdrho.zeroEntries()

        for i in range(self.num_eigenvalues):
            # Compute the coefficient
            coeff = self.eta[i] * self.eig_scale

            # Compute gradient of eigenvalue
            self.deig[i].zeroEntries()
            self.assembler.addMatDVSensInnerProduct(
                coeff, TACS.STIFFNESS_MATRIX, self.eigv[i], self.eigv[i], self.deig[i]
            )

            self.assembler.addMatDVSensInnerProduct(
                -coeff * self.lambda0,
                TACS.MASS_MATRIX,
                self.eigv[i],
                self.eigv[i],
                self.deig[i],
            )

            # Make sure the vector is properly distributed over all processors
            self.deig[i].beginSetValues(op=TACS.ADD_VALUES)
            self.deig[i].endSetValues(op=TACS.ADD_VALUES)

            # Add the contribution
            dcdrho.axpy(1.0, self.deig[i])

        # Compute gradient norm
        norm = dcdrho.norm()
        if self.comm.rank == 0:
            print("{:30s}{:20.10e}".format("[Constr] gradient norm:", norm))

        self.dcdrho = dcdrho
        return

    def qn_correction(self, zero_idx, z, s, y):
        """
        Update y:
        y <- y + z*F^T P Fs

        where:
        F: filter matrix
        P: Positive definite part of the constraint Hessian

        Note:
        x, s correspond raw design variable (unfiltered) and it's NOT equal to
        the design variable in the assembler:

        if:
        self.assembler.getDesignVars(dv)

        Then:
        dv == Fx

        Inputs:
            zero_idx (int list): indices to-be-zeroed in order to compute
                                 the Hessian-vector product for reduced problem
            s (PVec): unfiltered update step
            z (array-like): multipliers for dense constraints

        Outputs:
            y (PVec): y = y + z*F^T P Fs
        """
        # Timer
        t_start = MPI.Wtime()

        """[1] Compute svec <- F * s"""
        # Apply filter to s: svec = Fs
        self.svec.zeroEntries()
        self.fltr.applyFilter(TMR.convertPVecToVec(s), self.svec)

        """[1.5] Zero out entries in svec if called by a reduced problem"""
        if zero_idx:
            self.svec_vals[zero_idx] = 0.0

        """[2] Compute update <- P * svec by finite differencing"""
        # Finite difference step length for computing second order
        # derivative of stiffness matrix
        h = 1e-8

        # Save original rho for later use
        self.assembler.getDesignVars(self.rho_original)

        # Perturb rho <- rho + h*svec and set it to assembler
        self.rho.copyValues(self.rho_original)
        self.rho.axpy(h, self.svec)
        self.assembler.setDesignVars(self.rho)

        # Prepare the temp vector
        self.temp.zeroEntries()

        for i in range(self.num_eigenvalues):
            # Compute g(rho + h*s) for d2Kdx2
            coeff1 = self.eta[i] * self.eig_scale
            self.assembler.addMatDVSensInnerProduct(
                coeff1, TACS.STIFFNESS_MATRIX, self.eigv[i], self.eigv[i], self.temp
            )

            # Compute g(rho + h*s) for d2Mdx2
            coeff2 = -self.eta[i] * self.lambda0 * self.eig_scale
            self.assembler.addMatDVSensInnerProduct(
                coeff2, TACS.MASS_MATRIX, self.eigv[i], self.eigv[i], self.temp
            )

        # set density back to original
        self.assembler.setDesignVars(self.rho_original)

        for i in range(self.num_eigenvalues):
            # Compute g(rho + h*s) - g(rho) for d2Kdx2
            coeff1 = self.eta[i] * self.eig_scale
            self.assembler.addMatDVSensInnerProduct(
                -coeff1, TACS.STIFFNESS_MATRIX, self.eigv[i], self.eigv[i], self.temp
            )

            # Compute g(rho + h*s) - g(rho) for d2Mdx2
            coeff2 = -self.eta[i] * self.lambda0 * self.eig_scale
            self.assembler.addMatDVSensInnerProduct(
                -coeff2, TACS.MASS_MATRIX, self.eigv[i], self.eigv[i], self.temp
            )

        # Distribute the temp vector
        self.temp.beginSetValues(op=TACS.ADD_VALUES)
        self.temp.endSetValues(op=TACS.ADD_VALUES)

        # Prepare update vector
        self.update.zeroEntries()

        # Finish computing P * svec
        self.update.axpy(1.0 / h, self.temp)

        """[2.5] Zero out entries in update if called by a reduced problem"""
        if zero_idx:
            self.update_vals[zero_idx] = 0.0

        """[Debug print]"""
        curvature = self.svec.dot(self.update)
        rho_norm = self.rho.norm()
        s_norm = s.norm()
        y_norm = y.norm()
        dy_norm = self.update.norm()
        if self.comm.rank == 0:
            print("curvature: {:20.10e}".format(curvature))
            print("norm(rho):   {:20.10e}".format(rho_norm))
            print("norm(s):   {:20.10e}".format(s_norm))
            print("norm(y):   {:20.10e}".format(y_norm))
            print("norm(dy):  {:20.10e}".format(dy_norm))

        """[3] Compute y <- y + z * F^T * update"""
        # only perform such update when curvature condition is satisfied
        if curvature > 0:
            self.fltr.applyTranspose(self.update, self.update)  # update <- F*T update
            if zero_idx:
                self.update_vals[zero_idx] = 0.0
            y_wrap = TMR.convertPVecToVec(y)  # prepare y
            y_wrap.axpy(z[0], self.update)  # y <- y + z * update

        # Save curvature
        self.curvs.append(curvature)

        # Timer
        self.qn_time.append(MPI.Wtime() - t_start)
        return

    def getQnUpdateCurvs(self):
        return self.curvs

    def getAveragedQnTime(self):
        return np.average(self.qn_time)

    def computeNonDesignMat(self, indices, mscale, kscale, save_f5=False):
        """
        In this frequency constrained problem, we apply the frequency load
        by adding non-design block of material. This non-design block of
        material is modeled by M0 and K0, where
        M0 = M(dv[indices] = 1.0) * mscale
        K0 = K(dv[indices] = 1.0) * kscale
        """

        # Populate the non-design mass vector
        dv_one = self.assembler.createDesignVec()
        if indices:
            dv_one.getArray()[indices] = 1.0

        # Back up design variable
        dv_backup = self.assembler.createDesignVec()
        self.assembler.getDesignVars(dv_backup)

        # Assemble non-design mass and stiffness matrix
        self.m0mat = self.assembler.createMat()
        self.k0mat = self.assembler.createMat()
        self.assembler.setDesignVars(dv_one)
        self.assembler.assembleMatType(TACS.MASS_MATRIX, self.m0mat)
        self.assembler.assembleMatType(TACS.STIFFNESS_MATRIX, self.k0mat)
        self.m0mat.scale(mscale)
        self.k0mat.scale(kscale)

        # Save geometry to f5
        if save_f5:
            flag_m0 = (
                TACS.OUTPUT_CONNECTIVITY
                | TACS.OUTPUT_NODES
                | TACS.OUTPUT_DISPLACEMENTS
                | TACS.OUTPUT_EXTRAS
            )
            f5_m0 = TACS.ToFH5(self.assembler, TACS.SOLID_ELEMENT, flag_m0)
            f5_m0.writeToFile(os.path.join(self.prefix, "non_design_mass.f5"))

        # Set design variable back
        self.assembler.setDesignVars(dv_backup)

        return

    def addNonDesignMat(self, mmat, kmat):
        # Update mmat and apply boundary conditions
        mmat.axpy(1.0, self.m0mat)
        self.assembler.applyMatBCs(mmat)

        # Update kmat and apply boundary conditions
        kmat.axpy(1.0, self.k0mat)
        self.assembler.applyMatBCs(mmat)

        return


class MassObj:
    """
    Mass objective takes the following form:
        obj = m / m_fixed
    """

    def __init__(self, m_fixed, comm):
        self.m_fixed = m_fixed
        self.comm = comm
        self.rank = self.comm.Get_rank()

        self.assembler = None
        self.fltr = None
        self.mass_func = None

        return

    def objective(self, fltr, mg):
        if self.fltr is None:
            self.fltr = fltr
            self.assembler = self.fltr.getAssembler()
            self.mass_func = functions.StructuralMass(self.assembler)

        # Eval mass
        mass = self.assembler.evalFunctions([self.mass_func])[0]
        obj = mass / self.m_fixed
        if self.rank == 0:
            print("{:30s}{:20.10e}".format("[Obj] mass objective:", obj))

        return obj

    def objective_gradient(self, fltr, mg, dfdrho):
        # We only have one constraint
        dfdrho.zeroEntries()

        # Evaluate the mass gradient
        self.assembler.addDVSens([self.mass_func], [dfdrho], alpha=1.0 / self.m_fixed)

        # Compute norm
        norm = dfdrho.norm()
        if self.rank == 0:
            print("{:30s}{:20.10e}".format("[Con] gradient norm:", norm))
        return


def create_problem(
    prefix,
    domain,
    forest,
    bcs,
    props,
    nlevels,
    lambda0,
    ksrho,
    vol_frac=0.25,
    r0_frac=0.05,
    len0=1.0,
    AR=1.0,
    ratio=0.4,
    density=2600.0,
    iter_offset=0,
    eig_scale=1.0,
    num_eigenvalues=10,
    max_jd_size=100,
    max_gmres_size=30,
    mscale=10.0,
    kscale=1.0,
):
    """
    Create the TMRTopoProblem object and set up the topology optimization problem.

    This code is given the forest, boundary conditions, material properties and
    the number of multigrid levels. Based on this info, it creates the TMRTopoProblem
    and sets up the mass-constrained compliance minimization problem. Before
    the problem class is returned it is initialized so that it can be used for
    optimization.

    Args:
        forest (OctForest): Forest object
        bcs (BoundaryConditions): Boundary condition object
        props (StiffnessProperties): Material properties object
        nlevels (int): number of multigrid levels
        density (float): Density to use for the mass computation
        iter_offset (int): iteration counter offset

    Returns:
        TopoProblem: Topology optimization problem instance
    """

    # Create the problem and filter object
    N = 20
    mfilter = MFilterCreator(r0_frac, N, a=len0)
    filter_type = mfilter.filter_callback
    obj = CreatorCallback(bcs, props)
    problem = TopOptUtils.createTopoProblem(
        forest, obj.creator_callback, filter_type, use_galerkin=True, nlevels=nlevels
    )

    # Get the assembler object we just created
    assembler = problem.getAssembler()

    # Compute the fixed mass target
    lx = len0 * AR  # mm
    ly = len0  # mm
    lz = len0  # mm
    if domain == "lbracket":
        ly = len0 * ratio
    vol = lx * ly * lz
    if domain == "lbracket":
        S1 = lx * lz
        S2 = lx * lz * (1.0 - ratio) ** 2
        vol = (S1 - S2) * ly
    m_fixed = vol_frac * (vol * density)

    # Add objective callback
    obj_callback = MassObj(m_fixed, assembler.getMPIComm())
    problem.addObjectiveCallback(
        obj_callback.objective, obj_callback.objective_gradient
    )

    # Add constraint callback
    constr_callback = FrequencyConstr(
        prefix,
        domain,
        forest,
        len0,
        AR,
        ratio,
        iter_offset,
        lambda0,
        ksrho=ksrho,
        eig_scale=eig_scale,
        num_eigenvalues=num_eigenvalues,
        max_jd_size=max_jd_size,
        max_gmres_size=max_gmres_size,
        mscale=mscale,
        kscale=kscale,
    )
    problem.addConstraintCallback(
        1, 1, constr_callback.constraint, constr_callback.constraint_gradient
    )

    # Set output callback
    cb = OutputCallback(assembler, iter_offset=iter_offset)
    problem.setOutputCallback(cb.write_output)

    return problem, obj_callback, constr_callback


class ReducedProblem(ParOpt.Problem):
    """
    A reduced problem by fixing some design variables in the original problem
    """

    def __init__(
        self,
        original_prob,
        fixed_dv_idx: list,
        fixed_dv_val=1.0,
        qn_correction_func=None,
        ncon=1,
    ):
        self.prob = original_prob
        self.assembler = self.prob.getAssembler()
        self.comm = self.assembler.getMPIComm()
        self.ncon = ncon
        self.qn_correction_func = qn_correction_func

        # Allocate full-size vectors for the original problem
        self._x = self.prob.createDesignVec()
        self._g = self.prob.createDesignVec()
        self._A = [self.prob.createDesignVec() for _ in range(self.ncon)]

        # Allocate helper variables for the qn correction, if needed
        if self.qn_correction_func:
            self._s = self.prob.createDesignVec()
            self._y = self.prob.createDesignVec()

        # Get indices of fixed design variables, these indices
        # are with respect to the original full-sized problem
        self.fixed_dv_idx = fixed_dv_idx
        self.fixed_dv_val = fixed_dv_val

        # Compute the indices of fixed design variables, these indices
        # are with respect to the original full-sized problem
        self.free_dv_idx = [
            i for i in range(len(self._x)) if i not in self.fixed_dv_idx
        ]
        self.nvars = len(self.free_dv_idx)

        # Initial dv - can be set by calling setInitDesignVars()
        self.xinit = None

        self.num_obj_evals = 0
        self.save_snapshot_every = 1
        self.snapshot = {"iter": [], "obj": [], "infeas": [], "discreteness": []}

        super().__init__(self.comm, self.nvars, self.ncon)
        return

    def getNumCons(self):
        return self.ncon

    def setInitDesignVars(self, xinit):
        self.xinit = xinit
        return

    def getVarsAndBounds(self, x, lb, ub):
        # Set initial x
        if self.xinit is None:
            x[:] = 0.95
        else:
            x[:] = self.xinit[:]

        # Set bounds
        lb[:] = 1e-3
        ub[:] = 1.0
        return

    def evalObjCon(self, x):
        # Populate full-sized design variable
        self.reduDVtoDV(x, self._x)

        # Run analysis in full-sized problem
        fail, fobj, con = self.prob.evalObjCon(self.ncon, self._x)

        # Save a snapshot of the result
        if self.num_obj_evals % self.save_snapshot_every == 0:
            self.snapshot["iter"].append(self.num_obj_evals)
            self.snapshot["obj"].append(fobj)
            self.snapshot["infeas"].append(np.max([-con[0], 0]))  # hard-coded
            x_g = self.comm.allgather(np.array(x))
            x_g = np.concatenate(x_g)
            self.snapshot["discreteness"].append(np.dot(x_g, 1.0 - x_g) / len(x_g))

        self.num_obj_evals += 1

        return fail, fobj, con

    def evalObjConGradient(self, x, g, A):
        # Populate full-sized design variable
        self.reduDVtoDV(x, self._x)

        # Run analysis in full-sized problem to get gradient
        fail = self.prob.evalObjConGradient(self._x, self._g, self._A)

        # Get reduced gradient and constraint jacobian
        self.DVtoreduDV(self._g, g)
        for i in range(self.ncon):
            self.DVtoreduDV(self._A[i], A[i])

        return fail

    def reduDVtoDV(self, reduDV, DV, fixed_val=None):
        """
        Convert the reduced design vector to full-sized design vector
        """
        if fixed_val is None:
            val = self.fixed_dv_val
        else:
            val = fixed_val

        if self.fixed_dv_idx:
            DV[self.fixed_dv_idx] = val
        if self.free_dv_idx:
            DV[self.free_dv_idx] = reduDV[:]

        return

    def DVtoreduDV(self, DV, reduDV):
        """
        Convert the full-sized design vector to reduced design vector
        """
        if self.free_dv_idx:
            reduDV[:] = DV[self.free_dv_idx]

        return

    def computeQuasiNewtonUpdateCorrection(self, x, z, zw, s, y):
        if self.qn_correction_func:
            # Populate full-sized vectors
            self.reduDVtoDV(s, self._s, fixed_val=0.0)
            self.reduDVtoDV(y, self._y, fixed_val=0.0)

            # Update y and copy back
            self.qn_correction_func(self.fixed_dv_idx, z, self._s, self._y)
            self.DVtoreduDV(self._y, y)
        return

    def get_snapshot(self):
        return self.snapshot


def getFixedDVIndices(forest, domain, len0, AR, ratio):
    """
    Get indices for fixed design variables
    """

    fixed_dv_idx = []

    # Compute geometric parameters
    lx = len0 * AR
    ly = len0
    lz = len0
    if domain == "lbracket":
        ly = len0 * ratio

    # Get nodal locations
    Xpts = forest.getPoints()

    # Note: the local nodes are organized as follows:
    # |--- dependent nodes -- | ext_pre | -- owned local -- | - ext_post -|

    # Get number of local nodes in the current processor
    n_local_nodes = Xpts.shape[0]

    # Get numbder of dependent nodes
    _ptr, _conn, _weights = forest.getDepNodeConn()

    # Get number of ext_pre nodes
    n_ext_pre = forest.getExtPreOffset()

    # Get numbder of own nodes:
    offset = n_ext_pre

    # # Loop over all owned nodes and set non-design mass values
    tol = 1e-6  # Make sure our ranges are inclusive
    depth = 0.1  # depth for non-design mass
    if domain == "cantilever":
        xmin = (1 - depth) * lx - tol
        xmax = lx + tol
        ymin = 0.25 * ly - tol
        ymax = 0.75 * ly + tol
        zmin = 0.0 * lz - tol
        zmax = 0.2 * lz + tol

    elif domain == "michell":
        xmin = (1 - depth) * lx - tol
        xmax = lx + tol
        ymin = 0.25 * ly - tol
        ymax = 0.75 * ly + tol
        zmin = 0.4 * lz - tol
        zmax = 0.6 * lz + tol

    elif domain == "mbb":
        xmin = 0.0 * lx - tol
        xmax = 0.2 * lx + tol
        ymin = 0.25 * ly - tol
        ymax = 0.75 * ly + tol
        zmin = (1 - depth) * lz - tol
        zmax = lz + tol

    elif domain == "lbracket":
        xmin = (1 - depth) * lx - tol
        xmax = lx + tol
        ymin = 0.25 * ly - tol
        ymax = 0.75 * ly + tol
        zmin = 0.5 * ratio * lz - tol
        zmax = 1.0 * ratio * lz + tol

    else:
        raise ValueError("[Error]Unsupported domain type for non-design mass!")

    for i in range(offset, n_local_nodes):
        x, y, z = Xpts[i]
        if xmin < x < xmax:
            if ymin < y < ymax:
                if zmin < z < zmax:
                    fixed_dv_idx.append(i - offset)

    return fixed_dv_idx


class ReduOmAnalysis(om.ExplicitComponent):
    """
    This class wraps the analyses with openmdao interface such that
    the optimization can be run with different optimizers such as
    SNOPT and IPOPT.
    Note that the design/gradient vectors manipulated in this class
    are all global vectors. Local components can be queried by:

    local_vec = global_vec[start:end]

    where:
    start = self.offsets[rank]
    end = start + self.sizes[rank]
    """

    def __init__(self, comm, redu_prob, x0):
        """
        Args:
            comm (MPI communicator)
            problem (ParOpt.Problem)
            x0 (indexible array object)
        """

        super().__init__()

        # Copy over parameters
        self.comm = comm
        self.problem = redu_prob

        # Compute sizes and offsets
        local_size = len(x0)
        sizes = [0] * comm.size
        offsets = [0] * comm.size
        sizes = comm.allgather(local_size)
        if comm.size > 1:
            for i in range(1, comm.size):
                offsets[i] = offsets[i - 1] + sizes[i - 1]
        self.sizes = sizes
        self.offsets = offsets

        # Get number of constraints
        self.ncon = self.problem.getNumCons()

        # Compute some indices and dimensions
        self.local_size = self.sizes[self.comm.rank]
        self.global_size = np.sum(self.sizes)
        self.start = self.offsets[self.comm.rank]
        self.end = self.start + self.local_size

        # Allocate paropt vectors
        self.x = self.problem.createDesignVec()
        self.g = self.problem.createDesignVec()
        self.A = []
        for i in range(self.ncon):
            self.A.append(self.problem.createDesignVec())

        return

    def setup(self):
        self.add_input("x", shape=(self.global_size,))
        self.add_output("obj", shape=1)
        self.add_output("con", shape=1)

        self.declare_partials(of="obj", wrt="x")
        self.declare_partials(of="con", wrt="x")

        return

    def compute(self, inputs, outputs):
        # Broadcase x from root to all processor
        # In this way we only use optimization result from
        # root and implicitly discard results from any other
        # optimizer to prevent potential inconsistency
        if self.comm.rank == 0:
            x = inputs["x"]
        else:
            x = None
        x = self.comm.bcast(x, root=0)

        # Each processor only use its owned part to evaluate func and grad
        self.x[:] = x[self.start : self.end]
        fail, fobj, cons = self.problem.evalObjCon(self.x)

        if fail:
            raise RuntimeError("Failed to evaluate objective and constraints!")
        else:
            outputs["obj"] = fobj
            outputs["con"] = cons[0]

        # Barrier here because we don't do block communication
        self.comm.Barrier()

        return

    def compute_partials(self, inputs, partials):
        # Broadcase x from root to all processor
        # In this way we only use optimization result from
        # root and implicitly discard results from any other
        # optimizer to prevent potential inconsistency
        if self.comm.rank == 0:
            x = inputs["x"]
        else:
            x = None
        x = self.comm.bcast(x, root=0)

        # Each processor only use its owned part to evaluate func and grad
        self.x[:] = x[self.start : self.end]
        fail = self.problem.evalObjConGradient(self.x, self.g, self.A)

        if fail:
            raise RuntimeError("Failed to evaluate objective and constraints!")
        else:
            global_g = self.comm.allgather(np.array(self.g))
            global_g = np.concatenate(global_g)
            global_A = []
            for i in range(self.ncon):
                global_A.append(self.comm.allgather(np.array(self.A[i])))
                global_A[i] = np.concatenate(global_A[i])

            partials["obj", "x"] = global_g
            partials["con", "x"] = global_A
        return

    def globalVecToLocalvec(self, global_vec, local_vec):
        """
        Assign corresponding part of the global vector to local vector
        """
        local_vec[:] = global_vec[self.start : self.end]
        return


class GeneralEigSolver:
    """
    This class checks the actual smallest generalized eigenvalue with the
    Jacobi-Davidson method
    """

    def __init__(self, problem, N=10, max_jd_size=200, max_gmres_size=30):
        """
        Args:
            problem (TMR.TopoProblem): An instance that contains all components we need.
            N (int): number of eigenpairs sought
        """

        self.N = N

        # Get references from problem instance
        self.assembler = problem.getAssembler()
        self.filter = problem.getTopoFilter()
        self.mg = problem.getMg()

        # Allocate space for K and M matrix
        self.kmat = self.assembler.createMat()
        self.mmat = self.assembler.createMat()

        # Allocate space for the nodal density, eigenvalues and eigenvectors
        self.rho = self.assembler.createDesignVec()
        self.evals = np.zeros(N)
        self.evecs = [self.assembler.createVec() for _ in range(N)]

        # Create operator for the generalized eigenvalue problem
        self.oper = TACS.JDFrequencyOperator(
            self.assembler, self.kmat, self.mmat, self.mg.getMat(), self.mg
        )

        # Set up the JD solver
        self.jd = TACS.JacobiDavidson(self.oper, N, max_jd_size, max_gmres_size)
        self.jd.setTolerances(eig_rtol=1e-6, eig_atol=1e-8, rtol=1e-12, atol=1e-15)
        self.jd.setRecycle(self.N)

        # Temp vectors to check residual
        self.MatVec = self.assembler.createVec()
        self.temp = self.assembler.createVec()
        self.res = np.zeros(N)

        return

    def compute(
        self,
        x,
        add_non_design_mass=False,
        non_design_mass_indices=None,
        mscale=1.0,
        kscale=1.0,
    ):
        """
        Take in x, update the design variable in the assembler, assemble
        K, M and mg matrices, and solve the generalized eigenvalue problem.

        Args:
            x (PVec): the (unfiltered) design variable of the full (not reduced) problem
            add_non_design_mass (Bool): if set to True,  modify the generalized eigenvalue
                                        problem as follows:
                                        M <- M + M0, M0 = M(dv[indices] = 1.0) * mscale
                                        K <- K + K0, K0 = K(dv[indices] = 1.0) * kscale
        """

        # Update the assembler with x
        self.filter.applyFilter(TMR.convertPVecToVec(x), self.rho)
        self.assembler.setDesignVars(self.rho)

        # Update matrices and factor the multigrid preconditioner
        self.assembler.assembleMatType(TACS.STIFFNESS_MATRIX, self.kmat)
        self.assembler.assembleMatType(TACS.MASS_MATRIX, self.mmat)

        # Update M and K, if specified
        if add_non_design_mass:
            indices = non_design_mass_indices

            # Populate the non-design mass vector
            dv_one = self.assembler.createDesignVec()
            if indices:
                dv_one.getArray()[indices] = 1.0

            # Back up design variable
            dv_backup = self.assembler.createDesignVec()
            self.assembler.getDesignVars(dv_backup)

            # Assemble non-design mass and stiffness matrix
            m0mat = self.assembler.createMat()
            k0mat = self.assembler.createMat()
            self.assembler.setDesignVars(dv_one)
            self.assembler.assembleMatType(TACS.MASS_MATRIX, m0mat)
            self.assembler.assembleMatType(TACS.STIFFNESS_MATRIX, k0mat)
            m0mat.scale(mscale)
            k0mat.scale(kscale)

            # Set design variable back
            self.assembler.setDesignVars(dv_backup)

            # Update kmat
            self.kmat.axpy(1.0, k0mat)
            self.assembler.applyMatBCs(self.kmat)

            # Update mmat
            self.mmat.axpy(1.0, m0mat)
            self.assembler.applyMatBCs(self.mmat)

        self.mg.assembleMatType(TACS.STIFFNESS_MATRIX)
        self.mg.factor()

        # Solve and check success
        self.jd.solve(print_flag=True, print_level=1)
        assert self.jd.getNumConvergedEigenvalues() >= self.N

        # Extract eigenpairs
        for i in range(self.N):
            self.evals[i], err = self.jd.extractEigenvector(i, self.evecs[i])

        # Check residuals
        for i in range(self.N):
            self.kmat.mult(self.evecs[i], self.MatVec)  # Compute K*v
            self.temp.copyValues(self.MatVec)
            self.mmat.mult(self.evecs[i], self.MatVec)  # Compute M*v
            self.temp.axpy(-self.evals[i], self.MatVec)
            self.res[i] = self.temp.norm()

        return self.evals, self.evecs, self.res


def find_indices(
    forest,
    domain,
    len0,
    AR,
    ratio,
    xmin=0.0,
    ymin=0.0,
    zmin=0.0,
    xmax=None,
    ymax=None,
    zmax=None,
):
    """
    Return indices for the design vector such that:
    xmin, ymin, zmin <= x, y, z <= xmax, ymax, zmax
    """
    indices = []

    # Compute geometric parameters
    lx = len0 * AR
    ly = len0
    lz = len0
    if domain == "lbracket":
        ly = len0 * ratio

    # Set max to default
    if not xmax:
        xmax = lx
    if not ymax:
        ymax = ly
    if not zmax:
        zmax = lz

    # Get nodal locations
    Xpts = forest.getPoints()

    # Note: the local nodes are organized as follows:
    # |--- dependent nodes -- | ext_pre | -- owned local -- | - ext_post -|

    # Get number of local nodes in the current processor
    n_local_nodes = Xpts.shape[0]

    # Get number of ext_pre nodes
    n_ext_pre = forest.getExtPreOffset()

    # Get numbder of own nodes:
    offset = n_ext_pre

    # # Loop over all owned nodes and set non-design mass values
    for i in range(offset, n_local_nodes):
        x, y, z = Xpts[i]
        if xmin <= x <= xmax:
            if ymin <= y <= ymax:
                if zmin <= z <= zmax:
                    indices.append(i - offset)

    return indices


def test_beam_frequency(
    problem, x, prefix, add_non_design_mass, non_design_mass_indices, mscale, kscale
):
    """
    Set design variables as desired, compute the fundamental frequency,
    then generate f5 file
    """
    assembler = problem.getAssembler()

    ges = GeneralEigSolver(problem)
    evals, evecs, res = ges.compute(
        x,
        add_non_design_mass=add_non_design_mass,
        non_design_mass_indices=non_design_mass_indices,
        mscale=mscale,
        kscale=kscale,
    )
    assembler.setVariables(evecs[0])

    # comm = assembler.getMPIComm()
    # if comm.rank == 0:
    #     print("%4s%20s%20s" % ('No.', 'eigenvalue', 'residual'))
    #     for i, (e, r) in enumerate(zip(evals, res)):
    #         print("%4d%20.5e%20.5e" % (i, e, r))

    flag = (
        TACS.OUTPUT_CONNECTIVITY
        | TACS.OUTPUT_NODES
        | TACS.OUTPUT_DISPLACEMENTS
        | TACS.OUTPUT_EXTRAS
    )
    f5 = TACS.ToFH5(assembler, TACS.SOLID_ELEMENT, flag)
    f5.writeToFile(os.path.join(prefix, "test_beam.f5"))
    return
