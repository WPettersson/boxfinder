/*
This JobServer class is modified from the ThreadPool class, as written by Jakob
Progsch and Václav Zeman. Their original copyright is contained below, and
their original implementation can be found at
https://github.com/progschj/ThreadPool

Copyright (c) 2012 Jakob Progsch, Václav Zeman

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source distribution.
*/


#ifndef JOBSERVER_H
#define JOBSERVER_H

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "box.hpp"
#include "boxfinder.hpp"
#include "result.hpp"
#include "task.hpp"


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


class JobServer {
  public:
    explicit JobServer(size_t threads, CPXLONG *utopia_, Sense sense_, std::string name_);
    ~JobServer();

    void q(Box * b);
    void wait();

    std::list<Result *> getSolutions();

  private:
    std::list<Box *> waiting;
    std::list<Box *> runningBoxes;
    // How many threads are actively doing things, rather than waiting
    std::atomic<int> running;
    std::list<Result *> solutions;
    std::vector<std::thread> workers;
    std::mutex queue_mutex;
    std::mutex server_mutex;
    std::condition_variable condition;
    std::condition_variable server_condition;
    bool stop;
    CPXLONG *utopia;
    int objcnt;
    Sense sense;
    std::string name;

};

inline JobServer::JobServer(size_t threads, CPXLONG *utopia_, Sense sense_, std::string name_) : running(0),
  queue_mutex(), server_mutex(), stop(false), utopia(utopia_), objcnt(3), sense(sense_), name(name_) {
  for(size_t t = 0; t < threads; ++t) {
    workers.emplace_back(
      [this] {
        for (;;) {
          Box * nextBox;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(lock,
                [this]{ return this->stop || !this->waiting.empty(); });
            if (this->stop && this->waiting.empty()) {
              return;
            }
            running += 1;
            nextBox = this->waiting.front();
            this->waiting.pop_front();
            this->runningBoxes.push_back(nextBox);
          }
          BoxFinder finder(name, objcnt, sense, this, nextBox, utopia);
          Result * res = finder();
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            if (res->soln[0] == res->soln[1] &&
                res->soln[1] == res->soln[2] &&
                res->soln[0] == -1) {
              this->runningBoxes.erase(std::remove(runningBoxes.begin(), runningBoxes.end(),
                    nextBox), runningBoxes.end());
              delete nextBox;
              delete res;
            } else {
              solutions.push_back(res);
              // First run GenerateNewBoxesVsplit
              // Create the set containing the 3 sets S_i
              std::vector<std::vector<Box *>> sets;
              // Create the 3 sets S_i
              for(int i = 0; i < 3; ++i) {
                sets.emplace_back();
              }
              // We run the following loop over every box in waiting, and later
              // over every box in runningBoxes. We need this, as we need to a
              // certain level of consistency between the 'v' values of the
              // boxes which could otherwise be broken if we remove multiple
              // boxes and then split one of them.
              //
              std::list<Box *> toDelete;
              for(auto b: waiting) {
                // Line 30
                if (((sense == MIN) && (! b->less_than_u(res->soln))) ||
                    ((sense == MAX) && (! b->greater_than_u(res->soln)))) {
                  continue;
                }
                // line 31
                for(int i = 0; i < 3; ++i) {
                  // Line 32
                  if (((sense == MIN) && (res->soln[i] >= b->v[i]) && (res->soln[i] > utopia[i])) ||
                      ((sense == MAX) && (res->soln[i] <= b->v[i]) && (res->soln[i] < utopia[i]))) {
                    // Line 33
                    auto b_i = new Box(b);
                    // Line 34
                    b_i->u[i] = res->soln[i];
                    // Line 35
                    sets[i].push_back(b_i);
#ifdef DEBUG
                    debug_mutex.lock();
                    std::cout << "Split in " << i << " to make " << b_i->str() << std::endl;
                    debug_mutex.unlock();
#endif
                  }
                }
                // Line 36
                // Delete a box we're iterating over. This is hard while iterating, so
                // mark it as "to delete"
                b->done = true;
                toDelete.push_back(b);
              }
              for(auto b: runningBoxes) {
                // Line 30
                if (((sense == MIN) && (! b->less_than_u(res->soln))) ||
                    ((sense == MAX) && (! b->greater_than_u(res->soln)))) {
                  continue;
                }
                // line 31
                for(int i = 0; i < 3; ++i) {
                  // Line 32
                  if (((sense == MIN) && (res->soln[i] >= b->v[i]) && (res->soln[i] > utopia[i])) ||
                      ((sense == MAX) && (res->soln[i] <= b->v[i]) && (res->soln[i] < utopia[i]))) {
                    // Line 33
                    auto b_i = new Box(b);
                    // Line 34
                    b_i->u[i] = res->soln[i];
                    // Line 35
                    sets[i].push_back(b_i);
#ifdef DEBUG
                    debug_mutex.lock();
                    std::cout << "Split in " << i << " to make " << b_i->str() << std::endl;
                    debug_mutex.unlock();
#endif
                  }
                }
                // Line 36
                // Delete a box we're iterating over. This is hard while iterating, so
                // mark it as "to delete"
                b->done = true;
              }
              // Rest of line 36. Now we remove all the completed boxes, in one go.
              waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                  [](Box * b){return b->done;}), waiting.end());
              for(auto b: toDelete) {
                delete b;
              }

              // Note that running boxes will always be deleted by the task
              // running them, so we don't need to call delete on them.
              runningBoxes.erase(std::remove_if(runningBoxes.begin(),
                    runningBoxes.end(), [](Box * b){return b->done;}),
                    runningBoxes.end());
              delete nextBox;

              // Next step, UpdateIndividualSubsets
              for(int i = 0; i < 3; ++i) {
                if (sets[i].empty()) {
                  continue;
                }
#ifdef DEBUG
                debug_mutex.lock();
                std::cout << "UpdateIndividualSubsets in " << i << " has " << sets[i].size() << " elements." << std::endl;
                debug_mutex.unlock();
#endif
                int j, k;
                if (i == 0) {
                  j = 1; k = 2;
                } else if (i == 1) {
                  j = 0; k = 2;
                } else {
                  j = 0; k = 1;
                }
                // Lines 45 to 49. Also see box_sort function at start of this file
                if (sense == MIN) {
                  auto sort_fn = std::bind(box_sort, std::placeholders::_1, std::placeholders::_2, i);
                  std::sort(sets[i].begin(), sets[i].end(), sort_fn);
                } else {
                  // Maximising, negate sort function with a lambda.
                  auto sort_fn = std::bind(box_sort, std::placeholders::_1, std::placeholders::_2, i);
                  std::sort(sets[i].begin(), sets[i].end(), [sort_fn](Box *a, Box *b) {return !sort_fn(a,b);});
                }
                // Line 50
                if (sense == MIN) {
                  sets[i].front()->v[j] = res->soln[j];
                  sets[i].back()->v[k] = res->soln[k];
                } else {
                  sets[i].back()->v[j] = res->soln[j];
                  sets[i].front()->v[k] = res->soln[k];
                }
                // Line 51
                for(auto it = sets[i].begin() + 1; it != sets[i].end(); ++it) {
                  // Line 52
                  if (sense == MIN) {
                    (*it)->v[j] = (*(it-1))->u[j];
                    (*(it-1))->v[k] = (*it)->u[k];
                  } else {
                    (*(it-1))->v[j] = (*it)->u[j];
                    (*it)->v[k] = (*(it-1))->u[k];
                  }
                }
                // Line 54
                for(auto newbox: sets[i]) {
                  waiting.push_back(newbox);
                  condition.notify_one();
                }
              }
            }
          }
          running -= 1;
          server_condition.notify_one();
        }
      }
    );
  }
}

inline JobServer::~JobServer() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);
    stop = true;
  }
  condition.notify_all();
  for(std::thread &worker: workers) {
    worker.join();
  }
}

inline void JobServer::q(Box * b) {
  {
    std::unique_lock<std::mutex> lock(queue_mutex);

    // don't allow enqueueing after stopping the pool
    if (stop) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
    }
    this->waiting.push_back(b);
  }
  condition.notify_one();
}

inline std::list<Result *> JobServer::getSolutions() {
  return std::move(solutions);
}

inline void JobServer::wait() {
  std::unique_lock<std::mutex> lk(server_mutex);
  this->server_condition.wait(lk,
      [this]{ return (running == 0) && this->waiting.empty(); });
}

#endif /* JOBSERVER_H */
