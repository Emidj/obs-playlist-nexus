#pragma once
#include <algorithm>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace nexus {
// One reservation for the standby decoder. Only presentation consumes it.
class BalancedOrder {
    struct Group {std::vector<size_t> members,bag;std::optional<size_t> last;int quota=1;};
    std::vector<Group> groups;
    std::vector<size_t> membership,round;
    std::optional<size_t> reserved,last_group;
    std::mt19937 rng;
    void new_round(){
        round.clear();
        for(size_t i=0;i<groups.size();++i)
            if(!groups[i].members.empty())round.insert(round.end(),groups[i].quota,i);
    }
public:
    explicit BalancedOrder(unsigned seed=std::random_device{}()):rng(seed){}
    void reset(const std::vector<std::string> &keys,const std::map<std::string,int> &quotas={}) {
        groups.clear();membership.clear();round.clear();reserved.reset();last_group.reset();
        std::map<std::string,size_t> ids;
        for(size_t i=0;i<keys.size();++i){
            auto [it,added]=ids.emplace(keys[i],groups.size());
            if(added){
                groups.emplace_back();
                if(auto quota=quotas.find(keys[i]);quota!=quotas.end())
                    groups.back().quota=std::clamp(quota->second,1,99);
            }
            membership.push_back(it->second);
            groups[it->second].members.push_back(i);
        }
    }
    std::optional<size_t> peek() {
        if(reserved)return reserved;
        if(round.empty())new_round();
        if(round.empty())return {};
        auto choices=round;
        if(last_group&&std::any_of(choices.begin(),choices.end(),[&](size_t group){return group!=*last_group;}))
            std::erase(choices,*last_group);
        auto group=choices[std::uniform_int_distribution<size_t>(0,choices.size()-1)(rng)];
        auto &g=groups[group];
        if(g.bag.empty()) {
            g.bag=g.members;std::shuffle(g.bag.begin(),g.bag.end(),rng);
            if(g.bag.size()>1&&g.last&&g.bag.back()==*g.last)std::swap(g.bag.back(),g.bag.front());
        }
        reserved=g.bag.back();return reserved;
    }
    void presented(size_t index) {
        if(index>=membership.size())return;
        const auto group=membership[index];auto &g=groups[group];
        // Explicit jumps/history restart a round. Natural/Next consume the reservation.
        if(!reserved||*reserved!=index)new_round();
        if(g.bag.empty()){g.bag=g.members;std::shuffle(g.bag.begin(),g.bag.end(),rng);}
        std::erase(g.bag,index);
        if(auto turn=std::find(round.begin(),round.end(),group);turn!=round.end())round.erase(turn);
        g.last=index;last_group=group;reserved.reset();
    }
    void reject(size_t index) {
        if(index>=membership.size())return;
        auto group=membership[index];auto &g=groups[group];
        std::erase(g.members,index);std::erase(g.bag,index);
        if(g.members.empty())std::erase(round,group);
        reserved.reset();
    }
};
}
