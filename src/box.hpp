/*

boxsplit - an implementation of ...
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef BOX_HPP
#define BOX_HPP

#include <string>
#include <sstream>
#include <ilcplex/cplexx.h>

struct Box {
  Box(Box * old);
  Box(CPXLONG u_[], CPXLONG v_[]);
  bool less_than_u(CPXLONG a[]);
  bool greater_than_u(CPXLONG a[]);
  std::string str() const;

  CPXLONG u[3];
  CPXLONG v[3];
  // done marks whether we can delete this box
  bool done;
};

inline Box::Box(Box * old) : done(false) {
  for(int i = 0; i < 3; ++i) {
    u[i] = old->u[i];
    v[i] = old->v[i];
  }
}

inline Box::Box(CPXLONG u_[], CPXLONG v_[]) : done(false) {
  for(int i = 0; i < 3; ++i) {
    u[i] = u_[i];
    v[i] = v_[i];
  }
}

inline bool Box::less_than_u(CPXLONG a[]) {
  if ((a[0] < u[0]) && (a[1] < u[1]) && (a[2] < u[2])) {
    return true;
  }
  return false;
}

inline bool Box::greater_than_u(CPXLONG a[]) {
  if ((a[0] > u[0]) && (a[1] > u[1]) && (a[2] > u[2])) {
    return true;
  }
  return false;
}

inline std::string Box::str() const {
  std::stringstream ss;
  ss << "Box: [u: " << u[0] << ", " << u[1] << ", " << u[2];
  ss << ", v: " << v[0] << ", " << v[1] << ", " << v[2] << "]";
  return ss.str();
}

#endif /* BOX_HPP */
