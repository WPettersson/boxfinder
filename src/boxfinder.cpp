/*

boxsplit - an implementation of ...
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#include <iostream>
#include <list>
#include <cmath>
#include <mutex>
#include <sstream>
#include <string>

#include <ilcplex/cplexx.h>

#include "boxfinder.hpp"
#include "env.hpp"
#include "problem.hpp"
#include "result.hpp"

#ifdef DEBUG
extern std::mutex debug_mutex;
#endif


Result * BoxFinder::operator()() {
  status_ = RUNNING;
#ifdef DEBUG
  debug_mutex.lock();
  std::cout << "Running " << *this << std::endl;
  std::cout << "Searching in " << box_->str() << std::endl;
  debug_mutex.unlock();
#endif
  Env e;
  int cplex_status;
  e.env = CPXXopenCPLEX(&cplex_status);
  Problem p(filename_.c_str(), e);
  CPXsetintparam(e.env, CPXPARAM_Parallel, CPX_PARALLEL_DETERMINISTIC);
  CPXsetintparam(e.env, CPXPARAM_Threads, 1);
  float eta = 0.01;

  // Create a pair <int, float> for each objective function.
  // This way we can track and "undo" a sort.
  std::vector<std::pair<int, float>> obj_utop;
  for(int count = 0; count < objCount_; ++count) {
    obj_utop.emplace_back(count, utopia_[count]);
  }

  if (p.objsen == MIN) {
    std::sort(obj_utop.begin(), obj_utop.end(),
        [](const std::pair<int, float> &a, const std::pair<int, float> &b) {
          return a.second < b.second;
        });
  } else {
    std::sort(obj_utop.begin(), obj_utop.end(),
        [](const std::pair<int, float> &a, const std::pair<int, float> &b) {
          return a.second > b.second;
        });
  }

  float sorted_utopia[objCount_];

  for(int count = 0; count < objCount_; ++count) {
    sorted_utopia[count] = obj_utop[count].second;
  }

  float u_tilde[objCount_];
  float u_eta[objCount_];
  float sigma = 0;
  float cap_u = 0;
  for(int i = 0; i < objCount_; ++i) {
    u_tilde[i] = sorted_utopia[i];
    sigma += u_tilde[i];
    if (sense_ == MIN) {
      u_eta[i] = sorted_utopia[i] - eta;
    } else {
      u_eta[i] = sorted_utopia[i] + eta;
    }
    cap_u += 1 / u_eta[i];
  }

  float denom = u_eta[0] * cap_u * (sigma - u_tilde[0]) - objCount_*(1 - eta);
  //float alpha = (u_eta[0] * (sigma - u_tilde[0])) / denom;

  float weights[objCount_];

  for(int i = 0; i < objCount_; ++i) {
    weights[i] = (u_eta[0] * (sigma - u_tilde[0]) - u_eta[i] * (1 - eta)) / (u_eta[i] * denom);
  }

  float rho = (1 - eta) / denom;



  // Variable numbering
  int cur_numcols = CPXXgetnumcols(e.env, e.lp);
  // Number of variables in actual problem (not counting the "stuff" I add)
  int num_variables = cur_numcols;
  // fi columns start at fi_index
  int fi_index = cur_numcols;
  // Add constraints for f_i variables.
  for(int count = 0; count < objCount_; ++count) {
    CPXNNZ rmatbeg[1];
    double rmatval[num_variables+1];
    CPXDIM rmatind[num_variables+1];
    rmatbeg[0] = 0;
    // Index converts back to objective-numbering from sorted-numbering
    int index = obj_utop[count].first;
    for(int i = 0; i < num_variables; ++i) {
      rmatind[i] = p.objind[index][i];
      rmatval[i] = p.objcoef[index][i];
    }
    // New variable for f_i
    rmatval[num_variables] = -1;
    rmatind[num_variables] = cur_numcols;
    double rhs[1] = {0};
    char sense[1] = {'E'};
    char name[] = "f_X";
    name[2] = '0' + count;
    char * names[1] = {name};
    CPXXaddrows(e.env, e.lp, 1 /* one new columns */, 1 /* one new row */,
                num_variables+1, // Number of non-zeros
                rhs, sense, rmatbeg, rmatind, rmatval,
                names, // new column name
                nullptr); // new row name
    cur_numcols += 1;
  }

  // Add constraints to keep us inside the Box.
  for(int count = 0; count < objCount_; ++count) {
    CPXNNZ rmatbeg[1];
    double rmatval[1];
    CPXDIM rmatind[1];
    rmatbeg[0] = 0;
    rmatind[0] = fi_index + count;
    rmatval[0] = 1;
    // Index converts back to objective-numbering from sorted-numbering
    int index = obj_utop[count].first;
    // We subtract 0.5 from the upper bound as the bound is meant to be <,
    // but CPLEX only does ≤
    double rhs[1];
    char sense[1];
    if (p.objsen == MIN) {
      rhs[0] = static_cast<double>(box_->u[index]) - 0.5;
      sense[0] = 'L';
    } else {
      // Same for a lower bound
      rhs[0] = static_cast<double>(box_->u[index]) + 0.5;
      sense[0] = 'G';
    }
    CPXXaddrows(e.env, e.lp, 0 /* no new columns */, 1 /* one new row */,
                1, // Number of non-zeros
                rhs, sense, rmatbeg, rmatind, rmatval,
                nullptr, // new column name
                nullptr); // new row name
  }

  // Add constraints for diff_i variables
  int diffi_index = cur_numcols;
  for(int count = 0; count < objCount_; ++count) {
    CPXNNZ rmatbeg[1];
    double rmatval[2];
    CPXDIM rmatind[2];
    rmatbeg[0] = 0;
    rmatind[0] = fi_index + count;
    rmatval[0] = weights[count];
    rmatind[1] = cur_numcols;
    cur_numcols += 1;
    rmatval[1] = -1;
    char name[] = "diffiX";
    name[5] = '0' + count;
    char * names[1] = {name};
    double rhs[1] = {weights[count] * sorted_utopia[count]};
    if (p.objsen == MAX) {
      rmatval[1] *= -1;
    }
    char sense[1] = {'E'};
    CPXXaddrows(e.env, e.lp, 1 /* one new columns */, 1 /* one new row */,
                2, // Number of non-zeros
                rhs, sense, rmatbeg, rmatind, rmatval,
                names, // new column name
                nullptr); // new row name
  }
  // Add mdiff variable
  char name[] = "max_diff";
  char * names[] = {name};
  CPXXaddcols(e.env, e.lp, 1 /* one new column */, 0 /* no non-zero */,
    nullptr /* no objective change */, nullptr /* cmatbeg */, nullptr /* cmatind */,
    nullptr /* cmatend */, nullptr /* lb */, nullptr /* ub */, names /* name */);
  int mdiff_index = cur_numcols;
  cur_numcols += 1;
// and constraints for it.
  for(int count = 0; count < objCount_; ++count) {
    CPXNNZ rmatbeg[1];
    double rmatval[2];
    CPXDIM rmatind[2];
    rmatbeg[0] = 0;
    rmatind[0] = diffi_index + count;
    rmatval[0] = 1;
    rmatind[1] = mdiff_index;
    rmatval[1] = -1;
    double rhs[1] = {0};
    char sense[1] = {'L'};
    CPXXaddrows(e.env, e.lp, 0 /* no new columns */, 1 /* one new row */,
                2, // Number of non-zeros
                rhs, sense, rmatbeg, rmatind, rmatval,
                nullptr, // new column name
                nullptr); // new row name
  }

  // Set new objective into something
  // obj = mdiff + rho*f_i - rho*u_i      MINIMIZE
  // obj = mdiff + rho*u_i - rho*f_i      MAXIMIZE
  // Don't forget that CPLEX doesn't "set" the objective function, it just
  // changes objective coefficients by index. If we don't refer to all possible
  // variables, we might have other variables in our objective (from e.g. when
  // we read in the problem).
  {
    double objcoef[cur_numcols];
    CPXDIM indices[cur_numcols];
    for(int count = 0; count < cur_numcols; ++count) {
      objcoef[count] = 0;
      indices[count] = count;
    }
    objcoef[mdiff_index] = 1;
    for(int count = 0; count < objCount_; ++count) {
      if (p.objsen == MIN) {
        objcoef[fi_index + count] = rho;
      } else {
        objcoef[fi_index + count] = -rho;
      }
    }
    CPXXchgobj(e.env, e.lp, cur_numcols, indices, objcoef);
  }

  // Set CPLEX problem sense to minimise. We always want to minimise the
  // difference.
  CPXXchgobjsen(e.env, e.lp, CPX_MIN);
  /* solve */
  CPXXwriteprob(e.env, e.lp, "test.lp", "LP");
  cplex_status = CPXXmipopt (e.env, e.lp);
  if (cplex_status != 0) {
    std::cerr << "Failed to optimize LP." << std::endl;
  }

  cplex_status = CPXXgetstat (e.env, e.lp);
  if ((cplex_status == CPXMIP_INFEASIBLE) || (cplex_status == CPXMIP_INForUNBD)) {
    status_ = DONE;
    CPXLONG soln[3];
    for(int i = 0; i < 3 ; ++i) {
      soln[i] = -1;
    }
#ifdef DEBUG
    debug_mutex.lock();
    std::cout << *this << " found infeasible" << std::endl;;
    debug_mutex.unlock();
#endif
    return new Result(box_, soln);
  }

  double objval[objCount_];
  cplex_status = CPXXgetx(e.env, e.lp, objval, fi_index, fi_index+objCount_);
  if (cplex_status != 0) {
    std::cerr << "Failed to obtain objective value." << std::endl;
    exit(0);
  }

  // Put the solution back into the obj_utop list of pairs. We then sort this
  // list based on the first entry (objective number) to get back the original
  // ordering.
  for(int count = 0; count < objCount_; ++count) {
    obj_utop[count].second = objval[count];
  }

  std::sort(obj_utop.begin(), obj_utop.end(),
      [](const std::pair<int, float> &a, const std::pair<int, float> &b) {
        return a.first < b.first;
      });


  CPXLONG soln[3];
  for(int i = 0; i < 3 ; ++i) {
    for(int j = 0; j < 3; ++j) {
      if (obj_utop[j].first == i) {
        soln[i] = obj_utop[j].second;
        break;
      }
    }
  }
#ifdef DEBUG
  debug_mutex.lock();
  std::cout << *this << " done, found [";
  for(int i = 0; i < 2; ++i) {
    std::cout << soln[i] << ", ";
  }
  std::cout << soln[2] << "]" << std::endl;
  debug_mutex.unlock();
#endif

  status_ = DONE;
  auto * res = new Result(box_, soln);
  return res;
}

std::string BoxFinder::str() const {
  std::stringstream ss;
  ss << "BoxFinder: " << objCount_ << " objectives";
  return ss.str();
}

std::string BoxFinder::details() const {
  std::stringstream ss(str());
  ss << std::endl << "BoxFinder " << this << " is " << status_ << std::endl;
  return ss.str();
}
