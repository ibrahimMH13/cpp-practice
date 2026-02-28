#include <iostream>
#include <queue>
#include <unordered_map>
#include <functional>
#include <vector>

using namespace std;

class SlidingMedian
{

    priority_queue<long long> low;                                         // max
    priority_queue<long long, vector<long long>, greater<long long>> high; // less
    unordered_map<long long, int> delayed;

    int lowSize = 0;
    int highSize = 0;

    void pruneLow()
    {
        while (!low.empty())
        {
            long long x = low.top();
            auto it = delayed.find(x);
            if (it != delayed.end() && it->second > 0)
            {
                it->second--;
                if (it->second == 0)
                {
                    delayed.erase(x);
                }
                low.pop();
            }
            else
            {
                break;
            }
        }
    }

    void pruneHigh()
    {
        while (!high.empty())
        {
            long long y = high.top();
            auto it = delayed.find(y);

            if (it != delayed.end() && it->second > 0)
            {
                it->second--;
                if (it->second == 0)
                {
                    delayed.erase(y);
                }
                high.pop();
            }
            else
            {
                break;
            }
        }
    }

    void rebalance(){
        if (lowSize > highSize + 1)
        {
            pruneLow();
            long long x = low.top();
            low.pop();
            lowSize--;

            high.push(x);
            highSize++;
        }else if(highSize > lowSize){
            pruneHigh();
            long long x = high.top();
            high.pop();
            highSize--;
            low.push(x);
            lowSize++;
        }
        
    }
    public:
     void insert(long long x){
        if (!low.empty() || x < low.top())
        {
            low.push(x);
            lowSize++;
        }else{
            high.push(x);
            highSize++;
        }
        rebalance();
     }

     void erase(long long){

        delayed[x]++;
        pruneLow();

        if (!low.empty() && x <= low.top())
        {
            lowSize--;
            if (!low.empty() && x == low.top())
            {
                pruneLow();
            }else{
                highSize--;
                if (!high.empty() && x == high.top())
                {
                    pruneHigh();
                }
            }
            rebalance();
        }
     }

    double getMedian(int k){
        pruneLow();
        pruneHigh();

        if (k%2 ==1)
        {
            return(double) low.top();
        }
        return ((double)low.top() + (double)high.top()) /2.0;
        
    }
};
int main()
{
    
    cout << "Ibrahim \n";
    return 0;
}