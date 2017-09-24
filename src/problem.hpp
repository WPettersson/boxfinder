/*

boxsplit - an implementation of a multi-criteria optimisation algorithm of Klamroth and Dächert
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef PROBLEM_H
#define PROBLEM_H

#include "sense.hpp"
#include "env.hpp"

enum filetype_t { UNKNOWN, LP, MOP };

class Problem {
  public:
    int objcnt; // Number of objectives
    double* rhs;
    int** objind; // Objective indices
    double** objcoef; // Objective coefficients
    Sense objsen; // Objective sense. Note that all objectives must have the same
                // sense (i.e., either all objectives are to be minimised, or
                // all objectives are to be maximised).
    int* conind;
    char* consense;

    double mip_tolerance;

    filetype_t filetype;

    const char* filename();

    Problem(const char* filename, Env& env);
    ~Problem();

  private:
    int read_lp_problem(Env& e);
    int read_mop_problem(Env& e);
    const char* filename_;

};

inline const char * Problem::filename() {
  return filename_;
}

inline Problem::~Problem() {
  // If objcnt == 0, then no problem has been assigned and no memory allocated
  if (objcnt == 0)
    return;
  for(int j = 0; j < objcnt; ++j) {
    delete[] objind[j];
    delete[] objcoef[j];
  }
  delete[] objind;
  delete[] objcoef;
  delete[] rhs;
  delete[] conind;
  delete[] consense;
}
#endif /* PROBLEM_H */
