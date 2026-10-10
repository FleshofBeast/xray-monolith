#pragma once
#include "SharedWorld.h"
namespace coopnet {
struct RadioRecord {
    Identity id=0;
    std::uint64_t time=0;
    std::uint32_t show_time=5000;
    std::uint8_t type=0;
    std::string caption,text,texture;
};
inline bool valid_radio(const RadioRecord& n) {
    return n.id && n.type<=1 && n.show_time<=60000 && n.caption.size()<=1024 && n.text.size()<=4096 &&
        n.caption.find('\0')==std::string::npos && n.text.find('\0')==std::string::npos && (n.texture.empty() || shared_name(n.texture,192,true));
}
inline std::vector<std::uint8_t> encode_radio(const std::vector<RadioRecord>& news) {
    if(news.size()>256) throw std::length_error("Radio history limit");
    SharedWriter w; w.integer(news.size(),2); Identity previous=0;
    for(const auto& n:news) {
        if(!valid_radio(n) || n.id<=previous) throw std::invalid_argument("Invalid radio history"); previous=n.id;
        w.integer(n.id,8); w.integer(n.time,8); w.integer(n.show_time,4); w.integer(n.type,1);
        w.string(n.caption); w.string(n.text); w.string(n.texture);
    }
    return w.bytes;
}
inline bool decode_radio(const std::vector<std::uint8_t>& bytes,std::vector<RadioRecord>& output) {
    if(bytes.size()>shared_limit) return false; Reader r(bytes); std::uint64_t count,n;
    if(!r.integer(count,2) || count>256) return false;
    std::vector<RadioRecord> news; Identity previous=0;
    for(unsigned i=0;i<count;++i) {
        RadioRecord v;
        if(!r.integer(v.id,8) || !r.integer(v.time,8) || !r.integer(n,4)) return false; v.show_time=static_cast<std::uint32_t>(n);
        if(!r.integer(n,1)) return false; v.type=static_cast<std::uint8_t>(n);
        if(!shared_string(r,v.caption,1024) || !shared_string(r,v.text,4096) || !shared_string(r,v.texture,192) || !valid_radio(v) || v.id<=previous) return false;
        previous=v.id; news.push_back(std::move(v));
    }
    if(r.remaining()) return false; output=std::move(news); return true;
}
}
