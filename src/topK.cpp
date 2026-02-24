#include <iostream>
#include <vector>
#include <queue>
#include <functional>

using namespace std;

vector<int> topK(const vector<int> &nums, int k)
{
    priority_queue<int, vector<int>, greater<int>> pq;
    for (int x : nums)
    {
        pq.push(x);
        if (pq.size() > k)
        {
            pq.pop();
        }
    }
    vector<int> result(k);
    for (int i = k - 1; i >= 0; --i)
    {
        result[i] = pq.top();
        pq.pop();
    }
    return result;
}

struct Node
{
    int value;
    int index;
    int listIndex;
    bool operator>(const Node &other) const
    {
        return value > other.value;
    }
};

vector<int> mergeKSorted(const vector<vector<int>>& nums)
{
    priority_queue<Node, vector<Node>, greater<Node>> pq;

    for (int i = 0; i < (int)nums.size(); i++)
    {
        if (!nums[i].empty())
        {
            pq.push(Node{nums[i][0], 0, i});
        }
    }

    vector<int> result;

    while (!pq.empty())
    {
        Node curr = pq.top();
        pq.pop();

        result.push_back(curr.value);

        int nextIndex = curr.index + 1;
        if (nextIndex < (int)nums[curr.listIndex].size())
        {
            pq.push(Node{nums[curr.listIndex][nextIndex],nextIndex , curr.listIndex});
        }
    }

    return result;
}

int main()
{
    // ToP-K
    // nums = [3,2,1,5,6,4], k = 2
    // output → [5,6]
    // auto nums = {3, 2, 1, 5, 6, 4};
    // vector<int> res = topK(nums, 2);

    // MERGED TOP K
    vector<vector<int>> lists = {{1, 4, 9}, {2, 3, 5}, {1, 7}};
    vector<int> res = mergeKSorted(lists);
    for (int z : res)
        cout << z << "\n";
    return 0;
}