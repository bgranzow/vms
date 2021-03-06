#include "adapter.hpp"
#include "control.hpp"
#include "disc.hpp"
#include "size.hpp"
#include <ma.h>
#include <PCU.h>
#include <apf.h>
#include <spr.h>

namespace vms {

Adapter::Adapter(Input* in, Disc* d) {
  method = in->adapt_method;
  disc = d;
}

static void destroy_fields(Disc* d) {
  apf::Mesh* m = d->get_apf_mesh();
  apf::Field* uh = m->findField("uh");
  apf::Field* zh = m->findField("zh");
  apf::Field* Jeh1 = m->findField("Jeh1");
  apf::Field* Jeh2 = m->findField("Jeh2");
  apf::Field* Jeh1_bound = m->findField("Jeh1_bound");
  apf::Field* Jeh2_bound = m->findField("Jeh2_bound");
  if (uh) apf::destroyField(uh);
  if (zh) apf::destroyField(zh);
  if (Jeh1) apf::destroyField(Jeh1);
  if (Jeh2) apf::destroyField(Jeh2);
  if (Jeh1_bound) apf::destroyField(Jeh1_bound);
  if (Jeh2_bound) apf::destroyField(Jeh2_bound);
}

static apf::Field* get_spr_size_field(Disc* d, size_t t) {
  apf::Mesh* m = d->get_apf_mesh();
  apf::Field* uh = m->findField("uh");
  apf::Field* guh = spr::getGradIPField(uh, "guh", 1);
  apf::Field* size = spr::getTargetSPRSizeField(guh, t, 0.5, 2.0);
  apf::destroyField(guh);
  return size;
}

static apf::Field* get_size(Method method, Disc* d, size_t t) {
  apf::Field* size;
  apf::Mesh* m = d->get_apf_mesh();
  if (method == SPR) {
    print("using spr size field");
    size = get_spr_size_field(d, t);
  }
  else if (method == VMS1) {
    print("using vms 1 size field");
    apf::Field* e = m->findField("Jeh1_bound");
    size = get_iso_target_size(e, t);
  }
  else if (method == VMS2) {
    print("using vms 2 size field");
    apf::Field* e = m->findField("Jeh2_bound");
    size = get_iso_target_size(e, t);
  }
  else if (method == MIN) {
    print("using min size field");
    apf::Field* e1 = m->findField("Jeh1_bound");
    apf::Field* e2 = m->findField("Jeh2_bound");
    apf::Field* s1 = get_iso_target_size(e1, t, "size1");
    apf::Field* s2 = get_iso_target_size(e2, t, "size2");
    apf::Field* s3 = get_spr_size_field(d, t);
    size = get_min_size(s1,s2,s3);
  }
  return size;
}

void Adapter::adapt(size_t t, int i) {
  apf::Mesh2* m = disc->get_apf_mesh();
  apf::Field* size = get_size(method, disc, t);
  disc->write(i);
  destroy_fields(disc);
  ma::Input* in = ma::configure(m, size);
  in->maximumIterations = 1;
  in->shouldCoarsen = true;
  ma::adapt(in);
  apf::destroyField(size);
  disc->update();
}

void Adapter::unif_adapt(int i) {
  apf::Mesh2* m = disc->get_apf_mesh();
  disc->write(i);
  destroy_fields(disc);
  ma::runUniformRefinement(m);
  disc->update();
  disc->write(i+1);
}

}
