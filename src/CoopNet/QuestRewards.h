#pragma once
#include "GuestSave.h"
#include <map>
namespace engine_coopnet {
enum class RewardJournalResult { Applied, AlreadyApplied, Full, Invalid };
inline RewardJournalResult journal_quest_reward(GuestInventoryState& output,const SharedQuestReward& reward) {
    auto state=output;
    if(!reward.id || !coopnet::valid_progress(reward.before) || !coopnet::valid_progress(reward.after)) return RewardJournalResult::Invalid;
    if(std::find(state.rewards.begin(),state.rewards.end(),reward.id)!=state.rewards.end()) return RewardJournalResult::AlreadyApplied;
    if(state.items.size()+reward.items.size()>256 || state.rewards.size()>=65535) return RewardJournalResult::Full;
    for(const auto& item:reward.items) if(item.section.empty() || item.section.size()>128 || item.spawn.empty() || item.spawn.size()>=16384) return RewardJournalResult::Invalid;
    const auto bounded=[](std::int64_t value) {return static_cast<std::int32_t>(std::clamp(value,std::int64_t(-1000000),std::int64_t(1000000)));};
    state.money=static_cast<std::uint32_t>((std::min)(std::uint64_t(state.money)+reward.money,std::uint64_t(UINT32_MAX))); state.has_money=true;
    if(reward.before.present && reward.after.present) {
        state.progress.present=true;
        state.progress.reputation=bounded(std::int64_t(state.progress.reputation)+reward.after.reputation-reward.before.reputation);
        state.progress.rank=bounded(std::int64_t(state.progress.rank)+reward.after.rank-reward.before.rank);
        const auto merge=[&](auto& target,const auto& before,const auto& after) {
            std::map<std::string,std::int64_t> values,deltas;
            for(const auto& v:target) values[v.first]=v.second;
            for(const auto& v:before) deltas[v.first]-=v.second;
            for(const auto& v:after) deltas[v.first]+=v.second;
            for(const auto& v:deltas) values[v.first]+=v.second;
            target.clear(); for(const auto& v:values) target.emplace_back(v.first,bounded(v.second));
        };
        merge(state.progress.factions,reward.before.factions,reward.after.factions);
        merge(state.progress.story,reward.before.story,reward.after.story);
    }
    if(!coopnet::valid_progress(state.progress)) return RewardJournalResult::Invalid;
    state.items.insert(state.items.end(),reward.items.begin(),reward.items.end()); state.rewards.push_back(reward.id);
    output=std::move(state); return RewardJournalResult::Applied;
}
}
