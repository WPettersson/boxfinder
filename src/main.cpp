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




/**
 * Key function for sorting boxes as per UpdateIndividualSubsets
 * Let j,k \in \{1,2,3\} \setminus \{index\}
 * Then we want box[q]->u[j] \leq box[q+1]->u[j]
 * and          box[q]->u[k] \geq box[q+q]->u[k]
 * If box[q]->u == box[q+1]->u, then :
 *              box[q]->v[j] \leq box[q+1]->v[j] and
 *              box[q]->v[k] \geq box[q+1]->v[k]
 */
bool box_sort(const Box &a, const Box &b, int index) {
  int j, k;
  if (index == 0) {
    j = 1; k = 2;
  } else if (index == 1) {
    j = 0; k = 2;
  } else {
    j = 0; k = 1;
  }

  if ((a.u[0] == b.u[0]) && (a.u[1] == b.u[1]) && (a.u[2] == b.u[2])) {
    if ((a.v[j] <= b.v[j]) && (a.v[k] >= b.v[k])) {
      return true;
    } else if ((b.v[j] <= a.v[j]) && (b.v[k] >= a.v[k])) {
      return false;
    } else {
      // TODO Bad?
      return false;
    }
  }
  if ((a.u[j] <= b.u[j]) && (a.u[k] >= b.u[k])) {
    return true;
  } else if ((b.u[j] <= a.u[j]) && (b.u[k] >= a.u[k])) {
    return false;
  } else {
    // TODO bad?
    return false;
  }
  return false;
}

std::atomic<int> ipcount;

int main(int argc, char* argv[]) {

  int status = 0; /* Operation status */
  ipcount = 0;
  Env e;

  std::string pFilename, outputFilename;

  int numSteps;
  bool shareSolns;
  /* Timing */
  clock_t starttime, endtime;
  double cpu_time_used, elapsedtime, startelapsed;
  int num_threads;

  po::variables_map va_map;
  po::options_description opt("Options for aira");
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
    ("steps,s",
      po::value<int>(&numSteps)->default_value(1),
     "Number of steps to take along each objective function when splitting up the search space. Optional, default to 1.")
    ("share,r",
     po::bool_switch(&shareSolns),
     "Share solutions (and relaxations) across divisions of the solution space.")
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

  JobServer server(num_threads);


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
  // Create first Box
  auto * firstBox = new Box(u, v);

  std::vector<Box *> boxes;
  std::vector<CPXLONG *> solutions;
  std::queue<BoxFinder *> allTasks;
  boxes.push_back(firstBox);
  // Create and run box
  BoxFinder * task = new BoxFinder(pFilename, p.objcnt, p.objsen, &server, firstBox, utopia);
  allTasks.push(task);
  server.q(task);
  while (! boxes.empty()) {
    Result * res = server.wait();
    if (res->soln[0] == res->soln[1] &&
        res->soln[1] == res->soln[2] &&
        res->soln[0] == -1) {
      // No solution found in this box
      for(auto it = boxes.begin(); it != boxes.end(); ++it) {
        if (*it == res->box()) {
          Box * box = (*it);
          boxes.erase(it);
          delete box;
          break;
        }
      }
    } else {
      // Have solution!
      CPXLONG * sol = new CPXLONG[3];
      for(int i = 0; i < 3; ++i) {
        sol[i] = res->soln[i];
      }
      solutions.push_back(sol);
      // First run GenerateNewBoxesVsplit
      // Create the set containing the 3 sets S_i
      std::vector<std::vector<Box *>> sets;
      // Create the 3 sets S_i
      for(int i = 0; i < 3; ++i) {
        sets.emplace_back();
      }
      for(auto b: boxes) {
        // Line 30
        if (((p.objsen == MIN) && (! b->less_than_u(res->soln))) || 
            ((p.objsen == MAX) && (! b->greater_than_u(res->soln)))) {
          continue;
        }
        // line 31
        for(int i = 0; i < 3; ++i) {
          // Line 32
          if (((p.objsen == MIN) && (res->soln[i] >= b->v[i]) && (res->soln[i] > utopia[i])) ||
              ((p.objsen == MAX) && (res->soln[i] <= b->v[i]) && (res->soln[i] < utopia[i]))) {
            // Line 33
            Box * b_i = new Box(b);
            // Line 34
            b_i->u[i] = res->soln[i];
            // Line 35
            sets[i].push_back(b_i);
          }
        }
        // Line 36
        // Delete a box we're iterating over. This is hard while iterating, so
        // mark it as "to delete"
        b->done = true;
      }
      // Rest of line 36. Now we remove all the completed boxes, in one go.
      boxes.erase(std::remove_if(boxes.begin(), boxes.end(), [](Box * b){return b->done;}),
                  boxes.end());
      // TODO We need to delete[] the removed boxes to avoid leaking memory?

      // Next step, UpdateIndividualSubsets
      for(int i = 0; i < 3; ++i) {
        if (sets[i].empty()) {
          continue;
        }
        int j, k;
        if (i == 0) {
          j = 1; k = 2;
        } else if (i == 1) {
          j = 0; k = 2;
        } else {
          j = 0; k = 1;
        }
        // Lines 45 to 49. Also see box_sort function at start of this file
        if (p.objsen == MIN) {
          auto sort_fn = std::bind(box_sort, std::placeholders::_1, std::placeholders::_2, i);
          std::sort(sets[i].begin(), sets[i].end(), sort_fn);
        } else {
          // Maximising, negate sort function with a lambda.
          auto sort_fn = std::bind(box_sort, std::placeholders::_1, std::placeholders::_2, i);
          std::sort(sets[i].begin(), sets[i].end(), [sort_fn](Box *a, Box *b) {return !sort_fn(a,b);});
        }
        // Line 50
        if (p.objsen == MIN) {
          sets[i].front()->v[j] = res->soln[j];
          sets[i].back()->v[k] = res->soln[k];
        } else {
          sets[i].back()->v[j] = res->soln[j];
          sets[i].front()->v[k] = res->soln[k];
        }
        // Line 51
        for(auto it = sets[i].begin() + 1; it != sets[i].end(); ++it) {
          // Line 52
          if (p.objsen == MIN) {
            (*it)->v[j] = (*(it-1))->u[j];
            (*(it-1))->v[k] = (*it)->u[k];
          } else {
            (*(it-1))->v[j] = (*it)->u[j];
            (*it)->v[k] = (*(it-1))->u[k];
          }
        }
        // Line 54
        for(auto newbox: sets[i]) {
          boxes.push_back(newbox);
        }
      }
    }
    if (boxes.empty()) {
      break;
    }
    Box * nextBox = boxes.front();
    // Set one more box search to be "running"
    BoxFinder * task = new BoxFinder(pFilename, p.objcnt, p.objsen, &server, nextBox, utopia);
    allTasks.push(task);
    server.q(task);
  }


  // TODO Delete tasks and remaining boxes?
  // Really, at this point we don't care. We're about to exit anyway.

  /* Stop the clock. Sort and print results.*/
  endtime = clock();
  cpu_time_used=(static_cast<double>(endtime - starttime)) / CLOCKS_PER_SEC;
  clock_gettime(CLOCK_MONOTONIC, &start);
  elapsedtime = (start.tv_sec + start.tv_nsec/1e9 - startelapsed);
  // Sort biggest to smallest
  std::sort(solutions.begin(), solutions.end(), [] (CPXLONG *a, CPXLONG *b) -> bool {
      if (a[0] == b[0]) {
        return a[1] > b[1];
      }
      return (a[0] > b[0]);
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
