/*

boxsplit - an implementation of a multi-criteria optimisation algorithm of Klamroth and Dächert
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef BOXFINDER_HPP
#define BOXFINDER_HPP

#include <mutex>
#include <string>

#include "box.hpp"
#include "sense.hpp"
#include "task.hpp"
#include "jobserver.hpp"


class BoxFinder: public Task {
  public:
    BoxFinder(std::string problemName, int objCount, Sense sense,
        JobServer *taskServer, Box * box, CPXLONG * utopia);
    ~BoxFinder();

    void addNextLevel(Task * nextLevel);
    Result * operator()() override;

    std::string str() const override;
    std::string details() const override;

  private:
    /**
     * The utopia point for this box.
     */
    CPXLONG * utopia_;
    Box * box_;

    JobServer * taskServer_;
};

inline BoxFinder::BoxFinder(std::string problemName, int objCount,
    Sense sense, JobServer *taskServer, Box * box, CPXLONG * utopia) :
    Task(problemName, objCount, sense), box_(box), taskServer_(taskServer) {
    utopia_ = new CPXLONG[objCount_];
    for(int i = 0; i < objCount_; ++i) {
      utopia_[i] = utopia[i];
    }
}

inline BoxFinder::~BoxFinder() {
    delete[] utopia_;
}

#endif /* BOXFINDER_HPP */
