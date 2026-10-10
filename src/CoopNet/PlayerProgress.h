#pragma once
#include "Protocol.h"
#include <string>
#include <set>
#include <cstring>
namespace coopnet {
struct PlayerProgress {
    bool present=false;
    std::int32_t reputation=0,rank=0;
    std::vector<std::pair<std::string,std::int32_t>> factions,story;
};
inline bool operator==(const PlayerProgress& a,const PlayerProgress& b) {
    return a.present==b.present && a.reputation==b.reputation && a.rank==b.rank && a.factions==b.factions && a.story==b.story;
}
inline bool valid_progress(const PlayerProgress& p) {
    if(p.reputation < -1000000 || p.reputation>1000000 || p.rank< -1000000 || p.rank>1000000 || p.factions.size()>64 || p.story.size()>128) return false;
    if(!p.present && (p.reputation || p.rank || !p.factions.empty() || !p.story.empty())) return false;
    for(const auto* list:{&p.factions,&p.story}) {
        std::set<std::string> seen;
        for(const auto& v:*list) {
            if(v.first.empty() || v.first.size()>64 || v.second < -1000000 || v.second>1000000 || !seen.insert(v.first).second) return false;
            for(unsigned char c:v.first) if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_')) return false;
        }
    }
    return true;
}
inline void write_progress(Writer& w,const PlayerProgress& p) {
    if(!valid_progress(p)) throw std::invalid_argument("Invalid player progress");
    w.integer(p.present,1); w.integer(static_cast<std::uint32_t>(p.reputation),4); w.integer(static_cast<std::uint32_t>(p.rank),4);
    for(const auto* list:{&p.factions,&p.story}) {
        w.integer(list->size(),1);
        for(const auto& v:*list) {w.integer(v.first.size(),1); for(unsigned char c:v.first) w.integer(c,1); w.integer(static_cast<std::uint32_t>(v.second),4);}
    }
}
inline bool read_progress(Reader& r,PlayerProgress& output) {
    PlayerProgress p; std::uint64_t n,c;
    if(!r.integer(n,1) || n>1) return false; p.present=n!=0;
    for(auto* v:{&p.reputation,&p.rank}) {if(!r.integer(n,4)) return false; const auto bits=static_cast<std::uint32_t>(n); std::memcpy(v,&bits,4);}
    for(auto* list:{&p.factions,&p.story}) {
        if(!r.integer(n,1) || n>(list==&p.factions?64u:128u)) return false; const auto count=n;
        for(unsigned i=0;i<count;++i) {
            if(!r.integer(n,1) || !n || n>64 || n>r.remaining()) return false;
            std::string key; const auto length=n;
            for(unsigned k=0;k<length;++k) {if(!r.integer(c,1)) return false; key.push_back(static_cast<char>(c));}
            if(!r.integer(n,4)) return false; const auto bits=static_cast<std::uint32_t>(n); std::int32_t value; std::memcpy(&value,&bits,4);
            list->emplace_back(std::move(key),value);
        }
    }
    if(!valid_progress(p)) return false; output=std::move(p); return true;
}
}
