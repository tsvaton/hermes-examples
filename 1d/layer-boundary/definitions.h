#include "hermes2d.h"

using namespace Hermes;
using namespace Hermes::Hermes2D;
using namespace WeakFormsH1;
using Hermes::Ord;

/* Exact solution */

class CustomExactFunction
{
public:
  CustomExactFunction(double K) : K(K) {};

  double uhat(double x);

  double duhat_dx(double x);

  double dduhat_dxx(double x);
 
protected:
  double K;
};

class CustomExactSolution : public ExactSolutionScalar<double>
{
public:
  CustomExactSolution(MeshSharedPtr mesh, double K);

  virtual double value (double x, double y) const;

  virtual void derivatives(double x, double y, double& dx, double& dy) const;

  virtual Ord ord (double x, double y) const;

  MeshFunction<double>* clone() const { return new CustomExactSolution(this->mesh, this->K); }

  ~CustomExactSolution();

  CustomExactFunction* cef;
  double K;
};

/* Custom function */

class CustomFunction: public Hermes::Hermes2DFunction<double>
{
public:
  CustomFunction(double coeff1);

  virtual double value(double x, double y) const;

  virtual Ord value(Ord x, Ord y) const;

  ~CustomFunction();

  CustomExactFunction* cef;
  double coeff1;
};

/* Weak forms */

class CustomWeakForm : public WeakForm<double>
{
public:
  CustomWeakForm(CustomFunction* f);
};
