#define HERMES_REPORT_INFO
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace Hermes;
using namespace Hermes::Hermes2D;
using namespace Hermes::Hermes2D::Views;
using namespace Hermes::Hermes2D::RefinementSelectors;

// This example solves the compressible Euler equations using Discontinuous Galerkin method of higher order with adaptivity.
//
// Equations: Compressible Euler equations, perfect gas state equation.
//
// Domain: forward facing step, see mesh file ffs.mesh
//
// BC: Solid walls, inlet, no outlet.
//
// IC: Constant state identical to inlet.
//
// The following parameters can be changed:

// Visualization.
// Set to "true" to enable Hermes OpenGL visualization. 
const bool HERMES_VISUALIZATION = false;
// Set to "true" to enable VTK output.
const bool VTK_VISUALIZATION = true;
// Set visual output for every nth step.
const unsigned int EVERY_NTH_STEP = 1;
// Shock capturing.
bool SHOCK_CAPTURING = true;
// Quantitative parameter of the discontinuity detector.
double DISCONTINUITY_DETECTOR_PARAM = 1.0;

// For saving/loading of solution.
bool REUSE_SOLUTION = false;

// Initial polynomial degree.     
const int P_INIT = 0;                                              
// Number of initial uniform mesh refinements. 
const int INIT_REF_NUM = 1;                                             
// Number of initial localized mesh refinements.         
const int INIT_REF_NUM_STEP = 1;                                 
// CFL value.
double CFL_NUMBER = 0.5;                         
// Initial time step.
double time_step = 1E-6;                          

// Adaptivity.
// Every UNREF_FREQth time step the mesh is unrefined.
const int UNREF_FREQ = 5;

// Number of mesh refinements between two unrefinements.
// The mesh is not unrefined unless there has been a refinement since
// last unrefinement.
int REFINEMENT_COUNT = 0;

// This is a quantitative parameter of the adapt(...) function and
// it has different meanings for various adaptive strategies (see below).
const double THRESHOLD = 0.3;                     
// Adaptive strategy:
const int STRATEGY = 1;                           

// Predefined list of element refinement candidates. Possible values are
// H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
// H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
CandList CAND_LIST = H2D_HP_ANISO;                

// Maximum polynomial degree used. -1 for unlimited.
const int MAX_P_ORDER = 1;                       

// Maximum allowed level of hanging nodes:
const int MESH_REGULARITY = -1;                   

// This parameter influences the selection of
// candidates in hp-adaptivity. Default value is 1.0. 
const double CONV_EXP = 1;                        

// Stopping criterion for adaptivity.
double ERR_STOP = 5.0;                     

// Adaptivity process stops when the number of degrees of freedom grows over
// this limit. This is mainly to prevent h-adaptivity to go on forever.
const int NDOF_STOP = 16000;                   

// Matrix solver for orthogonal projections: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
// SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  

// Equation parameters.
const double P_EXT = 1.0;         // Exterior pressure (dimensionless).
const double RHO_EXT = 1.4;       // Inlet density (dimensionless).   
const double V1_EXT = 3.0;        // Inlet x-velocity (dimensionless).
const double V2_EXT = 0.0;        // Inlet y-velocity (dimensionless).
const double KAPPA = 1.4;         // Kappa.

// Boundary markers.
const std::string BDY_SOLID_WALL_BOTTOM = "1";
const std::string BDY_OUTLET = "2";
const std::string BDY_SOLID_WALL_TOP = "3";
const std::string BDY_INLET = "4";

// Weak forms.
#include "../forms_explicit.cpp"

// Initial condition.
#include "../initial_condition.cpp"

// Criterion for mesh refinement.
int refinement_criterion(Element* e)
{
  if(e->vn[2]->y <= 0.4 && e->vn[1]->x <= 0.6)
    return 0;
  else
    return -1;
}

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh, base_mesh;
  MeshReaderH2D mloader;
  mloader.load("ffs.mesh", &base_mesh);

  base_mesh.refine_by_criterion(refinement_criterion, INIT_REF_NUM_STEP);

  // Perform initial mesh refinements.
  for (int i = 0; i < INIT_REF_NUM; i++)
    base_mesh.refine_all_elements(0, true);

  mesh.copy(&base_mesh);

  // Initialize boundary condition types and spaces with default shapesets.
  L2Space<double>space_rho(&mesh, P_INIT);
  L2Space<double>space_rho_v_x(&mesh, P_INIT);
  L2Space<double>space_rho_v_y(&mesh, P_INIT);
  L2Space<double>space_e(&mesh, P_INIT);
  int ndof = Space<double>::get_num_dofs(Hermes::vector<const Space<double>*>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e));

  // Initialize solutions, set initial conditions.
  ConstantSolution<double> sln_rho(&mesh, RHO_EXT);
  ConstantSolution<double> sln_rho_v_x(&mesh, RHO_EXT * V1_EXT);
  ConstantSolution<double> sln_rho_v_y(&mesh, RHO_EXT * V2_EXT);
  ConstantSolution<double> sln_e(&mesh, QuantityCalculator::calc_energy(RHO_EXT, RHO_EXT * V1_EXT, RHO_EXT * V2_EXT, P_EXT, KAPPA));

  ConstantSolution<double> prev_rho(&mesh, RHO_EXT);
  ConstantSolution<double> prev_rho_v_x(&mesh, RHO_EXT * V1_EXT);
  ConstantSolution<double> prev_rho_v_y(&mesh, RHO_EXT * V2_EXT);
  ConstantSolution<double> prev_e(&mesh, QuantityCalculator::calc_energy(RHO_EXT, RHO_EXT * V1_EXT, RHO_EXT * V2_EXT, P_EXT, KAPPA));

  ConstantSolution<double> rsln_rho(&mesh, RHO_EXT);
  ConstantSolution<double> rsln_rho_v_x(&mesh, RHO_EXT * V1_EXT);
  ConstantSolution<double> rsln_rho_v_y(&mesh, RHO_EXT * V2_EXT);
  ConstantSolution<double> rsln_e(&mesh, QuantityCalculator::calc_energy(RHO_EXT, RHO_EXT * V1_EXT, RHO_EXT * V2_EXT, P_EXT, KAPPA));

  // Initialize weak formulation.
  Hermes::vector<std::string> solid_wall_markers(BDY_SOLID_WALL_BOTTOM, BDY_SOLID_WALL_TOP);
  Hermes::vector<std::string> inlet_markers;
  inlet_markers.push_back(BDY_INLET);
  Hermes::vector<std::string> outlet_markers;
  outlet_markers.push_back(BDY_OUTLET);

  EulerEquationsWeakFormSemiImplicit wf(KAPPA, RHO_EXT, V1_EXT, V2_EXT, P_EXT,solid_wall_markers, 
    inlet_markers, outlet_markers, &prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e);
  
  // Filters for visualization of Mach number, pressure and entropy.
  MachNumberFilter Mach_number(Hermes::vector<MeshFunction<double>*>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e), KAPPA);
  PressureFilter pressure(Hermes::vector<MeshFunction<double>*>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e), KAPPA);

  ScalarView pressure_view("Pressure", new WinGeom(0, 0, 600, 300));
  ScalarView Mach_number_view("Mach number", new WinGeom(700, 0, 600, 300));

  ScalarView s1("Rho", new WinGeom(0, 0, 700, 400));
  ScalarView s2("RhoVX", new WinGeom(700, 0, 700, 400));
  ScalarView s3("RhoVY", new WinGeom(0, 400, 700, 400));
  ScalarView s4("RhoE", new WinGeom(700, 400, 700, 400));

  // Initialize refinement selector.
  L2ProjBasedSelector<double> selector(CAND_LIST, CONV_EXP, MAX_P_ORDER);
  selector.set_error_weights(1.0, 1.0, 1.0);

  // Set up CFL calculation class.
  CFLCalculation CFL(CFL_NUMBER, KAPPA);

  // Look for a saved solution on the disk.
  CalculationContinuity<double> continuity(CalculationContinuity<double>::onlyTime);
  int iteration = 0; double t = 0;
  bool loaded_now = false;

  if(REUSE_SOLUTION && continuity.have_record_available())
  {
    continuity.get_last_record()->load_mesh(&mesh);
    Hermes::vector<Space<double> *> spaceVector = continuity.get_last_record()->load_spaces(Hermes::vector<Mesh *>(&mesh, &mesh, &mesh, &mesh));
    space_rho.copy(spaceVector[0], &mesh);
    space_rho_v_x.copy(spaceVector[1], &mesh);
    space_rho_v_y.copy(spaceVector[2], &mesh);
    space_e.copy(spaceVector[3], &mesh);
    continuity.get_last_record()->load_time_step_length(time_step);
    t = continuity.get_last_record()->get_time() + time_step;
    iteration = (continuity.get_num()) * EVERY_NTH_STEP + 1;
    loaded_now = true;
  }

  // Time stepping loop.
  for(; t < 14.5; t += time_step)
  {
    if(t > 0.3)
      ERR_STOP = 2.5;

    CFL.set_number(CFL_NUMBER + (t/4.5) * 1.0);
    Hermes::Mixins::Loggable::Static::info("---- Time step %d, time %3.5f.", iteration, t);

    // Adaptivity loop:
    int as = 1; 
    int ndofs_prev = 0;
    bool done = false;
    do
    {
      Hermes::Mixins::Loggable::Static::info("---- Adaptivity step %d:", as);

      Hermes::vector<Space<double> *>* ref_spacesNoDerefinement;
      
      // Periodic global derefinements.
      if (as == 1 && (iteration > 1 && iteration % UNREF_FREQ == 0 && REFINEMENT_COUNT > 0))
      {
        Hermes::Mixins::Loggable::Static::info("Global mesh derefinement.");

        REFINEMENT_COUNT = 0;

        space_rho.unrefine_all_mesh_elements(true);

        space_rho.adjust_element_order(-1, P_INIT);
        space_rho_v_x.adjust_element_order(-1, P_INIT);
        space_rho_v_y.adjust_element_order(-1, P_INIT);
        space_e.adjust_element_order(-1, P_INIT);
      }

      Mesh::ReferenceMeshCreator refMeshCreatorFlow(&mesh);
      Mesh* ref_mesh_flow = refMeshCreatorFlow.create_ref_mesh();

      int order_increase = 1;
      Space<double>::ReferenceSpaceCreator refSpaceCreatorRho(&space_rho, ref_mesh_flow, order_increase);
      Space<double>* ref_space_rho = refSpaceCreatorRho.create_ref_space();
      Space<double>::ReferenceSpaceCreator refSpaceCreatorRhoVx(&space_rho_v_x, ref_mesh_flow, order_increase);
      Space<double>* ref_space_rho_v_x = refSpaceCreatorRhoVx.create_ref_space();
      Space<double>::ReferenceSpaceCreator refSpaceCreatorRhoVy(&space_rho_v_y, ref_mesh_flow, order_increase);
      Space<double>* ref_space_rho_v_y = refSpaceCreatorRhoVy.create_ref_space();
      Space<double>::ReferenceSpaceCreator refSpaceCreatorE(&space_e, ref_mesh_flow, order_increase);
      Space<double>* ref_space_e = refSpaceCreatorE.create_ref_space();

      Hermes::vector<const Space<double>*> ref_spaces_const(ref_space_rho, ref_space_rho_v_x, ref_space_rho_v_y, ref_space_e);

      if(ndofs_prev != 0)
        if(Space<double>::get_num_dofs(ref_spaces_const) == ndofs_prev)
          selector.set_error_weights(2.0 * selector.get_error_weight_h(), 1.0, 1.0);
        else
          selector.set_error_weights(1.0, 1.0, 1.0);

      ndofs_prev = Space<double>::get_num_dofs(ref_spaces_const);

      // Project the previous time level solution onto the new fine mesh.
      Hermes::Mixins::Loggable::Static::info("Projecting the previous time level solution onto the new fine mesh.");
      if(loaded_now)
      {
        loaded_now = false;

        continuity.get_last_record()->load_solutions(Hermes::vector<Solution<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e),
            Hermes::vector<Space<double> *>(ref_space_rho, ref_space_rho_v_x, ref_space_rho_v_y, ref_space_e));
      }
      else
      {
        OGProjection<double> ogProjection;
        ogProjection.project_global(ref_spaces_const, Hermes::vector<Solution<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), 
        Hermes::vector<Solution<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), Hermes::vector<Hermes::Hermes2D::ProjNormType>());
      }

      FluxLimiter flux_limiterLoading(FluxLimiter::Kuzmin, Hermes::vector<Solution<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), ref_spaces_const, true);

      flux_limiterLoading.limitOscillations = true;

      int limited = flux_limiterLoading.limit_according_to_detector();
      int counter = 0;
      Hermes::Mixins::Loggable::Static::info("Limited in %d-th step: %d.", ++counter, limited);
      while(limited > 10)
      {
        limited = flux_limiterLoading.limit_according_to_detector();
        Hermes::Mixins::Loggable::Static::info("Limited in %d-th step: %d.", ++counter, limited);
      }

      flux_limiterLoading.get_limited_solutions(Hermes::vector<Solution<double>*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));

      if(iteration > std::max((int)(continuity.get_num() * EVERY_NTH_STEP + 1), 1))
      {
        delete rsln_rho.get_mesh();
        delete rsln_rho_v_x.get_mesh();
        delete rsln_rho_v_y.get_mesh();
        delete rsln_e.get_mesh();
      }

      // Report NDOFs.
      Hermes::Mixins::Loggable::Static::info("ndof_coarse: %d, ndof_fine: %d.", 
        Space<double>::get_num_dofs(Hermes::vector<const Space<double> *>(&space_rho, &space_rho_v_x, 
        &space_rho_v_y, &space_e)), Space<double>::get_num_dofs(ref_spaces_const));

      // Assemble the reference problem.
      Hermes::Mixins::Loggable::Static::info("Solving on reference mesh.");
      DiscreteProblem<double> dp(&wf, ref_spaces_const);

      SparseMatrix<double>* matrix = create_matrix<double>();
      Vector<double>* rhs = create_vector<double>();
      LinearMatrixSolver<double>* solver = create_linear_solver<double>( matrix, rhs);

      wf.set_current_time_step(time_step);

      // Assemble the stiffness matrix and rhs.
      Hermes::Mixins::Loggable::Static::info("Assembling the stiffness matrix and right-hand side vector.");
      dp.assemble(matrix, rhs);

      // Solve the matrix problem.
      Hermes::Mixins::Loggable::Static::info("Solving the matrix problem.");
      try
      {
        solver->solve();

        Hermes::Mixins::Loggable::Static::info("Solved.");

        if(!SHOCK_CAPTURING)
          Solution<double>::vector_to_solutions(solver->get_sln_vector(), ref_spaces_const, 
          Hermes::vector<Solution<double>*>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e));
        else
        {      
          FluxLimiter flux_limiter(FluxLimiter::Kuzmin, solver->get_sln_vector(), ref_spaces_const, true);

          flux_limiter.limit_second_orders_according_to_detector(Hermes::vector<Space<double> *>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e));

          flux_limiter.limit_according_to_detector(Hermes::vector<Space<double> *>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e));

          flux_limiter.get_limited_solutions(Hermes::vector<Solution<double>*>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e));
        }
      }
      catch(std::exception& e)
      {
        std::cout << e.what();
      }

      // Project the fine mesh solution onto the coarse mesh.
      Hermes::Mixins::Loggable::Static::info("Projecting reference solution on coarse mesh.");
      OGProjection<double> ogProjection; ogProjection.project_global(Hermes::vector<const Space<double> *>(&space_rho, &space_rho_v_x,
        &space_rho_v_y, &space_e), Hermes::vector<Solution<double>*>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e), 
        Hermes::vector<Solution<double>*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e), 
        Hermes::vector<ProjNormType>(HERMES_L2_NORM, HERMES_L2_NORM, HERMES_L2_NORM, HERMES_L2_NORM)); 

      // Calculate element errors and total error estimate.
      Hermes::Mixins::Loggable::Static::info("Calculating error estimate.");
      Adapt<double>* adaptivity = new Adapt<double>(Hermes::vector<Space<double> *>(&space_rho, &space_rho_v_x, 
        &space_rho_v_y, &space_e), Hermes::vector<ProjNormType>(HERMES_L2_NORM, HERMES_L2_NORM, HERMES_L2_NORM, HERMES_L2_NORM));
      double err_est_rel_total = adaptivity->calc_err_est(Hermes::vector<Solution<double>*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e),
        Hermes::vector<Solution<double>*>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e)) * 100;

      CFL.calculate_semi_implicit(Hermes::vector<Solution<double> *>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e), ref_space_rho->get_mesh(), time_step);

      // Report results.
      Hermes::Mixins::Loggable::Static::info("err_est_rel: %g%%", err_est_rel_total);

      // If err_est too large, adapt the mesh.
      if (err_est_rel_total < ERR_STOP)
        done = true;
      else
      {
        if (Space<double>::get_num_dofs(Hermes::vector<const Space<double> *>(&space_rho, &space_rho_v_x, 
          &space_rho_v_y, &space_e)) >= NDOF_STOP) 
        {
          Hermes::Mixins::Loggable::Static::info("Max. number of DOFs exceeded.");
          REFINEMENT_COUNT++;
          done = true;
        }
        else
        {
          Hermes::Mixins::Loggable::Static::info("Adapting coarse mesh.");
          REFINEMENT_COUNT++;
          done = adaptivity->adapt(Hermes::vector<RefinementSelectors::Selector<double> *>(&selector, &selector, &selector, &selector), 
            THRESHOLD, STRATEGY, MESH_REGULARITY);
        }

        if(!done)
          as++;
      }

      // Visualization and saving on disk.
      if(done && (iteration - 1) % EVERY_NTH_STEP == 0 && iteration > 1)
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
          Mach_number.reinit();
          Linearizer lin;
          Orderizer ord;
          char filename[40];
          sprintf(filename, "Density-%i.vtk", iteration);
          lin.save_solution_vtk(&rsln_rho, filename, "Density", false);
          sprintf(filename, "Mach number-%i.vtk", iteration);
          lin.save_solution_vtk(&Mach_number, filename, "MachNumber", false);
          sprintf(filename, "Space-%i.vtk", iteration);
          ord.save_orders_vtk(ref_space_rho, filename);
          sprintf(filename, "Mesh-%i.vtk", iteration);
          ord.save_mesh_vtk(ref_space_rho, filename);
        }

        // Save the progress.
        if(iteration > 1)
        {
          continuity.add_record(t, Hermes::vector<Mesh*>(&mesh, &mesh, &mesh, &mesh), Hermes::vector<Space<double>*>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e), 
            Hermes::vector<Solution<double> *>(&rsln_rho, &rsln_rho_v_x, &rsln_rho_v_y, &rsln_e), time_step);
        }
      }

      // Clean up.
      delete solver;
      delete matrix;
      delete rhs;
      delete adaptivity;
    }
    while (done == false);

    iteration++;

    // Copy the solutions into the previous time level ones.
    prev_rho.copy(&rsln_rho);
    prev_rho_v_x.copy(&rsln_rho_v_x);
    prev_rho_v_y.copy(&rsln_rho_v_y);
    prev_e.copy(&rsln_e);
  }

  pressure_view.close();
  Mach_number_view.close();

  return 0;
}
