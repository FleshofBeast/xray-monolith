#pragma once
#include "ActorSnapshot.h"
#include <set>
namespace coopnet {
inline void write_world_float(Writer& writer,float value) { std::uint32_t bits; std::memcpy(&bits,&value,4); writer.integer(bits,4); }
inline bool read_world_float(Reader& reader,float& value) { std::uint64_t bits; if (!reader.integer(bits,4)) return false; const auto raw=static_cast<std::uint32_t>(bits); std::memcpy(&value,&raw,4); return true; }
// Baseline-local anchors are session-scoped identities, never engine object IDs.
inline Identity world_anchor(Identity session, std::uint16_t saved_object) {
    auto value=(session | (Identity(1)<<63)) ^ Identity(saved_object);
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
struct WorldAnimation {
    std::uint8_t part=0; std::uint16_t slot=0,motion=0;
    float time=0,speed=1; bool stop=false;
};
inline bool valid_world_animation(const WorldAnimation& a) {
    return a.part<4 && a.slot<256 && a.motion<65535 && std::isfinite(a.time) && a.time>=0 && a.time<=3600 &&
        std::isfinite(a.speed) && a.speed>=0 && a.speed<=10;
}
struct WorldPose {
    Identity anchor=0, incarnation=0;
    std::array<float,3> position{}, rotation{};
    float health=0;
    std::vector<WorldAnimation> animations;
    std::uint8_t zone_state=255;
    std::uint32_t zone_time=0;
};
struct WorldState {
    std::uint32_t level=0, tick=0;
    std::vector<WorldPose> objects;
};
inline bool valid_world_state(const WorldState& state) {
    if (!state.level || state.objects.empty() || state.objects.size()>128) return false;
    std::set<Identity> unique;
    for (const auto& object:state.objects) {
        if (!object.anchor || !object.incarnation || !unique.insert(object.anchor).second ||
            !std::isfinite(object.health) || object.health < -1 || object.health>1) return false;
        for (const auto& vector:{object.position,object.rotation})
            for (const auto value:vector) if (!std::isfinite(value) || std::abs(value)>1000000) return false;
        if (object.animations.size()>4) return false;
        if ((object.zone_state!=255 && object.zone_state>4) || object.zone_time>0x7fffffff ||
            (object.zone_state==255 && object.zone_time) || (object.zone_state!=255 && !object.animations.empty())) return false;
        std::set<unsigned> parts;
        for (const auto& animation:object.animations) if (!valid_world_animation(animation) || !parts.insert(animation.part).second) return false;
    }
    return true;
}
inline std::vector<std::uint8_t> encode_world_state(const WorldState& state) {
    if (!valid_world_state(state)) throw std::invalid_argument("Invalid world state");
    Writer writer; writer.integer(state.level,4); writer.integer(state.tick,4); writer.integer(state.objects.size(),2);
    for (const auto& object:state.objects) {
        writer.integer(object.anchor,8); writer.integer(object.incarnation,8);
        for (const auto& vector:{object.position,object.rotation}) for (const auto value:vector) {
            std::uint32_t bits; std::memcpy(&bits,&value,4); writer.integer(bits,4);
        }
        std::uint32_t bits; std::memcpy(&bits,&object.health,4); writer.integer(bits,4);
        writer.integer(object.zone_state,1); writer.integer(object.zone_time,4);
        writer.integer(object.animations.size(),1);
        for (const auto& a:object.animations) {
            writer.integer(a.part,1); writer.integer(a.slot,2); writer.integer(a.motion,2);
            write_world_float(writer,a.time); write_world_float(writer,a.speed); writer.integer(a.stop,1);
        }
    }
    return writer.bytes;
}
inline bool decode_world_state(const std::vector<std::uint8_t>& bytes,WorldState& output) {
    Reader reader(bytes); std::uint64_t level,tick,count;
    if (!reader.integer(level,4) || !reader.integer(tick,4) || !reader.integer(count,2) || !count || count>128 ||
        reader.remaining()<count*50) return false;
    WorldState state; state.level=static_cast<std::uint32_t>(level); state.tick=static_cast<std::uint32_t>(tick);
    state.objects.resize(static_cast<std::size_t>(count));
    for (auto& object:state.objects) {
        if (!reader.integer(object.anchor,8) || !reader.integer(object.incarnation,8)) return false;
        for (auto* vector:{&object.position,&object.rotation}) for (auto& value:*vector) {
            std::uint64_t bits; if (!reader.integer(bits,4)) return false;
            const auto number=static_cast<std::uint32_t>(bits); std::memcpy(&value,&number,4);
        }
        std::uint64_t bits; if (!reader.integer(bits,4)) return false;
        const auto number=static_cast<std::uint32_t>(bits); std::memcpy(&object.health,&number,4);
        std::uint64_t zone,state_time;
        if (!reader.integer(zone,1) || !reader.integer(state_time,4)) return false;
        object.zone_state=static_cast<std::uint8_t>(zone); object.zone_time=static_cast<std::uint32_t>(state_time);
        std::uint64_t animations; if (!reader.integer(animations,1) || animations>4) return false;
        for (unsigned i=0;i<animations;++i) {
            WorldAnimation a; std::uint64_t part,slot,motion,stop;
            if (!reader.integer(part,1) || !reader.integer(slot,2) || !reader.integer(motion,2) ||
                !read_world_float(reader,a.time) || !read_world_float(reader,a.speed) || !reader.integer(stop,1) || stop>1) return false;
            a.part=static_cast<std::uint8_t>(part); a.slot=static_cast<std::uint16_t>(slot); a.motion=static_cast<std::uint16_t>(motion); a.stop=stop!=0;
            object.animations.push_back(a);
        }
    }
    if (reader.remaining() || !valid_world_state(state)) return false;
    output=std::move(state); return true;
}
}
