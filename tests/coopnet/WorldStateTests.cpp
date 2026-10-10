#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
#include <limits>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"World state failed at "<<line<<'\n'; std::exit(1); } }
#define require(value) check((value),__LINE__)
int main() {
    std::set<Identity> anchors;
    for (unsigned id=0;id<65536;++id) require(anchors.insert(world_anchor(123,static_cast<std::uint16_t>(id))).second);
    require(!anchors.count(0) && world_anchor(123,5)!=world_anchor(124,5));
    WorldState state{10,1,{{world_anchor(123,5),7,{1,2,3},{0,1,0},.75f}}},decoded;
    auto encoded=encode_world_state(state);
    require(decode_world_state(encoded,decoded) && decoded.objects[0].health==.75f);
    for (std::size_t n=0;n<encoded.size();++n) require(!decode_world_state({encoded.begin(),encoded.begin()+n},decoded));
    encoded.push_back(0); require(!decode_world_state(encoded,decoded));
    auto invalid=state; invalid.objects.push_back(invalid.objects.front()); require(!valid_world_state(invalid));
    invalid=state; invalid.objects.front().health=std::numeric_limits<float>::quiet_NaN(); require(!valid_world_state(invalid));
    invalid=state; invalid.objects.resize(129); require(!valid_world_state(invalid));
    auto zone=state; zone.objects.front().zone_state=2; zone.objects.front().zone_time=750;
    encoded=encode_world_state(zone); require(decode_world_state(encoded,decoded));
    require(decoded.objects.front().zone_state==2 && decoded.objects.front().zone_time==750);
    invalid=zone; invalid.objects.front().zone_state=5; require(!valid_world_state(invalid));
    invalid=zone; invalid.objects.front().zone_state=255; require(!valid_world_state(invalid));
    invalid=zone; invalid.objects.front().zone_time=0xffffffff; require(!valid_world_state(invalid));
    invalid=zone; invalid.objects.front().animations.push_back({}); require(!valid_world_state(invalid));
    auto animated=state; animated.objects.front().animations={{0,2,17,.5f,1.25f,false},{3,0,41,2.f,.75f,true}};
    encoded=encode_world_state(animated);
    require(decode_world_state(encoded,decoded));
    require(decoded.objects.front().animations.size()==2);
    const auto& motion=decoded.objects.front().animations.back();
    require(motion.part==3 && motion.slot==0 && motion.motion==41 && motion.time==2.f && motion.speed==.75f && motion.stop);
    for (std::size_t n=0;n<encoded.size();++n) require(!decode_world_state({encoded.begin(),encoded.begin()+n},decoded));
    invalid=animated; invalid.objects.front().animations.back().part=0; require(!valid_world_state(invalid));
    invalid=animated; invalid.objects.front().animations.front().time=std::numeric_limits<float>::quiet_NaN(); require(!valid_world_state(invalid));
    invalid=animated; invalid.objects.front().animations.front().speed=-1.f; require(!valid_world_state(invalid));
    invalid=animated; invalid.objects.front().animations.front().slot=256; require(!valid_world_state(invalid));
    invalid=animated; invalid.objects.front().animations.front().motion=65535; require(!valid_world_state(invalid));
    encoded.back()=2; require(!decode_world_state(encoded,decoded));
    auto maximum=state; maximum.objects.clear();
    for (unsigned i=0;i<128;++i) {
        auto object=state.objects.front(); object.anchor=world_anchor(123,static_cast<std::uint16_t>(i));
        for (unsigned part=0;part<4;++part) object.animations.push_back({static_cast<std::uint8_t>(part),0,1,0.f,1.f,false});
        maximum.objects.push_back(object);
    }
    encoded=encode_world_state(maximum); require(encoded.size()<16384 && decode_world_state(encoded,decoded));
    HostPump host; ClientPump client; auto links=MemoryTransport::pair(); Identity token=10;
    host.start(123,1,{1,1},[&] { return ++token; }); require(host.attach(1,std::move(links.second)));
    client.start(std::move(links.first),2,{1,1}); auto pump=[&] { host.update(.01); client.update(.01); };
    unsigned applied=0; client.set_world_sink([&](const WorldState& value) { applied+=static_cast<unsigned>(value.objects.size()); });
    for (unsigned n=0;n<10;++n) pump();
    require(host.publish_world_state(state)); pump(); require(!applied);
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(12),std::uint8_t(0)); WorldBaseline baseline{555,10,12,{}};
    client.set_baseline_sink([](const WorldBaseline&,const std::vector<std::uint8_t>&) { return true; });
    const auto player=client.session().welcome().player;
    require(host.send_baseline(player,baseline,bytes)); for (unsigned n=0;n<10;++n) pump();
    require(client.acknowledge_baseline()); pump();
    require(host.assign_level(player,10,666)); pump();
    require(host.publish_world_state(state)); pump(); require(!applied);
    require(client.acknowledge_level(10)); pump();
    require(host.publish_world_state(state)); pump(); require(applied==1);
    require(host.publish_world_state(state)); pump(); require(applied==1);
    state.tick=2; require(host.publish_world_state(state)); pump(); require(applied==2);
    state.tick=3; state.objects.front().incarnation=8; require(host.publish_world_state(state)); pump(); require(applied==2);
    state.level=11; state.objects.front().incarnation=7; require(host.publish_world_state(state)); pump(); require(applied==2);
    NPCRecord npc; npc.section="dog_weak"; npc.pose=state.objects.front();
    require(host.publish_shared_world(SharedKind::NPC,10,1,encode_npcs({npc}))); for (unsigned n=0;n<10;++n) pump();
    zone.tick=4; zone.objects.front().anchor=world_anchor(123,44);
    require(host.publish_world_state(zone)); pump(); require(applied==3);
    require(host.publish_world_state(zone)); pump(); require(applied==3);
    require(host.publish_shared_world(SharedKind::NPC,10,2,encode_npcs({npc}))); for (unsigned n=0;n<10;++n) pump();
    require(host.publish_world_state(zone)); pump(); require(applied==3);
    zone.tick=5; zone.objects.front().incarnation=8; require(host.publish_world_state(zone)); pump(); require(applied==3);
    zone.objects.front().incarnation=7; require(host.publish_world_state(zone)); pump(); require(applied==4);
    client.stop(); host.stop();
    std::cout<<"CoopNet NPC codec, anchor uniqueness, loading barrier, level isolation, duplicate and incarnation rejection passed\n";
}

