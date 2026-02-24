
#include <iostream>
#include <functional>
#include <queue>
#include <functional>

using namespace std;

class MedianFinder
{

    priority_queue<int> low;
    priority_queue<int, vector<int>, greater<int>> high;

public:
    MedianFinder() = default;

    void addNumber(int num)
    {

        if (low.empty() || num <= low.top())
        {
            low.push(num);
        }
        else
        {
            high.push(num);
        }

        if (low.size() > high.size() + 1)
        {
            high.push(low.top());
            low.pop();
        }
        else if (high.size() > low.size())
        {
            low.push(high.top());
            high.pop();
        }

        if (!low.empty() && !high.empty() && low.top() > high.top())
        {
            int a = low.top();
            low.pop();
            int b = high.top();
            high.pop();

            low.push(b);
            high.push(a);
        }
    }

    double findMedian()
    {
        if (low.empty() && high.empty())
        {
            return 0.0;
        }

        if (low.size() == high.size())
        {
            return (static_cast<double>(low.top()) + static_cast<double>(high.top())) / 2.0;
        }
        return static_cast<double>(low.top());
    }
};

int main()
{

    MedianFinder mf;

   vector<int> stream = {5, 15, 1, 3};

    for (int n : stream)
    {
        mf.addNumber(n);
        cout << "Added: " << n
             << " | Median: "
             << mf.findMedian()
             << std::endl;
    }
    return 0;
}