#include <iostream>
#include <functional>
#include <vector>
#include <queue>
using namespace std;

class KthLargest
{

public:
    KthLargest(int k) : k_(k) {}
    int add(int newNumber)
    {

        pq.push(newNumber);
        if (pq.size() > k_)
        {
            pq.pop();
        }
        return pq.top();
    }

private:
    int k_;
    priority_queue<int, vector<int>, greater<int>> pq ;

};


int main()
{

    KthLargest kth(3);

    vector<int> stream = {10, 7, 25, 3, 40, 18};
    for (int x : stream)
    {
        cout << "Add: " << x
             << " | 3rd largest: "
             << kth.add(x)
             << endl;
    }

    return 0;
}