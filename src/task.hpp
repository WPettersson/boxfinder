/*

boxsplit - an implementation of a multi-criteria optimisation algorithm of Klamroth and Dächert
Copyright (C) 2017 William Pettersson <william.pettersson@gmail.com>

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef TASK_HPP
#define TASK_HPP

#include <algorithm>
#include <iostream>
#include <list>
#include <mutex>
#include <string>

#include "sense.hpp"

#ifdef DEBUG
extern std::mutex debug_mutex;
#endif

class Result;

/**
 * Status of a task:
 * WAITING - waiting for pre-requisites to complete
 * QUEUED - pre-requisites complete, waiting to start
 * RUNNING - running
 * DONE - done
 */
enum Status { WAITING, QUEUED, RUNNING, DONE };

class Task {
  public:
    Task(std::string filename, int objCount, Sense sense);

    bool isReady() const;
    Status status() const;
    int objCount() const;

    virtual Result * operator()() = 0;

    virtual std::string str() const = 0;
    virtual std::string details() const = 0;

  protected:
    Status status_;
    std::mutex listMutex_;
    std::list<Task *> preReqs_;

    std::string filename_;
    int objCount_;
    Sense sense_;

};

std::ostream & operator<<(std::ostream & str, const Task & t);

inline Task::Task(std::string filename, int objCount, Sense sense) :
    listMutex_(), filename_(std::move(filename)), objCount_(objCount),
    sense_(sense) {
  status_ = WAITING;
}

inline Status Task::status() const {
  return status_;
}

inline bool Task::isReady() const {
#ifdef DEBUG_TASKSERVER
  debug_mutex.lock();
  std::cout << "Checking if " << *this << " is ready" << std::endl;
  debug_mutex.unlock();
#endif
  for(auto task: preReqs_) {
    if (task->status() != DONE) {
#ifdef DEBUG_TASKSERVER
      debug_mutex.lock();
      std::cout << "Still waiting on " << *task << std::endl;
      debug_mutex.unlock();
#endif
      return false;
    }
  }
#ifdef DEBUG_TASKSERVER
  debug_mutex.lock();
  std::cout << *this << " is ready" << std::endl;
  debug_mutex.unlock();
#endif
  return true;
}

inline int Task::objCount() const {
  return objCount_;
}

inline std::ostream & operator<<(std::ostream & str, Status status_) {
  std::string res = "UNKNOWN STATE!";
  switch (status_) {
    case WAITING:
      res = "WAITING";
      break;
    case QUEUED:
      res = "QUEUED";
      break;
    case RUNNING:
      res = "RUNNING";
      break;
    case DONE:
      res = "DONE";
      break;
    default:
      break;
  }
  return str << res;
}

inline std::ostream & operator<<(std::ostream & str, const Task & t) {
  return str << t.str();
}

#endif /* TASK_HPP */
