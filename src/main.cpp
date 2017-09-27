/*

boxsplit - an implementation of a multi-criteria optimisation algorithm of Klamroth and Dächert
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/


#include <iostream>
#include <iomanip>
#include <fstream>
#include <queue>
#include <vector>

#include <ilcplex/cplexx.h>

#include <boost/program_options.hpp>

#include "box.hpp"
#include "boxfinder.hpp"
#include "jobserver.hpp"
#include "problem.hpp"
#include "result.hpp"
#include "env.hpp"



namespace po = boost::program_options;
extern std::string HASH;
#ifdef DEBUG
std::mutex debug_mutex;
#endif




std::atomic<int> ipcount;

int main(int argc, char* argv[]) {

  int status = 0; /* Operation status */
  ipcount = 0;
  Env e;

  std::string pFilename, outputFilename;

  /* Timing */
  clock_t starttime, endtime;
  double cpu_time_used, elapsedtime, startelapsed;
  int num_threads;

  po::variables_map va_map;
  po::options_description opt("Options for boxfinder");
  opt.add_options()
    ("help,h", "Show this help.")
    ("lp,p",
      po::value<std::string>(&pFilename),
     "The LP file to solve. Required.")
    ("output,o",
      po::value<std::string>(&outputFilename),
     "The output file. Required.")
    ("threads,t",
      po::value<int>(&num_threads)->default_value(1),
     "Number of threads to use internally. Optional, default to 1.")
  ;

  po::store(po::parse_command_line(argc, argv, opt), va_map);
  po::notify(va_map);

  if (va_map.count("help")) {
    // usage();
    std::cout << "boxfinder at " << HASH << std::endl;
    std::cout << opt << std::endl;
    return(1);
  }

  if (va_map.count("lp") == 0) {
    std::cerr << "Error: You must pass in a problem file." << std::endl;
    std::cerr << opt << std::endl;
    return(1);
  }

  std::ofstream outFile;

  if (va_map.count("output") == 0) {
    std::cerr << "Error: You must pass in an output file." << std::endl;
    std::cerr << opt << std::endl;
    return(1);
  }


  /* Start the timer */
  starttime = clock();
  timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  startelapsed = start.tv_sec + start.tv_nsec/1e9;

  // Find global utopia/ideal point
  // Need to read problem, which means setting up env.
  e.env = CPXXopenCPLEX(&status);
  Problem p(pFilename.c_str(), e);
  if (p.objcnt != 3) {
    std::cerr << "Error: This program only works on problems with 3 objective "
      "functions." << std::endl;
    exit(-1);
  }
  CPXsetintparam(e.env, CPXPARAM_Parallel, CPX_PARALLEL_DETERMINISTIC);
  CPXsetintparam(e.env, CPXPARAM_Threads, 1);
  CPXLONG utopia[p.objcnt];
  for(int i = 0; i < p.objcnt; ++i) {
    // Optimise in i direction
    CPXDIM cur_numcols = CPXXgetnumcols(e.env, e.lp);
    status = CPXXchgobj(e.env, e.lp, cur_numcols, p.objind[i], p.objcoef[i]);
    if ( status ) {
      std::cerr << "Failed to change objective function." << std::endl;
    }
    status = CPXXmipopt (e.env, e.lp);
    ipcount++;
    if ( status ) {
      std::cerr << "Failed to obtain objective value." << std::endl;
    }
    double val;
    status = CPXXgetobjval(e.env, e.lp, &val);
    utopia[i] = round(val);
  }

  CPXLONG u[3];
  CPXLONG v[3];
  if (p.objsen == MIN) {
    for (int i = 0; i < p.objcnt; ++i) {
      u[i] = INT_MAX;
      v[i] = utopia[i]-1;
    }
  } else {
    for (int i = 0; i < p.objcnt; ++i) {
      u[i] = 0;
      v[i] = utopia[i]+1;
    }
  }


  JobServer server(num_threads, utopia, p.objsen, pFilename);


  // Create first Box
  auto * firstBox = new Box(u, v);

  server.q(firstBox);
  server.wait();
  std::list<CPXLONG *> solutions = server.getSolutions();

  /* Stop the clock. Sort and print results.*/
  endtime = clock();
  cpu_time_used=(static_cast<double>(endtime - starttime)) / CLOCKS_PER_SEC;
  clock_gettime(CLOCK_MONOTONIC, &start);
  elapsedtime = (start.tv_sec + start.tv_nsec/1e9 - startelapsed);
  // Sort biggest to smallest
  solutions.sort([] (CPXLONG *a, CPXLONG *b) -> bool {
      if (a[0] == b[0]) {
        return a[1] > b[1];
      }
      return (a[0] > b[0]);
      });
  solutions.unique([] (CPXLONG *a, CPXLONG *b) -> bool {
      for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) {
          return false;
        }
      }
      return true;
      });
  constexpr int width = 8;
  constexpr int precision = 3;
  outFile.open(outputFilename);
  outFile << std::endl << "Using BoxFinder at " << HASH << std::endl;
  for(auto sol: solutions) {
    outFile << sol[0];
    for(int i = 1; i < 3; ++i) {
      outFile << "\t" << sol[i];
    }
    outFile << std::endl;
  }
  outFile << std::endl << "---" << std::endl;
  int solCount = solutions.size();
  outFile << cpu_time_used << " CPU seconds" << std::endl;
  outFile << std::setw(width) << std::setprecision(precision) << std::fixed;
  outFile << elapsedtime << " elapsed seconds" << std::endl;
  outFile << std::setw(width) << std::setprecision(precision) << std::fixed;
  outFile << ipcount << " IPs solved" << std::endl;
  outFile << std::setw(width) << std::setprecision(precision) << std::fixed;
  outFile << solCount << " Solutions found" << std::endl;
  return 0;
}
