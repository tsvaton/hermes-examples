#define HERMES_REPORT_INFO
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace Hermes;
using namespace Hermes::Hermes2D;
using namespace Hermes::Hermes2D::Views;

// This example solves the compressible Euler equations using a basic
// piecewise-constant finite volume method, or Discontinuous Galerkin 
// method of higher order with no adaptivity.
//
// Equations: Compressible Euler equations, perfect gas state equation.
//
// Domain: GAMM channel, see mesh file GAMM-channel.mesh
//
// BC: Solid walls, inlet, no outlet.
//
// IC: Constant state identical to inlet.
//
// The following parameters can be changed:

// Visualization.
// Set to "true" to enable Hermes OpenGL visualization. 
const bool HERMES_VISUALIZATION = true;
// Set to "true" to enable VTK output.
const bool VTK_VISUALIZATION = false;
// Set visual output for every nth step.
const unsigned int EVERY_NTH_STEP = 1;

// Shock capturing.
enum shockCapturingType
{
  FEISTAUER,
  KUZMIN,
  KRIVODONOVA
};
bool SHOCK_CAPTURING = true;
shockCapturingType SHOCK_CAPTURING_TYPE = FEISTAUER;
// Quantitative parameter of the discontinuity detector in case of Krivodonova.
double DISCONTINUITY_DETECTOR_PARAM = 1.0;
// Quantitative parameter of the shock capturing in case of Feistauer.
const double NU_1 = 0.1;
const double NU_2 = 0.1;

// For saving/loading of solution.
bool REUSE_SOLUTION = false;

// Initial polynomial degree.
const int P_INIT = 1;                                                      
// Number of initial uniform mesh refinements.    
const int INIT_REF_NUM = 2;                                                
// CFL value.
double CFL_NUMBER = 1.0;                                
// Initial time step.
double time_step_n = 1E-6;                                
// Initial time step.
double time_step_n_minus_one = 1E-6;                                

// Matrix solver for orthogonal projections: 
// SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
// SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  

// Equation parameters.
// Exterior pressure (dimensionless).
const double P_EXT = 2.5;         
// Inlet density (dimensionless).   
const double RHO_EXT = 1.0;       
// Inlet x-velocity (dimensionless).
const double V1_EXT = 1.25;       
// Inlet y-velocity (dimensionless).
const double V2_EXT = 0.0;        
// Kappa.
const double KAPPA = 1.4;         

// Boundary markers.
const std::string BDY_INLET = "1";
const std::string BDY_OUTLET = "2";
const std::string BDY_SOLID_WALL_BOTTOM = "3";
const std::string BDY_SOLID_WALL_TOP = "4";

// Weak forms.
#include "../forms_explicit.cpp"

// Initial condition.
#include "../initial_condition.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  MeshReaderH2D mloader;
  mloader.load("GAMM-channel.mesh", &mesh);

  // Perform initial mesh refinements.
  for (int i = 0; i < INIT_REF_NUM; i++) 
    mesh.refine_all_elements(0, true);

  // Initialize boundary condition types and spaces with default shapesets.
  L2Space<double> space_rho(&mesh, P_INIT);
  L2Space<double> space_rho_v_x(&mesh, P_INIT);
  L2Space<double> space_rho_v_y(&mesh, P_INIT);
  L2Space<double> space_e(&mesh, P_INIT);
  L2Space<double> space_stabilization(&mesh, 0);
  int ndof = Space<double>::get_num_dofs(Hermes::vector<const Space<double>*>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e));
  Hermes::Mixins::Loggable::static_info("ndof: %d", ndof);

  // Initialize solutions, set initial conditions.
  ConstantSolution<double> prev_rho(&mesh, RHO_EXT);
  ConstantSolution<double> prev_rho_v_x(&mesh, RHO_EXT * V1_EXT);
  ConstantSolution<double> prev_rho_v_y(&mesh, RHO_EXT * V2_EXT);
  ConstantSolution<double> prev_e(&mesh, QuantityCalculator::calc_energy(RHO_EXT, RHO_EXT * V1_EXT, RHO_EXT * V2_EXT, P_EXT, KAPPA));

  // Filters for visualization of Mach number, pressure and entropy.
  MachNumberFilter Mach_number(Hermes::vector<MeshFunction<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), KAPPA);
  PressureFilter pressure(Hermes::vector<MeshFunction<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), KAPPA);

  ScalarView pressure_view("Pressure", new WinGeom(0, 0, 600, 300));
  ScalarView Mach_number_view("Mach number", new WinGeom(700, 0, 600, 300));
  ScalarView s1("prev_rho", new WinGeom(0, 0, 600, 300));
  ScalarView s2("prev_rho_v_x", new WinGeom(700, 0, 600, 300));
  ScalarView s3("prev_rho_v_y", new WinGeom(0, 400, 600, 300));
  ScalarView s4("prev_e", new WinGeom(700, 400, 600, 300));

  // Set up the solver, matrix, and rhs according to the solver selection.
  SparseMatrix<double>* matrix = create_matrix<double>();
  Vector<double>* rhs = create_vector<double>();
  Vector<double>* rhs_stabilization = create_vector<double>();
  LinearMatrixSolver<double>* solver = create_linear_solver<double>( matrix, rhs);

  // Set up CFL calculation class.
  CFLCalculation CFL(CFL_NUMBER, KAPPA);

  // Look for a saved solution on the disk.
  CalculationContinuity<double> continuity(CalculationContinuity<double>::onlyTime);
  int iteration = 0; double t = 0;

  if(REUSE_SOLUTION && continuity.have_record_available())
  {
    continuity.get_last_record()->load_mesh(&mesh);
    continuity.get_last_record()->load_spaces(Hermes::vector<Space<double> *>(&space_rho, &space_rho_v_x, 
      &space_rho_v_y, &space_e), Hermes::vector<SpaceType>(HERMES_L2_SPACE, HERMES_L2_SPACE, HERMES_L2_SPACE, HERMES_L2_SPACE), Hermes::vector<Mesh *>(&mesh, &mesh, 
      &mesh, &mesh));
    continuity.get_last_record()->load_solutions(Hermes::vector<Solution<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), Hermes::vector<Space<double> *>(&space_rho, &space_rho_v_x, 
      &space_rho_v_y, &space_e));
    continuity.get_last_record()->load_time_step_length(time_step_n);
    continuity.get_last_record()->load_time_step_length_n_minus_one(time_step_n_minus_one);
    t = continuity.get_last_record()->get_time();
    iteration = continuity.get_num();
  }

  // Initialize weak formulation.
  EulerEquationsWeakFormSemiImplicit wf(KAPPA, RHO_EXT, V1_EXT, V2_EXT, P_EXT, BDY_SOLID_WALL_BOTTOM, BDY_SOLID_WALL_TOP, 
    BDY_INLET, BDY_OUTLET, &prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e, (P_INIT == 0));

  EulerEquationsWeakFormStabilization wf_stabilization(&prev_rho);

  if(SHOCK_CAPTURING && SHOCK_CAPTURING_TYPE == FEISTAUER)
    wf.set_stabilization(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e, NU_1, NU_2);

  // Initialize the FE problem.
  DiscreteProblem<double> dp(&wf, Hermes::vector<const Space<double>*>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e));
  DiscreteProblem<double> dp_stabilization(&wf_stabilization, &space_stabilization);
  bool* discreteIndicator = NULL;

  // If the FE problem is in fact a FV problem.
  if(P_INIT == 0)
    dp.set_fvm();

  // Time stepping loop.
  for(; t < 3.0; t += time_step_n)
  {
    Hermes::Mixins::Loggable::static_info("---- Time step %d, time %3.5f.", iteration++, t);

    if(SHOCK_CAPTURING && SHOCK_CAPTURING_TYPE == FEISTAUER)
      {
        dp_stabilization.assemble(rhs_stabilization);
        if(discreteIndicator != NULL)
          delete [] discreteIndicator;
        discreteIndicator = new bool[space_stabilization.get_mesh()->get_max_element_id() + 1];
        for(unsigned int i = 0; i < space_stabilization.get_mesh()->get_max_element_id() + 1; i++)
          discreteIndicator[i] = false;
        Element* e;
        for_all_active_elements(e, space_stabilization.get_mesh())
        {
          AsmList<double> al;
          space_stabilization.get_element_assembly_list(e, &al);
          if(rhs_stabilization->get(al.get_dof()[0]) >= 1)
            discreteIndicator[e->id] = true;
        }
        wf.set_discreteIndicator(discreteIndicator);
      }

    // Set the current time step.
    wf.set_time_step(time_step_n);

    // Assemble the stiffness matrix and rhs.
    Hermes::Mixins::Loggable::static_info("Assembling the stiffness matrix and right-hand side vector.");

    dp.assemble(matrix, rhs);

    // Solve the matrix problem.
    Hermes::Mixins::Loggable::static_info("Solving the matrix problem.");
    if(solver->solve())
    {
      if(!SHOCK_CAPTURING || SHOCK_CAPTURING_TYPE == FEISTAUER)
      {
        Solution<double>::vector_to_solutions(solver->get_sln_vector(), Hermes::vector<const Space<double> *>(&space_rho, &space_rho_v_x, 
          &space_rho_v_y, &space_e), Hermes::vector<Solution<double> *>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
      }
      else
      {
        FluxLimiter* flux_limiter;
        if(SHOCK_CAPTURING_TYPE == KUZMIN)
          flux_limiter = new FluxLimiter(FluxLimiter::Kuzmin, solver->get_sln_vector(), Hermes::vector<const Space<double> *>(&space_rho, &space_rho_v_x, 
          &space_rho_v_y, &space_e));
        else
          flux_limiter = new FluxLimiter(FluxLimiter::Krivodonova, solver->get_sln_vector(), Hermes::vector<const Space<double> *>(&space_rho, &space_rho_v_x, 
          &space_rho_v_y, &space_e));

        if(SHOCK_CAPTURING_TYPE == KUZMIN)
          flux_limiter->limit_second_orders_according_to_detector();

        flux_limiter->limit_according_to_detector();

        flux_limiter->get_limited_solutions(Hermes::vector<Solution<double> *>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
      }
    }
    else
      throw Hermes::Exceptions::Exception("Matrix solver failed.\n");

    CFL.calculate_semi_implicit(Hermes::vector<Solution<double> *>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), &mesh, time_step_n);

    // Visualization.
    if((iteration - 1) % EVERY_NTH_STEP == 0) 
    {
      // Hermes visualization.
      if(HERMES_VISUALIZATION) 
      {
        Mach_number.reinit();
        pressure.reinit();
        pressure_view.show(&pressure);
        Mach_number_view.show(&Mach_number);
        pressure_view.save_numbered_screenshot("Pressure-%u.bmp", iteration - 1, true);
        Mach_number_view.save_numbered_screenshot("Mach-%u.bmp", iteration - 1, true);
      }
      // Output solution in VTK format.
      if(VTK_VISUALIZATION) 
      {
        pressure.reinit();
        Mach_number.reinit();
        Linearizer lin_pressure;
        char filename[40];
        sprintf(filename, "pressure-3D-%i.vtk", iteration - 1);
        lin_pressure.save_solution_vtk(&pressure, filename, "Pressure", true);
        Linearizer lin_mach;
        sprintf(filename, "Mach number-3D-%i.vtk", iteration - 1);
        lin_mach.save_solution_vtk(&Mach_number, filename, "MachNumber", true);
      }
    }
  }

  pressure_view.close();
  Mach_number_view.close();

  return 0;
}
