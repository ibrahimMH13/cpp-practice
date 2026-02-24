#include <iostream>
#include <vector>
#include <unordered_map>
#include <queue>
#include <functional>
#include <utility>


using namespace std;
vector<int> intersectStored(const vector<int> &a, const vector<int> &b)
{

    vector<int> result;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (a[i] == b[j])
        {
            result.push_back(a[i]);
            i++;
            j++;
        }
        else if (a[i] < b[j])
        {
            i++;
        }
        else
        {
            j++;
        }
    }

    return result;
}

vector<int> mergedSorted(const vector<int> &a, const vector<int> &b)
{

    vector<int> out;
    out.reserve(a.size() + b.size());
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (a[i] <= b[j])
        {
            out.push_back(a[i]);
            i++;
        }
        else
        {
            out.push_back(b[j]);
            j++;
        }
    }
    while (i < a.size())
    {
        out.push_back(a[i]);
        i++;
    }

    while (j < b.size())
    {
        out.push_back(b[j]);
        j++;
    }

    return out;
}

void removeDuplicated(vector<int> &a)
{

    if (a.size() < 1)
        return;

    int write = 1;

    for (int read = 1; read < (int)a.size(); read++)
    {
        if (a[read] != a[read - 1])
        {
            a[write] = a[read];
            write++;
        }
    }
    a.resize(write);
}

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
    vector<int> result;
    result.reserve(k);

    while (!pq.empty())
    {
        result.push_back(pq.top());
        pq.pop();
    }

    return result;
}

vector<int> topKFrequent(const vector<int>& nums, int k){

        unordered_map<int, int> req;

        for (int x : nums)
        {
            req[x]++;
        }
        priority_queue<pair<int, int>,vector<pair<int,int>>,greater<pair<int,int>>> pq;

        for (auto& [value,count] : req)
        {
           pq.push({count, value});
           if (pq.size() > k)
           {
                pq.pop();
           }
           
        }

        vector<int> result;
        while (!pq.empty())
        {
            result.push_back(pq.top().second);
            pq.pop();

        }
        
        return result;        
}

struct Node {
    int value;
    int listIndex;
    int elemIndx;

    bool operator>(const Node& other) const{
        return value > other.value;
    }
};

vector<int> mergedKSorted(const vector<vector<int>>& lists){

    priority_queue<Node, vector<Node>, greater<Node>> pq;

    for (int i = 0; i < lists.size(); i++)
    {
       if (!lists[i].empty())
       {
         pq.push({
            lists[i][0],
            i,
            0
         });
       }
    }

    vector<int> result;
     while (!pq.empty())
     {
       Node curr = pq.top();
       pq.pop();
       result.push_back(curr.value);
       int nextInx = curr.elemIndx+1;
       if (nextInx < lists[curr.listIndex].size())
       {
            pq.push({
                lists[curr.listIndex][nextInx],
                curr.listIndex,
                nextInx
            });
       }
     }
     
    return result;
}


int main()
{

    //   const vector<int>  a = {5,5,5};
    //   const vector<int>  b = {4,5,5};
    //     auto r = intersectStored(a, b);
    //     for (int x : r) cout << x <<" ";
    //     cout << "\n";
    vector<int> a = {1, 2, 2,9, 4};
    const vector<int> b = {2, 2, 3};
     vector<int> c = {3,1,5,12,2,11};
      vector<int> d = {1,1,1,2,2,3};
      auto rr = {a,c,d};
    removeDuplicated(a);
    auto v = topK(c, 2);
    auto topKF = topKFrequent(d,2);
    auto merged = mergedKSorted(rr);
    for (int x : merged)
        cout << x << " ";
    cout << "\n";
    return 0;
}