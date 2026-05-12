#ifndef THREADID_H
#define THREADID_H
#include <set>
using namespace std;
class ThreadID {
private:
    set<int> availableIDs;
public:
    ThreadID();
    int getID();
    void releaseID(int id);
};
#endif