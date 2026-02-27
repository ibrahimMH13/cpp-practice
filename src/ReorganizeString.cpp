#include <iostream>
#include<vector>
#include <unordered_map>
#include <queue>

using namespace std;


class Solution{

    public:
        
        string reorganizeString(string s){

            unordered_map<char, int> freq;
            priority_queue<pair<int, char>> maxHeap;
            for (char c: s)
            {
                freq[c]++;
            }
            
            for (auto& [ch, count]: freq)
            {
                maxHeap.push({count, ch});
            }
            string result = "";
            pair<int, char> prev = {0, '#'};  
            while (!maxHeap.empty())
            {
                auto [count, ch] = maxHeap.top();
                maxHeap.pop();
                result +=ch;
                count--;

                if (prev.first > 0)
                {
                   maxHeap.push(prev);
                }
                prev = {count,ch};
            }

            if (result.size() != s.size())
            {
                return "";
            }
            return result;
            
        }
};

int main(){
    Solution s;
    string r =  s.reorganizeString("aaaa");
    cout << r <<"\n";
    return 0;
}