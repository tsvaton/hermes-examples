#define HERMES_REPORT_ALL
#define HERMES_REPORT_FILE "application.log"
#include "definitions.h"

//  This problem describes the distribution of the vector potential in
//  a 2D domain comprising a wire carrying electrical current, air, and
//  an iron which is not under voltage.
//
//  PDE: -div(1/rho grad p) - omega**2 / (rho c**2) * p = 0.
//
//  Domain: Axisymmetric geometry of a horn, see mesh file domain.mesh.
//
//  BC: Prescribed pressure on the bottom edge,
//      zero Neumann on the walls and on the axis of symmetry,
//      Newton matched boundary at outlet 1/rho dp/dn = j omega p / (rho c)
//
//  The following parameters can be changed:

// Number of initial uniform mesh refinements.
const int INIT_REF_NUM = 0;                       
// Initial polynomial degree of mesh elements.
const int P_INIT = 2;                             
// This is a quantitative parameter of the adapt(...) function and
// it has different meanings for various adaptive strategies.
const double THRESHOLD = 0.3;                     
// Adaptive strategy:
// STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
//   error is processed. If more elements have similar errors, refine
//   all to keep the mesh symmetric.
// STRATEGY = 1 ... refine all elements whose error is larger
//   than THRESHOLD times maximum element error.
// STRATEGY = 2 ... refine all elements whose error is larger
//   than THRESHOLD.
const int STRATEGY = 0;                           
// Predefined list of element refinement candidates. Possible values are
// H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
// H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
const CandList CAND_LIST = H2D_HP_ANISO;          
// Maximum allowed level of hanging nodes:
// MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
// MESH_REGULARITY = 1 ... at most one-level hanging nodes,
// MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
// Note that regular meshes are not supported, this is due to
// their notoriously bad performance.
const int MESH_REGULARITY = -1;                   
// This parameter influences the selection of
// candidates in hp-adaptivity. Default value is 1.0. 
const double CONV_EXP = 1.0;                      
// Stopping criterion for adaptivity.
const double ERR_STOP = 1.0;                      
// Adaptivity process stops when the number of degrees of freedom grows
// over this limit. This is to prevent h-adaptivity to go on forever.
const int NDOF_STOP = 60000;                      
// Name of the iterative method employed by AztecOO (ignored by the other solvers).
// Possibilities: gmres, cg, cgs, tfqmr, bicgstab.
const char* iterative_method = "bicgstab";        
// Name of the preconditioner employed by AztecOO (ignored by the other solvers).
// Possibilities: none, jacobi, neumann, least-squares, or a
//  preconditioner from IFPACK (see solver/aztecoo.h)
const char* preconditioner = "least-squares";     
// Matrix solver: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
// SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  

// Problem parameters.
const double RHO = 1.25;
const double FREQ = 5e3;
const double OMEGA = 2 * M_PI * FREQ;
const double SOUND_SPEED = 353.0;
const std::complex<double>  P_SOURCE(1.0, 0.0);

int main(int argc, char* argv[])
{
  // Time measurement.
  Hermes::Mixins::TimeMeasurable cpu_time;
  cpu_time.tick();

  // Load the mesh.
  Mesh mesh;
  MeshReaderH2D mloader;
  mloader.load("domain.mesh", &mesh);

  //MeshView mv("Initial mesh", new WinGeom(0, 0, 400, 400));
  //mv.show(&mesh);
  //View::wait(HERMES_WAIT_KEYPRESS);

  // Perform initial mesh refinements.
  for (int i = 0; i < INIT_REF_NUM; i++) mesh.refine_all_elements();

  // Initialize boundary conditions.
  DefaultEssentialBCConst<std::complex<double> > bc_essential("Source", P_SOURCE);
  EssentialBCs<std::complex<double> > bcs(&bc_essential);

  // Create an H1 space with default shapeset.
  H1Space<std::complex<double> > space(&mesh, &bcs, P_INIT);
  int ndof = Space<std::complex<double> >::get_num_dofs(&space);
  Hermes::Mixins::Loggable::static_info("ndof = %d", ndof);

  // Initialize the weak formulation.
  CustomWeakFormAcoustics wf("Outlet", RHO, SOUND_SPEED, OMEGA);

  // Initialize coarse and reference mesh solution.
  Solution<std::complex<double> > sln, ref_sln;

  // Initialize refinement selector.
  H1ProjBasedSelector<std::complex<double> > selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Initialize views.
  ScalarView sview_real("Solution - real part", new WinGeom(0, 0, 330, 350));
  ScalarView sview_imag("Solution - imaginary part", new WinGeom(400, 0, 330, 350));
  sview_real.show_mesh(false);
  sview_real.fix_scale_width(50);
  sview_imag.show_mesh(false);
  sview_imag.fix_scale_width(50);
  OrderView  oview("Polynomial orders", new WinGeom(400, 0, 300, 350));

  // DOF and CPU convergence graphs initialization.
  SimpleGraph graph_dof, graph_cpu;

  // Adaptivity loop:
  int as = 1;
  bool done = false;
  do
  {
    Hermes::Mixins::Loggable::static_info("---- Adaptivity step %d:", as);

    // Construct globally refined reference mesh and setup reference space.
    Space<std::complex<double> >* ref_space = Space<std::complex<double> >::construct_refined_space(&space);
    int ndof_ref = Space<std::complex<double> >::get_num_dofs(ref_space);

    // Assemble the reference problem.
    Hermes::Mixins::Loggable::static_info("Solving on reference mesh.");
    DiscreteProblem<std::complex<double> > dp(&wf, ref_space);

    // Time measurement.
    cpu_time.tick();

    // Perform Newton's iteration.
    Hermes::Hermes2D::NewtonSolver<std::complex<double> > newton(&dp);
    try
    {
      newton.solve();
    }
    catch(Hermes::Exceptions::Exception e)
    {
      e.printMsg();
      throw Hermes::Exceptions::Exception("Newton's iteration failed.");
    };
    // Translate the resulting coefficient vector into the Solution<std::complex<double> > sln.
    Hermes::Hermes2D::Solution<std::complex<double> >::vector_to_solution(newton.get_sln_vector(), ref_space, &ref_sln);

    // Project the fine mesh solution onto the coarse mesh.
    Hermes::Mixins::Loggable::static_info("Projecting reference solution on coarse mesh.");
    OGProjection<std::complex<double> > ogProjection; ogProjection.project_global(&space, &ref_sln, &sln);

    // Time measurement.
    cpu_time.tick();

    // View the coarse mesh solution and polynomial orders.
    RealFilter mag(&ref_sln);
    sview_real.show(&mag);
    oview.show(&space);

    // Calculate element errors and total error estimate.
    Hermes::Mixins::Loggable::static_info("Calculating error estimate.");
    Adapt<std::complex<double> >* adaptivity = new Adapt<std::complex<double> >(&space);
    double err_est_rel = adaptivity->calc_err_est(&sln, &ref_sln) * 100;

    // Report results.
    Hermes::Mixins::Loggable::static_info("ndof_coarse: %d, ndof_fine: %d, err_est_rel: %g%%",
      Space<std::complex<double> >::get_num_dofs(&space), Space<std::complex<double> >::get_num_dofs(ref_space), err_est_rel);

    // Time measurement.
    cpu_time.tick();

    // Add entry to DOF and CPU convergence graphs.
    graph_dof.add_values(Space<std::complex<double> >::get_num_dofs(&space), err_est_rel);
    graph_dof.save("conv_dof_est.dat");
    graph_cpu.add_values(cpu_time.accumulated(), err_est_rel);
    graph_cpu.save("conv_cpu_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est_rel < ERR_STOP) done = true;
    else
    {
      Hermes::Mixins::Loggable::static_info("Adapting coarse mesh.");
      done = adaptivity->adapt(&selector, THRESHOLD, STRATEGY, MESH_REGULARITY);
    }
    if (Space<std::complex<double> >::get_num_dofs(&space) >= NDOF_STOP) done = true;

    delete adaptivity;
    if (done == false)
      delete ref_space->get_mesh();
    delete ref_space;

    // Increase counter.
    as++;
  }
  while (done == false);

  Hermes::Mixins::Loggable::static_info("Total running time: %g s", cpu_time.accumulated());

  // Show the reference solution - the final result.
  RealFilter ref_mag(&ref_sln);
  sview_real.show(&ref_mag);
  oview.show(&space);

  // Output solution in VTK format.
  Linearizer lin;
  bool mode_3D = true;
  lin.save_solution_vtk(&ref_mag, "sln.vtk", "Acoustic pressure", mode_3D);
  Hermes::Mixins::Loggable::static_info("Solution in VTK format saved to file %s.", "sln.vtk");

  // Wait for all views to be closed.
  View::wait();
  return 0;
}
