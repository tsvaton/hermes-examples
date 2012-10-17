#define HERMES_REPORT_ALL
#define HERMES_REPORT_FILE "application.log"
#include "definitions.h"

//  This example solves the time-dependent Richard's equation using 
//  adaptive time integration (no dynamical meshes in space yet).
//  Many different time stepping methods can be used. The nonlinear 
//  solver in each time step is the Newton's method. 
//
//  PDE: C(h)dh/dt - div(K(h)grad(h)) - (dK/dh)*(dh/dy) = 0
//  where K(h) = K_S*exp(alpha*h)                          for h < 0,
//        K(h) = K_S                                       for h >= 0,
//        C(h) = alpha*(theta_s - theta_r)*exp(alpha*h)    for h < 0,
//        C(h) = alpha*(theta_s - theta_r)                 for h >= 0.
//
//  Domain: rectangle (0, 8) x (0, 6.5).
//  Units: length: cm
//         time: days
//
//  BC: Dirichlet, given by the initial condition.
//
//  The following parameters can be changed:

// Choose full domain or half domain.
//const char* mesh_file = "domain-full.mesh";
const char* mesh_file = "domain-half.mesh";

// Adaptive time stepping.
// Time step (in days).
double time_step = 0.3;                           
// If rel. temporal error is greater than this threshold, decrease time 
// step size and repeat time step.
const double time_tol_upper = 1.0;                
// If rel. temporal error is less than this threshold, increase time step
// but do not repeat time step (this might need further research).
const double time_tol_lower = 0.5;                
// Timestep decrease ratio after unsuccessful nonlinear solve.
double time_step_dec = 0.8;                       
// Timestep increase ratio after successful nonlinear solve.
double time_step_inc = 1.1;                       
// Computation will stop if time step drops below this value. 
double time_step_min = 1e-8; 			  
                       
// Elements orders and initial refinements.
// Initial polynomial degree of mesh elements.
const int P_INIT = 2;                             
// Number of initial uniform mesh refinements.
const int INIT_REF_NUM = 2;                       
// Number of initial mesh refinements towards the top edge.
const int INIT_REF_NUM_BDY_TOP = 1;               

// Matrix solver: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
// SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  


// Constitutive relations.
enum CONSTITUTIVE_RELATIONS {
    CONSTITUTIVE_GENUCHTEN,    // Van Genuchten.
    CONSTITUTIVE_GARDNER       // Gardner.
};
// Use van Genuchten's constitutive relations, or Gardner's.
CONSTITUTIVE_RELATIONS constitutive_relations_type = CONSTITUTIVE_GENUCHTEN;

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
ButcherTableType butcher_table_type = Implicit_SDIRK_CASH_3_23_embedded;

// Newton's method.
// Stopping criterion for Newton on fine mesh.
const double NEWTON_TOL = 1e-5;                   
// Maximum allowed number of Newton iterations.
int NEWTON_MAX_ITER = 10;                         

// Times.
// Start-up time for time-dependent Dirichlet boundary condition.
const double STARTUP_TIME = 5.0;                  
// Time interval length.
const double T_FINAL = 1000.0;                    
// Time interval of the top layer infiltration.
const double PULSE_END_TIME = 1000.0;             
// Global time variable.
double current_time = 0;                          

// Problem parameters.
// Initial pressure head.
double H_INIT = -15.0;                            
// Top constant pressure head -- an infiltration experiment.
double H_ELEVATION = 10.0;                        
double K_S_vals[4] = {350.2, 712.8, 1.68, 18.64}; 
double ALPHA_vals[4] = {0.01, 1.0, 0.01, 0.01};
double N_vals[4] = {2.5, 2.0, 1.23, 2.5};
double M_vals[4] = {0.864, 0.626, 0.187, 0.864};

double THETA_R_vals[4] = {0.064, 0.0, 0.089, 0.064};
double THETA_S_vals[4] = {0.14, 0.43, 0.43, 0.24};
double STORATIVITY_vals[4] = {0.1, 0.1, 0.1, 0.1};

// Precalculation of constitutive tables.
const int MATERIAL_COUNT = 4;
// 0 - constitutive functions are evaluated directly (slow performance).
// 1 - constitutive functions are linearly approximated on interval 
//     <TABLE_LIMIT; LOW_LIMIT> (very efficient CPU utilization less 
//     efficient memory consumption (depending on TABLE_PRECISION)).
// 2 - constitutive functions are aproximated by quintic splines.
const int CONSTITUTIVE_TABLE_METHOD = 2;
						  
/* Use only if CONSTITUTIVE_TABLE_METHOD == 2 */					  
// Number of intervals.        
const int NUM_OF_INTERVALS = 16;                                
// Low limits of intervals approximated by quintic splines.
double INTERVALS_4_APPROX[16] = 
      {-1.0, -2.0, -3.0, -4.0, -5.0, -8.0, -10.0, -12.0, 
      -15.0, -20.0, -30.0, -50.0, -75.0, -100.0,-300.0, -1000.0}; 
// This array contains for each integer of h function appropriate polynomial ID.
                      
// First DIM is the interval ID, second DIM is the material ID, 
// third DIM is the derivative degree, fourth DIM are the coefficients.

/* END OF Use only if CONSTITUTIVE_TABLE_METHOD == 2 */					  

/* Use only if CONSTITUTIVE_TABLE_METHOD == 1 */
// Limit of precalculated functions (should be always negative value lower 
// then the lowest expect value of the solution (consider DMP!!)
double TABLE_LIMIT = -1000.0; 		          
// Precision of precalculated table use 1.0, 0,1, 0.01, etc.....
const double TABLE_PRECISION = 0.1;               

bool CONSTITUTIVE_TABLES_READY = false;
// Polynomial approximation of the K(h) function close to saturation.
// This function has singularity in its second derivative.
// First dimension is material ID
// Second dimension is the polynomial derivative.
// Third dimension are the polynomial's coefficients.
double*** POLYNOMIALS;                            	  
// Lower bound of K(h) function approximated by polynomials.						  
const double LOW_LIMIT = -1.0;                    
const int NUM_OF_INSIDE_PTS = 0;
/* END OF Use only if CONSTITUTIVE_TABLE_METHOD == 1 */

bool POLYNOMIALS_READY = false;
bool POLYNOMIALS_ALLOCATED = false;

// Global variables for forms.
double K_S, ALPHA, THETA_R, THETA_S, N, M, STORATIVITY;

// Main function.
int main(int argc, char* argv[])
{
  ConstitutiveRelationsGenuchtenWithLayer constitutive_relations(CONSTITUTIVE_TABLE_METHOD, NUM_OF_INSIDE_PTS, LOW_LIMIT, TABLE_PRECISION, TABLE_LIMIT, K_S_vals, ALPHA_vals, N_vals, M_vals, THETA_R_vals, THETA_S_vals, STORATIVITY_vals);

  // Either use exact constitutive relations (slow) (method 0) or precalculate 
  // their linear approximations (faster) (method 1) or
  // precalculate their quintic polynomial approximations (method 2) -- managed by 
  // the following loop "Initializing polynomial approximation".
  if (CONSTITUTIVE_TABLE_METHOD == 1)
    constitutive_relations.constitutive_tables_ready = get_constitutive_tables(1, &constitutive_relations, MATERIAL_COUNT);  // 1 stands for the Newton's method.
  

  // The van Genuchten + Mualem K(h) function is approximated by polynomials close 
  // to zero in case of CONSTITUTIVE_TABLE_METHOD==1.
  // In case of CONSTITUTIVE_TABLE_METHOD==2, all constitutive functions are approximated by polynomials.
  Hermes::Mixins::Loggable::static_info("Initializing polynomial approximations.");
  for (int i=0; i < MATERIAL_COUNT; i++)
  {
    // Points to be used for polynomial approximation of K(h).
    double* points = new double[NUM_OF_INSIDE_PTS];

    init_polynomials(6 + NUM_OF_INSIDE_PTS, LOW_LIMIT, points, NUM_OF_INSIDE_PTS, i, &constitutive_relations, MATERIAL_COUNT, NUM_OF_INTERVALS, INTERVALS_4_APPROX);
  }
  
  constitutive_relations.polynomials_ready = true;
  if (CONSTITUTIVE_TABLE_METHOD == 2)
  {
    constitutive_relations.constitutive_tables_ready = true;
    //Assign table limit to global definition.
    constitutive_relations.table_limit = INTERVALS_4_APPROX[NUM_OF_INTERVALS-1];
  }
  
  // Choose a Butcher's table or define your own.
  ButcherTable bt(butcher_table_type);
  if (bt.is_explicit()) Hermes::Mixins::Loggable::static_info("Using a %d-stage explicit R-K method.", bt.get_size());
  if (bt.is_diagonally_implicit()) Hermes::Mixins::Loggable::static_info("Using a %d-stage diagonally implicit R-K method.", bt.get_size());
  if (bt.is_fully_implicit()) Hermes::Mixins::Loggable::static_info("Using a %d-stage fully implicit R-K method.", bt.get_size());

  // Load the mesh.
  Mesh mesh, basemesh;
  MeshReaderH2D mloader;
  mloader.load(mesh_file, &basemesh);
  
  // Perform initial mesh refinements.
  mesh.copy(&basemesh);
  for(int i = 0; i < INIT_REF_NUM; i++) mesh.refine_all_elements();
  mesh.refine_towards_boundary("Top", INIT_REF_NUM_BDY_TOP);

  // Initialize boundary conditions.
  RichardsEssentialBC bc_essential("Top", H_ELEVATION, PULSE_END_TIME, H_INIT, STARTUP_TIME);
  EssentialBCs<double> bcs(&bc_essential);

  // Create an H1 space with default shapeset.
  H1Space<double> space(&mesh, &bcs, P_INIT);
  int ndof = space.get_num_dofs();
  Hermes::Mixins::Loggable::static_info("ndof = %d.", ndof);

  // Convert initial condition into a Solution.
  ZeroSolution<double> h_time_prev(&mesh), h_time_new(&mesh), time_error_fn(&mesh);

  // Initialize views.
  ScalarView view("Initial condition", new WinGeom(0, 0, 600, 500));
  view.fix_scale_width(80);

  // Visualize the initial condition.
  view.show(&h_time_prev);

  // Initialize the weak formulation.
  CustomWeakFormRichardsRK wf(&constitutive_relations);

   // Visualize the projection and mesh.
  ScalarView sview("Initial condition", new WinGeom(0, 0, 400, 350));
  sview.fix_scale_width(50);
  sview.show(&h_time_prev);
  ScalarView eview("Temporal error", new WinGeom(405, 0, 400, 350));
  eview.fix_scale_width(50);
  eview.show(&time_error_fn);
  OrderView oview("Initial mesh", new WinGeom(810, 0, 350, 350));
  oview.show(&space);

  // Graph for time step history.
  SimpleGraph time_step_graph;
  Hermes::Mixins::Loggable::static_info("Time step history will be saved to file time_step_history.dat.");

  // Initialize Runge-Kutta time stepping.
  RungeKutta<double> runge_kutta(&wf, &space, &bt);

  // Time stepping:
  double current_time = 0;
  int ts = 1;
  do 
  {
    Hermes::Mixins::Loggable::static_info("---- Time step %d, time %3.5f s", ts, current_time);

    Space<double>::update_essential_bc_values(&space, current_time);

    // Perform one Runge-Kutta time step according to the selected Butcher's table.
    Hermes::Mixins::Loggable::static_info("Runge-Kutta time step (t = %g s, time step = %g s, stages: %d).", 
         current_time, time_step, bt.get_size());
    try
    {
      runge_kutta.setTime(current_time);
      runge_kutta.setTimeStep(time_step);
      runge_kutta.set_newton_max_iter(NEWTON_MAX_ITER);
      runge_kutta.set_newton_tol(NEWTON_TOL);
      runge_kutta.rk_time_step_newton(&h_time_prev, 
          &h_time_new, &time_error_fn);
    }
    catch(Exceptions::Exception& e)
    {
      Hermes::Mixins::Loggable::static_info("Runge-Kutta time step failed, decreasing time step size from %g to %g days.", 
           time_step, time_step * time_step_dec);
      time_step *= time_step_dec;
      if (time_step < time_step_min) 
        throw Hermes::Exceptions::Exception("Time step became too small.");
      continue;
    }
    
    // Copy solution for the new time step.
    h_time_prev.copy(&h_time_new);

    // Show error function.
    char title[100];
    sprintf(title, "Temporal error, t = %g", current_time);
    eview.set_title(title);
    eview.show(&time_error_fn);

    // Calculate relative time stepping error and decide whether the 
    // time step can be accepted. If not, then the time step size is 
    // reduced and the entire time step repeated. If yes, then another
    // check is run, and if the relative error is very low, time step 
    // is increased.
    double rel_err_time = Global<double>::calc_norm(&time_error_fn, HERMES_H1_NORM) / Global<double>::calc_norm(&h_time_new, HERMES_H1_NORM) * 100;
    Hermes::Mixins::Loggable::static_info("rel_err_time = %g%%", rel_err_time);
    if (rel_err_time > time_tol_upper) {
      Hermes::Mixins::Loggable::static_info("rel_err_time above upper limit %g%% -> decreasing time step from %g to %g days and repeating time step.", 
           time_tol_upper, time_step, time_step * time_step_dec);
      time_step *= time_step_dec;
      continue;
    }
    if (rel_err_time < time_tol_lower) {
      Hermes::Mixins::Loggable::static_info("rel_err_time = below lower limit %g%% -> increasing time step from %g to %g days", 
           time_tol_lower, time_step, time_step * time_step_inc);
      time_step *= time_step_inc;
    }

    // Add entry to the timestep graph.
    time_step_graph.add_values(current_time, time_step);
    time_step_graph.save("time_step_history.dat");

    // Update time.
    current_time += time_step;

    // Show the new time level solution.
    sprintf(title, "Solution, t = %g", current_time);
    sview.set_title(title);
    sview.show(&h_time_new);
    oview.show(&space);

    // Save complete Solution.
    char filename[100];
    sprintf(filename, "outputs/tsln_%f.dat", current_time);
    h_time_new.save(filename);
    Hermes::Mixins::Loggable::static_info("Solution at time %g saved to file %s.", current_time, filename);

    // Save solution for the next time step.
    h_time_prev.copy(&h_time_new);

    // Increase time step counter.
    ts++;
  } 
  while (current_time < T_FINAL);

  // Wait for the view to be closed.
  View::wait();
  return 0;
}

