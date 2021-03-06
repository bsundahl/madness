
// Copyright 2021 Adrian Hurtado

#include "molresponse/timer.h"

#include <vector>

#include "TDDFT.h"
#include "madness/mra/mra.h"
// Needed for timers
namespace molresponse {
double pop(std::vector<double>& v) {
  double x = v.back();
  v.pop_back();
  return x;
}
// Pulled from SCF.cc, starts a timer
void start_timer(World& world) {
  world.gop.fence();
  ttt.push_back(wall_time());
  sss.push_back(cpu_time());
}
// Stops a timer
void end_timer(World& world, const char* msg) {
  MADNESS_CHECK(ttt.size() > 0);
  double wall = wall_time() - pop(ttt);
  double cpu = cpu_time() - pop(sss);
  if (world.rank() == 0) printf("   timer: %20.20s %8.2fs %8.2fs\n", msg, cpu, wall);
}
}  // namespace molresponse
