#define HERMES_REPORT_ALL
#define HERMES_REPORT_FILE "application.log"
#include "definitions.h"

// This example shows how to discretize the first-order time-domain Maxwell's equations 
// with vector-valued E (an Hcurl function) and double B (an H1 function). Time integration 
// is done using an arbitrary R-K method (see below).
//
// PDE: \partial E / \partial t - SPEED_OF_LIGHT**2 * curl B = 0,
//      \partial B / \partial t + curl E = 0.
//
// Note: curl E = \partial E_2 / \partial x - \partial E_1 / \partial y
//       curl B = (\partial B / \partial y, - \partial B / \partial x)
//       
// Domain: square (-pi/2, pi/2)^2.
//
// BC: perfect conductor for E on the entire boundary, no BC for B.
//
// The following parameters can be changed:

// Initial polynomial degree of mesh elements.
const int P_INIT = 6;                              
// Number of initial uniform mesh refinements.
const int INIT_REF_NUM = 1;                        
// Time step.
const double time_step = 0.05;                     
// Final time.
const double T_FINAL = 35.0;                       
// Matrix solver: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
// SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;   

// Choose one of the following time-integration methods, or define your own Butcher's table. The last number 
// in the name of each method is its order. The one before last, if present, is the number of stages.
// Explicit methods:
//   Explicit_RK_1, Explicit_RK_2, Explicit_RK_3, Explicit_RK_4.
// Implicit methods: 
//   Implicit_RK_1, Implicit_Crank_Nicolson_2_2, Implicit_SIRK_2_2, Implicit_ESIRK_2_2, Implicit_SDIRK_2_2, 
//   Implicit_Lobatto_IIIA_2_2, Implicit_Lobatto_IIIB_2_2, Implicit_Lobatto_IIIC_2_2, Implicit_Lobatto_IIIA_3_4, 
//   Implicit_Lobatto_IIIB_3_4, Implicit_Lobatto_IIIC_3_4, Implicit_Radau_IIA_3_5, Implicit_SDIRK_5_4.
// Embedded explicit methods:
//   Explicit_HEUN_EULER_2_12_embedded, Explicit_BOGACKI_SHAMPINE_4_23_embedded, Explicit_FEHLBERG_6_45_embedded,
//   Explicit_CASH_KARP_6_45_embedded, Explicit_DORMAND_PRINCE_7_45_embedded.
// Embedded implicit methods:
//   Implicit_SDIRK_CASH_3_23_embedded, Implicit_ESDIRK_TRBDF2_3_23_embedded, Implicit_ESDIRK_TRX2_3_23_embedded, 
//   Implicit_SDIRK_BILLINGTON_3_23_embedded, Implicit_SDIRK_CASH_5_24_embedded, Implicit_SDIRK_CASH_5_34_embedded, 
//   Implicit_DIRK_ISMAIL_7_45_embedded. 
ButcherTableType butcher_table_type = Implicit_RK_1;

// Problem parameters.
// Square of wave speed.        
const double C_SQUARED = 1;                                   

int main(int argc, char* argv[])
{
  // Choose a Butcher's table or define your own.
  ButcherTable bt(butcher_table_type);
  if (bt.is_explicit()) Hermes::Mixins::Loggable::Static::info("Using a %d-stage explicit R-K method.", bt.get_size());
  if (bt.is_diagonally_implicit()) Hermes::Mixins::Loggable::Static::info("Using a %d-stage diagonally implicit R-K method.", bt.get_size());
  if (bt.is_fully_implicit()) Hermes::Mixins::Loggable::Static::info("Using a %d-stage fully implicit R-K method.", bt.get_size());

  // Load the mesh.
  Mesh mesh;
  MeshReaderH2D mloader;
  mloader.load("domain.mesh", &mesh);

  // Perform initial mesh refinemets.
  for (int i = 0; i < INIT_REF_NUM; i++) mesh.refine_all_elements();

  // Initialize solutions.
  CustomInitialConditionWave E_sln(&mesh);
  ZeroSolution<double> B_sln(&mesh);
  Hermes::vector<Solution<double>*> slns(&E_sln, &B_sln);

  // Initialize the weak formulation.
  CustomWeakFormWave wf(C_SQUARED);
  
  // Initialize boundary conditions
  DefaultEssentialBCConst<double> bc_essential("Perfect conductor", 0.0);
  EssentialBCs<double> bcs_E(&bc_essential);
  EssentialBCs<double> bcs_B;

  // Create x- and y- displacement space using the default H1 shapeset.
  HcurlSpace<double> E_space(&mesh, &bcs_E, P_INIT);
  H1Space<double> B_space(&mesh, &bcs_B, P_INIT);
  Hermes::vector<const Space<double> *> spaces(&E_space, &B_space);
  Hermes::vector<Space<double> *> spaces_mutable(&E_space, &B_space);
  Hermes::Mixins::Loggable::Static::info("ndof = %d.", Space<double>::get_num_dofs(spaces));

  // Initialize views.
  ScalarView E1_view("Solution E1", new WinGeom(0, 0, 400, 350));
  E1_view.fix_scale_width(50);
  ScalarView E2_view("Solution E2", new WinGeom(410, 0, 400, 350));
  E2_view.fix_scale_width(50);
  ScalarView B_view("Solution B", new WinGeom(0, 405, 400, 350));
  B_view.fix_scale_width(50);

  // Initialize Runge-Kutta time stepping.
  RungeKutta<double> runge_kutta(&wf, spaces, &bt);

  // Time stepping loop.
  double current_time = time_step; int ts = 1;
  do
  {
    // Perform one Runge-Kutta time step according to the selected Butcher's table.
    Hermes::Mixins::Loggable::Static::info("Runge-Kutta time step (t = %g s, time_step = %g s, stages: %d).", 
         current_time, time_step, bt.get_size());
    bool jacobian_changed = false;
    bool verbose = true;
    
    try
    {
      runge_kutta.set_time(current_time);
      runge_kutta.set_time_step(time_step);
      runge_kutta.rk_time_step_newton(slns, slns);
    }
    catch(Exceptions::Exception& e)
    {
      e.print_msg();
      throw Hermes::Exceptions::Exception("Runge-Kutta time step failed");
    }

    // Visualize the solutions.
    char title[100];
    sprintf(title, "E1, t = %g", current_time);
    E1_view.set_title(title);
    E1_view.show(&E_sln, HERMES_EPS_NORMAL, H2D_FN_VAL_0);
    sprintf(title, "E2, t = %g", current_time);
    E2_view.set_title(title);
    E2_view.show(&E_sln, HERMES_EPS_NORMAL, H2D_FN_VAL_1);
    sprintf(title, "B, t = %g", current_time);
    B_view.set_title(title);
    B_view.show(&B_sln, HERMES_EPS_NORMAL, H2D_FN_VAL_0);

    // Update time.
    current_time += time_step;
  
  } while (current_time < T_FINAL);

  // Wait for the view to be closed.
  View::wait();

  return 0;
}
