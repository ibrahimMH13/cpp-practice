#include <iostream>
#include <queue>
#include <vector>

using namespace std;

class Solution
{
public:
    vector<vector<int>> KClosest(vector<vector<int>> &points, int k)
    {
        priority_queue<pair<int, vector<int>>> maxHeap;
        for (auto &p : points)
        {
            int dist = p[0] * p[0] + p[1] * p[1];
            maxHeap.push({dist, p});
            if (maxHeap.size() > static_cast<size_t>(k))
            {
                maxHeap.pop();
            }
        }
        vector<vector<int>> result;
        while (!maxHeap.empty())
        {
            result.push_back(maxHeap.top().second);
            maxHeap.pop();
        }
        return result;
    }
};

int main()
{

    Solution s;
    vector<vector<int>> points = {
        {1, 3},
        {-2, 2},
        {5, 8},
        {0, 1}};

    int k = 2;

    vector<vector<int>> result = s.KClosest(points, k);

    for (auto &p : result)
    {
        cout << "[" << p[0] << "," << p[1] << "] \n";
    }

    return 0;
}
