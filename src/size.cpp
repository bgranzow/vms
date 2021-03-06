#include "size.hpp"
#include "control.hpp"
#include <PCU.h>
#include <apf.h>
#include <apfMesh.h>
#include <apfShape.h>
#include <apfCavityOp.h>

namespace vms {

struct Specification {
  apf::Mesh* mesh;
  apf::Field* error;
  int polynomial_order;
  size_t target_number;
  double alpha;
  double beta;
  double size_factor;
  apf::Field* elem_size;
  apf::Field* vtx_size;
  std::string size_name;
};

static void setup_specification(
    Specification* s,
    apf::Field* err,
    size_t t,
    int p,
    std::string name) {
  s->mesh = apf::getMesh(err);
  s->error = err;
  s->polynomial_order = p;
  s->target_number = t;
  s->alpha = 0.5;
  s->beta = 2.0;
  s->size_factor = 0.0;
  s->elem_size = 0;
  s->vtx_size = 0;
  s->size_name = name;
}

static double sum_contributions(Specification* s) {
  double r = 0.0;
  int d = s->mesh->getDimension();
  int p = s->polynomial_order;
  apf::MeshEntity* elem;
  apf::MeshIterator* it = s->mesh->begin(d);
  while ((elem = s->mesh->iterate(it))) {
    double v = fabs(apf::getScalar(s->error, elem, 0));
    r += pow(v, ((2.0*d)/(2.0*p+d)));
  }
  s->mesh->end(it);
  PCU_Add_Doubles(&r,1);
  return r;
}

static void compute_size_factor(Specification* s) {
  int d = s->mesh->getDimension();
  double G = sum_contributions(s);
  double N = s->target_number;
  s->size_factor = pow((G/N),(1.0/d));
}

static double get_current_size(apf::Mesh* m, apf::MeshEntity* e) {
  double h = 0.0;
  apf::Downward edges;
  int ne = m->getDownward(e,1,edges);
  for (int i=0; i < ne; ++i)
    h = std::max(h, apf::measure(m, edges[i]));
  return h;
}

static double get_new_size(Specification* s, apf::MeshEntity* e) {
  int p = s->polynomial_order;
  int d = s->mesh->getDimension();
  double h = get_current_size(s->mesh, e);
  double theta_e = fabs(apf::getScalar(s->error, e, 0));
  double r = pow(theta_e, ((-2.0)/(2.0*p+d)));
  double h_new = s->size_factor*r*h;
  if (h_new < s->alpha*h) h_new = s->alpha*h;
  if (h_new > s->beta*h) h_new = s->beta*h;
  return h_new;
}

static void get_elem_size(Specification* s) {
  apf::Field* e_size = apf::createStepField(s->mesh, "esize", apf::SCALAR);
  int d = s->mesh->getDimension();
  apf::MeshEntity* elem;
  apf::MeshIterator* it = s->mesh->begin(d);
  while ((elem = s->mesh->iterate(it))) {
    double h = get_new_size(s, elem);
    apf::setScalar(e_size, elem, 0, h);
  }
  s->mesh->end(it);
  s->elem_size = e_size;
}

static void avg_to_vtx(apf::Field* ef, apf::Field* vf, apf::MeshEntity* ent) {
  apf::Mesh* m = apf::getMesh(ef);
  apf::Adjacent elems;
  m->getAdjacent(ent, m->getDimension(), elems);
  double s = 100000.0;
  for (size_t i=0; i < elems.getSize(); ++i)
    s = std::min(s, apf::getScalar(ef, elems[i], 0));
  apf::setScalar(vf, ent, 0, s);
}

class AverageOp : public apf::CavityOp {
  public:
    AverageOp(Specification* s) :
      apf::CavityOp(s->mesh),
      specs(s),
      entity(0)
    {
    }
    virtual Outcome setEntity(apf::MeshEntity* e) {
      entity = e;
      if (apf::hasEntity(specs->vtx_size, entity))
        return SKIP;
      if (! requestLocality(&entity, 1))
        return REQUEST;
      return OK;
    }
    virtual void apply() {
      avg_to_vtx(specs->elem_size, specs->vtx_size, entity);
    }
    Specification* specs;
    apf::MeshEntity* entity;
};

static void average_size_field(Specification* s) {
  std::string n = s->size_name;
  s->vtx_size = apf::createLagrangeField(s->mesh, n.c_str(), apf::SCALAR, 1);
  AverageOp op(s);
  op.applyToDimension(0);
}

static void create_size_field(Specification* s) {
  compute_size_factor(s);
  get_elem_size(s);
  average_size_field(s);
  apf::destroyField(s->elem_size);
  apf::destroyField(s->error);
}

apf::Field* get_iso_target_size(
    apf::Field* e,
    std::size_t t,
    std::string name) {
  double t0 = time();
  ASSERT(t > 0);
  Specification s;
  setup_specification(&s, e, t, 1, name);
  create_size_field(&s);
  double t1 = time();
  print("isotropic target size field computed in %f seconds", t1-t0);
  return s.vtx_size;
}

apf::Field* get_min_size(
    apf::Field* s1,
    apf::Field* s2,
    apf::Field* s3) {
  apf::Mesh* m = apf::getMesh(s1);
  apf::Field* f = apf::createLagrangeField(m, "min_size", apf::SCALAR, 1); 
  apf::MeshEntity* vtx = 0;
  apf::MeshIterator* vertices = m->begin(0);
  while ((vtx = m->iterate(vertices))) {
    double v1 = apf::getScalar(s1, vtx, 0);
    double v2 = apf::getScalar(s2, vtx, 0);
    double v3 = apf::getScalar(s3, vtx, 0);
    double v = std::min(v1, v2);
    v = std::min(v, v3);
    apf::setScalar(f, vtx, 0, v);
  }
  m->end(vertices);
  apf::destroyField(s1);
  apf::destroyField(s2);
  apf::destroyField(s3);
  return f;
}

}
