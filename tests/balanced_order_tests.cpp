#include "balanced_order.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("balanced scheduler invariant failed");}
int main(){try{
    // Unequal pools: equal group frequency and separate without-replacement bags.
    for(unsigned count=1;count<=12;++count)for(unsigned seed=0;seed<20;++seed){
        nexus::BalancedOrder order(seed);std::vector<std::string> keys;std::vector<size_t> sizes;
        for(unsigned g=0;g<count;++g){sizes.push_back(g%5+1);for(size_t i=0;i<sizes.back();++i)keys.push_back(std::to_string(g));}
        order.reset(keys);std::vector<std::set<size_t>> seen(count);std::set<size_t> round;std::optional<size_t> last;
        for(unsigned i=0;i<1200;++i){auto next=order.peek();check(bool(next));check(order.peek()==next);auto group=size_t(std::stoul(keys[*next]));
            if(last&&count>1)check(*last!=group);check(round.insert(group).second);
            check(seen[group].insert(*next).second);if(seen[group].size()==sizes[group])seen[group].clear();
            order.presented(*next);last=group;if(round.size()==count)round.clear();
        }
    }
    for(unsigned seed=0;seed<20;++seed){
        nexus::BalancedOrder weighted(seed);
        const std::vector<std::string> keys={"People","People","People","Back","Back","Camera"};
        const std::map<std::string,int> quotas={{"People",3},{"Back",2},{"Camera",1}};
        weighted.reset(keys,quotas);
        for(int round=0;round<100;++round){
            std::map<std::string,int> counts;
            std::set<size_t> clips;
            for(int i=0;i<6;++i){
                auto next=weighted.peek();
                check(next&&weighted.peek()==next&&clips.insert(*next).second);
                ++counts[keys[*next]];
                weighted.presented(*next);
            }
            check(counts==quotas);
        }
        weighted.reset({"People","Camera"},{{"People",5}});
        for(int i=0;i<60;++i){auto next=weighted.peek();check(bool(next));weighted.presented(*next);}
    }
    nexus::BalancedOrder empty(1);empty.reset({});check(!empty.peek());
    nexus::BalancedOrder bad(2);bad.reset({"a","a","b","c"});bad.reject(2);bad.reject(3);
    for(int i=0;i<30;++i){auto value=bad.peek();check(value&&*value<2);bad.presented(*value);}
    bad.reject(0);bad.reject(1);check(!bad.peek());
    nexus::BalancedOrder manual(3);manual.reset({"a","b","c"});manual.presented(1);check(manual.peek()!=1);manual.presented(0);check(manual.peek()!=0);
    std::cout<<"PASS: 288000 selections, 1-12 groups, unequal bags, stable reservations, failures and manual jumps\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
