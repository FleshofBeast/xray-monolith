#include "stdafx.h"
#include "Level.h"
#include "ai_space.h"
#include "level_graph.h"
#include "actor_defs.h"
#include "../CoopNet/EngineActorBridge.h"
#include "../xrEngine/irenderable.h"
#include "../xrEngine/vis_common.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include <atomic>
#include "Actor.h"
#include "../CoopNet/PlayerName.h"
#include "ui/UITextureMaster.h"
#include "../xrEngine/GameFont.h"
#include "HUDManager.h"
#include "../xrEngine/bone.h"
#include "CameraFirstEye.h"
namespace engine_coopnet {
namespace {
// Presentation only: no CActor/local-input/HUD/Lua/ALife ownership or engine object ID.
class RemoteActorVisual : public ISpatial, public IRenderable {
public:
    std::uint32_t generation;
    shared_str model;
    std::uint32_t updates = 0;
    std::atomic<std::uint32_t> renders{0};
    MotionID legs, torso, head;
    IRenderVisual* weapon=nullptr;
    Fmatrix weapon_transform;
    coopnet::ActorAppearance appearance;
    std::atomic<std::uint32_t> weapon_renders{0};
    bool weapon_attached=false;
    unsigned probe_shots=0;
    RemoteActorVisual(const RemoteActorPose& pose) : ISpatial(g_SpatialSpace), generation(pose.generation), model(pose.appearance.body.empty()?pose.visual:pose.appearance.body.c_str()) {
        renderable.visual = Render->model_Create(model.c_str());
        spatial.type = STYPE_RENDERABLE;
        if (auto* skeleton = renderable.visual->dcast_PKinematics()) skeleton->spatialParent = this;
        weapon_transform.identity(); legs.invalidate(); torso.invalidate(); head.invalidate();
        apply(pose); spatial_register();
    }
    ~RemoteActorVisual() override {
        if(weapon)Render->model_Delete(weapon);
        spatial_unregister();
        if (auto* skeleton = renderable.visual->dcast_PKinematics()) skeleton->spatialParent = nullptr;
    }
    IRenderable* dcast_Renderable() override { return this; }
    bool canOptimizeCalculateBones() override { return false; }
    void animate(IKinematicsAnimated& animated, std::uint16_t movement) {
        using namespace ACTOR_DEFS;
        const bool crouched = (movement & mcCrouch) != 0;
        const bool climbing = !crouched && (movement & mcClimb) != 0;
        const char* base = crouched ? "cr" : climbing ? "cl" : "norm";
        // The engine's acceleration modifier selects walking when set.
        const bool running = !(movement & mcAccel) || (movement & mcSprint);
        const char* suffix = climbing ? "_idle_1" : crouched && !running ? "_idle_1" : "_idle_0";
        if (movement & mcLanding) suffix = "_jump_end";
        else if (movement & mcLanding2) suffix = "_jump_end_1";
        else if ((movement & mcTurn) && !climbing) suffix = "_turn";
        else if (movement & mcFall) suffix = "_jump_idle";
        else if (movement & mcJump) suffix = "_jump_begin";
        else if (movement & mcFwd) suffix = running || climbing ? "_run_fwd_0" : "_walk_fwd_0";
        else if (movement & mcBack) suffix = running || climbing ? "_run_back_0" : "_walk_back_0";
        else if (movement & mcLStrafe) suffix = running || climbing ? "_run_ls_0" : "_walk_ls_0";
        else if (movement & mcRStrafe) suffix = running || climbing ? "_run_rs_0" : "_walk_rs_0";
        string128 name;
        strconcat(sizeof(name), name, base, suffix);
        auto nextLegs = animated.ID_Cycle_Safe(name);
        if (!nextLegs.valid()) nextLegs = animated.ID_Cycle_Safe("norm_idle_0");
        xr_sprintf(name,"%s_torso_%u_aim_0",base,weapon && appearance.animation?appearance.animation:0);
        auto nextTorso = animated.ID_Cycle_Safe(name);
        if (!nextTorso.valid()) nextTorso = animated.ID_Cycle_Safe("norm_torso_0_aim_0");
        const auto nextHead = animated.ID_Cycle_Safe("head_idle_0");
        if (nextTorso.valid() && torso != nextTorso) { animated.PlayCycle(nextTorso, TRUE); torso = nextTorso; }
        if (nextHead.valid() && head != nextHead) { animated.PlayCycle(nextHead, TRUE); head = nextHead; }
        if (nextLegs.valid() && legs != nextLegs) { animated.PlayCycle(nextLegs, TRUE); legs = nextLegs; }
        animated.UpdateTracks();
    }
    void apply(const RemoteActorPose& pose) {
        ++updates;
        if(!pose.appearance.body.empty() && (appearance.body.empty() || coopnet::encode_appearance(appearance)!=coopnet::encode_appearance(pose.appearance))) {
            if(appearance.weapon!=pose.appearance.weapon) {
                if(weapon)Render->model_Delete(weapon);
                if(!pose.appearance.weapon.empty()) {
                    string_path asset;xr_strcpy(asset,pose.appearance.weapon.c_str());if(!strstr(asset,".ogf"))xr_strcat(asset,".ogf");
                    if(FS.exist("$game_meshes$",asset))weapon=Render->model_Create(pose.appearance.weapon.c_str());
                }
            }
            appearance=pose.appearance;
            auto visibility=[](IRenderVisual* visual,const std::vector<std::string>& hidden) {
                auto* k=visual?visual->dcast_PKinematics():nullptr;if(!k)return;
                for(u16 n=0;n<k->LL_BoneCount();++n)k->LL_SetBoneVisible(n,TRUE,FALSE);
                for(const auto& name:hidden){const auto bone=k->LL_BoneID(name.c_str());if(bone!=BI_NONE)k->LL_SetBoneVisible(bone,FALSE,FALSE);}
                k->CalculateBones_Invalidate();
            };
            visibility(renderable.visual,appearance.hidden_body);visibility(weapon,appearance.hidden_weapon);
            Msg("* CoopNet remote appearance applied: body %s weapon %s animation %u hidden attachments %u",model.c_str(),appearance.weapon.c_str(),appearance.animation,static_cast<unsigned>(appearance.hidden_weapon.size()));
        }
        renderable.xform.setHPB(-pose.rotation[1], pose.rotation[0], pose.rotation[2]);
        renderable.xform.c.set(pose.position[0],pose.position[1],pose.position[2]);
        if(strstr(GetCommandLineA(),"-coop_appearance_probe") && g_actor) {
            Fvector target=renderable.xform.c;target.y+=1.2f;
            static_cast<CCameraFirstEye*>(g_actor->cam_FirstEye())->LookAtPoint(target);
            if(weapon && weapon_renders.load(std::memory_order_relaxed)>100 && probe_shots<2) {
                Render->Screenshot(IRender_interface::SM_NORMAL);++probe_shots;
                Msg("* CoopNet appearance probe screenshot: body %s weapon %s",model.c_str(),appearance.weapon.c_str());
            }
        }
        if (auto* animated = renderable.visual->dcast_PKinematicsAnimated()) animate(*animated, pose.movement);
        if (auto* skeleton = renderable.visual->dcast_PKinematics()) skeleton->CalculateBones(TRUE);
        weapon_attached=false;
        if(weapon) {
            auto* k=renderable.visual->dcast_PKinematics();
            const auto left=k?k->LL_BoneID((appearance.one_hand?appearance.one_hand_bone:appearance.left_bone).c_str()):BI_NONE;
            const auto right=k?k->LL_BoneID(appearance.right_bone.c_str()):BI_NONE;
            if(k && left!=BI_NONE && right!=BI_NONE) {
                const auto& l=k->LL_GetTransform(left);const auto& r=k->LL_GetTransform(right);
                Fmatrix grip;Fvector d,side,up;d.sub(l.c,r.c);
                if(d.square_magnitude()>EPS_S && _valid(d)) {
                    d.normalize();side.crossproduct(r.j,d);
                    if(side.square_magnitude()>EPS_S) {side.normalize();up.crossproduct(d,side);up.normalize();grip.set(side,up,d,r.c);}
                    else {grip=r;}
                } else {grip=r;}
                Fmatrix offset;offset.setHPB(appearance.offset[3],appearance.offset[4],appearance.offset[5]);offset.c.set(appearance.offset[0],appearance.offset[1],appearance.offset[2]);
                Fmatrix world;world.mul_43(renderable.xform,grip);weapon_transform.mul_43(world,offset);weapon_attached=true;
                if(auto* animated=weapon->dcast_PKinematicsAnimated())animated->UpdateTracks();
                if(auto* skeleton=weapon->dcast_PKinematics())skeleton->CalculateBones(TRUE);
            }
        }
        const auto& sphere = renderable.visual->getVisData().sphere;
        renderable.xform.transform_tiny(spatial.sphere.P,sphere.P);
        spatial.sphere.R = sphere.R+(weapon?1.f:0.f);
        spatial_move();
    }
    void renderable_Render() override {
        renders.fetch_add(1,std::memory_order_relaxed);
        Render->set_Transform(&renderable.xform); Render->add_Visual(renderable.visual);
        renderable.visual->getVisData().hom_frame = Device.dwFrame;
        if(weapon && weapon_attached) {
            Render->set_Transform(&weapon_transform);Render->add_Visual(weapon);weapon->getVisData().hom_frame=Device.dwFrame;
            const auto rendered=weapon_renders.fetch_add(1,std::memory_order_relaxed)+1;
            if(rendered==1 || rendered%600==0)Msg("* CoopNet remote held weapon rendered: %s submissions %u",appearance.weapon.c_str(),rendered);
        }
    }
    BOOL renderable_ShadowGenerate() override { return TRUE; }
    BOOL renderable_ShadowReceive() override { return TRUE; }
};
xr_map<std::uint64_t, RemoteActorVisual*> visuals;
std::vector<PlayerNameplate> nameplates;
ui_shader* nameplate_shader=nullptr;
Frect nameplate_texel;
}
void set_player_nameplates(const std::vector<PlayerNameplate>& labels) {
    nameplates=labels;
    if(labels.empty()) xr_delete(nameplate_shader);
}
void draw_player_nameplates() {
    if(nameplates.empty() || !g_pGameLevel || !g_pGameLevel->bReady || !g_actor) return;
    auto* font=UI().Font().pFontLetterica16Russian; if(!font) return;
    if(!nameplate_shader) {
        nameplate_shader=xr_new<ui_shader>();
        CUITextureMaster::InitTexture("ui_inGame2_white_rect","hud\\default",*nameplate_shader,nameplate_texel);
    }
    UIRender->SetShader(**nameplate_shader);
    Fvector2 texture_size; UIRender->GetActiveTextureResolution(texture_size);
    if(texture_size.x<=0 || texture_size.y<=0) return;
    const float u=(nameplate_texel.x1+nameplate_texel.x2)*.5f/texture_size.x;
    const float v=(nameplate_texel.y1+nameplate_texel.y2)*.5f/texture_size.y;
    const float scale=Device.dwHeight/768.f;
    auto circle=[&](float x,float y,float radius,u32 color) {
        UIRender->SetShader(**nameplate_shader);
        UIRender->StartPrimitive(60,IUIRender::ptTriList,IUIRender::pttTL);
        for(unsigned i=0;i<20;++i) {
            const float a=i*PI_MUL_2/20.f,b=(i+1)*PI_MUL_2/20.f;
            UIRender->PushPoint(x,y,0,color,u,v);
            UIRender->PushPoint(x+cosf(a)*radius,y+sinf(a)*radius,0,color,u,v);
            UIRender->PushPoint(x+cosf(b)*radius,y+sinf(b)*radius,0,color,u,v);
        }
        UIRender->FlushPrimitive();
    };
    for(const auto& label:nameplates) {
        if(!coopnet::valid_player_name(label.name)) continue;
        Fvector point; point.set(label.position[0],label.position[1]+1.95f,label.position[2]);
        IKinematics* skeleton=nullptr; Fmatrix transform;
        if(label.object!=0xffff) {
            auto* actor=smart_cast<CActor*>(Level().Objects.net_Find(label.object));
            if(!actor || actor==g_actor || actor->getDestroy()) continue;
            transform=actor->XFORM(); if(actor->Visual()) skeleton=actor->Visual()->dcast_PKinematics();
        } else {
            const auto visual=visuals.find(label.entity); if(visual==visuals.end()) continue;
            transform=visual->second->renderable.xform; skeleton=visual->second->renderable.visual->dcast_PKinematics();
        }
        if(skeleton) {
            const auto bone=skeleton->LL_BoneID("bip01_head");
            if(bone!=BI_NONE) { skeleton->CalculateBones(TRUE); transform.transform_tiny(point,skeleton->LL_GetTransform(bone).c); point.y+=.30f; }
        }
        Fvector4 projected; Device.mFullTransform.transform(projected,point);
        if(projected.w<=EPS || projected.z<0 || projected.z>1 || !_valid(projected.x) || !_valid(projected.y) ||
            projected.x<-1 || projected.x>1 || projected.y<-1 || projected.y>1) continue;
        const float width=font->SizeOf_(label.name.c_str());
        const float x=(1+projected.x)*.5f*Device.dwWidth-(width+16*scale)*.5f;
        const float y=(1-projected.y)*.5f*Device.dwHeight-font->CurrentHeight_();
        const u32 colors[]={color_rgba(0,0,0,255),color_rgba(255,45,45,255),color_rgba(255,135,0,255),color_rgba(255,225,0,255),color_rgba(55,225,75,255)};
        circle(x+5*scale,y+font->CurrentHeight_()*.5f,5*scale,color_rgba(230,230,230,255));
        circle(x+5*scale,y+font->CurrentHeight_()*.5f,4*scale,colors[static_cast<unsigned>(coopnet::health_color(label.health))]);
        font->SetAligment(CGameFont::alLeft);
        font->SetColor(color_rgba(0,0,0,220)); font->Out(x+16*scale+1,y+1,"%s",label.name.c_str());
        font->SetColor(color_rgba(255,255,255,255)); font->Out(x+16*scale,y,"%s",label.name.c_str());
        static unsigned rendered=0; if(++rendered==1 || rendered%600==0) Msg("* CoopNet teammate nameplate drawn: name %s health %.3f color %u",label.name.c_str(),label.health,static_cast<unsigned>(coopnet::health_color(label.health)));
    }
}
void remove_remote_actor(std::uint64_t entity) {
    const auto found = visuals.find(entity);
    if (found == visuals.end()) return;
    auto* visual = found->second; visuals.erase(found);
    Msg("* CoopNet remote actor visual removed: updates %u render submissions %u",visual->updates,
        visual->renders.load(std::memory_order_relaxed));
    xr_delete(visual);
}
void clear_remote_actors() {
    while (!visuals.empty()) remove_remote_actor(visuals.begin()->first);
}
bool present_remote_actor(const RemoteActorPose& pose) {
    if (!g_pGameLevel || !g_pGameLevel->bReady || !ai().get_level_graph() ||
        pose.level != static_cast<std::uint32_t>(ai().level_graph().level_id()) + 1 || !pose.visual[0]) return false;
    const char* body=pose.appearance.body.empty()?pose.visual:pose.appearance.body.c_str();
    auto found = visuals.find(pose.entity);
    if (found != visuals.end() && (found->second->generation != pose.generation || found->second->model != body)) {
        remove_remote_actor(pose.entity); found = visuals.end();
    }
    if (found == visuals.end()) {
        if (visuals.size() >= 4) return false;
        string_path asset; xr_strcpy(asset,body);
        if (!strstr(asset,".ogf")) xr_strcat(asset,".ogf");
        if (!FS.exist("$game_meshes$",asset)) return false;
        visuals.emplace(pose.entity,xr_new<RemoteActorVisual>(pose));
        Msg("* CoopNet remote actor visual created: generation %u level %u", pose.generation,pose.level);
    } else found->second->apply(pose);
    return true;
}
}
