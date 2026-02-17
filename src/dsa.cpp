#include <iostream>
#include <vector>

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
 
 void removeDuplicated(vector<int>& a){

    if (a.size() < 1) return;
    
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
 
int main()
{

    //   const vector<int>  a = {5,5,5};
    //   const vector<int>  b = {4,5,5};
    //     auto r = intersectStored(a, b);
    //     for (int x : r) cout << x <<" ";
    //     cout << "\n";
    vector<int> a = {1, 2, 2, 4};
    const vector<int> b = {2, 2, 3};
    removeDuplicated(a);
    for (int x : a)
        cout << x << " ";
    cout << "\n";
    return 0;
}