#include <chem/SCF.h>
#include <madness/world/worldmem.h>
#include <molresponse/ground_parameters.h>
#include <molresponse/response_parameters.h>
#include <stdlib.h>

//#include "TDDFT.h"  // All response functions/objects enter through this
//#include "molresponse/density.h"
#//include "molresponse/global_functions.h"

#if defined(HAVE_SYS_TYPES_H) && defined(HAVE_SYS_STAT_H) && defined(HAVE_UNISTD_H)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static inline int file_exists(const char* inpname) {
  struct stat buffer;
  size_t rc = stat(inpname, &buffer);
  return (rc == 0);
}
#endif

template <typename T>
void test_same(const T& t1, const T& t2) {
  if (t1 != t2) {
    print("t1, t2", t1, t2);
    using madness::operators::operator<<;
    std::cout << "++" << t1 << "++" << std::endl;
    std::cout << "++" << t2 << "++" << std::endl;

    throw std::runtime_error("failure in test");
    ;
  }
}

struct inputfile {
  std::string fname;
  inputfile(const std::string filename, const std::string lines) {
    fname = filename;
    std::ofstream myfile;
    myfile.open(fname);
    myfile << lines << std::endl;
    myfile.close();
  }

  ~inputfile() { remove(fname.c_str()); }
};
bool test_derived(World& world) {
  print("entering test_derived");
  std::string inputlines = R"input(mp3
			econv 1.e-4
			#dconv 1.e-4
			maxiter 12# asd
			end)input";
  inputfile ifile("input1", inputlines);

  ResponseParameters param;
  param.read_and_set_derived_values(world, "input1", "mp3");

  test_same(param.econv(), 1.e-4);
  test_same(param.dconv(), sqrt(param.econv()) * 0.1);
  return true;
}

int main(int argc, char** argv) {
  // Initialize MADNESS mpi
  initialize(argc, argv);
  //{  // limite lifetime of world so that finalize() can execute cleanly
  World world(SafeMPI::COMM_WORLD);
  startup(world, argc, argv, true);
  print_meminfo(world.rank(), "startup");

  FunctionDefaults<3>::set_pmap(pmapT(new LevelPmap<Key<3> >(world)));

  GroundParameters g_params;

  std::cout.precision(6);
  // This makes a default input file name of 'input'
  int success = 0;
  try {
    test_derived(world);
  } catch (std::exception& e) {
    print("\n\tan error occurred .. ");
    print(e.what());
    success = 1;
  } catch (...) {
    print("\n\tan unknown error occurred .. ");
    success = 1;
  }
  /*
      const char* inpname = "input";
      // Process 0 reads input information and broadcasts
      for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
          inpname = argv[i];
          break;
        }
      }
      ResponseParameters r_params;

      GroundStateCalculation g_params;
      if (world.rank() == 0) print("input filename: ", inpname);
      if (!file_exists(inpname)) throw "input file not found";

                  */
  // first step is to read the input for r_params and g_params
  /*
      // Read the ground parameters from the archive
      g_params.read(world, r_params.archive);
      if (world.rank() == 0) {
        g_params.print_params();
        print_molecule(world, g_params);
      }
      // if a proerty calculation set the number of states
      if (r_params.property) {
        r_params.SetNumberOfStates(g_params.molecule);
      }

      // print params
      if (world.rank() == 0) {
        r_params.print_params();
      }
      // Broadcast to all other nodes
      density_vector densityTest = SetDensityType(world, r_params.response_type, r_params, g_params);
      // Create the TDDFT object
      if (r_params.load_density) {
        print("Loading Density");
        densityTest.LoadDensity(world, r_params.load_density_file, r_params, g_params);
      } else {
        print("Computing Density");
        densityTest.compute_response(world);
      }
      //
      // densityTest.PlotResponseDensity(world);
      densityTest.PrintDensityInformation();

      if (r_params.response_type.compare("dipole") == 0) {  //
        print("Computing Alpha");
        Tensor<double> alpha = densityTest.ComputeSecondOrderPropertyTensor(world);
        print("Second Order Analysis");
        densityTest.PrintSecondOrderAnalysis(world, alpha);
      }
  */
  if (world.rank() == 0) printf("\nfinished at time %.1fs\n\n", wall_time());
  world.gop.fence();
  world.gop.fence();
  //}  // world is dead -- ready to finalize
  finalize();

  return success;
}
