/*

boxsplit - an implementation of a multi-criteria optimisation algorithm of Klamroth and Dächert
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef RESULT_HPP
#define RESULT_HPP

#include <ilcplex/cplexx.h>

class Box;

class Result {
  public:
    Result(Box *box, CPXLONG soln_[]);
    ~Result();
    CPXLONG * soln;
    Box * box() { return box_; }

  private:
    Box * box_;
};

inline Result::Result(Box *box, CPXLONG soln_[]) :
  box_(box) {
  this->soln = new CPXLONG[3];
  for(int i = 0; i < 3; ++i) {
    this->soln[i] = soln_[i];
  }
}

inline Result::~Result() {
  delete this->soln;
}


#endif /* RESULT_HPP */
