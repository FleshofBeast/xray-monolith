#pragma once
#include <vector>
#include <string>
namespace engine_coopnet {
// Plain-text bridge keeps the exception-enabled transport TU out of xrCore headers.
void report(const char* text);
void command(const char* name, const char* arguments);
void update(double elapsed);
void stop();
bool available();
bool guest_settings_locked();
bool world_setting_command(const char* command);
void register_world_setting_command(const char* command);
bool host_settings_application();
void applying_host_settings(bool value);
bool join_from_menu(const char* address);
bool queue_character_join(const char* address,bool create);
void cancel_character_join();
void saved_join_address(char* output,unsigned capacity);
void join_status(char* output,unsigned capacity);
bool take_version_mismatch(char* output,unsigned capacity);
unsigned long long host_save_source_scope();
unsigned long long guest_character_identity(unsigned short object);
bool simulation_active();
bool shared_world_active();
bool party_level_change_allowed();
bool party_controls_enabled();
bool player_downed();
bool can_respawn();
bool request_respawn();
struct SpectatorPlayer { unsigned long long entity=0; unsigned generation=0; std::string name; bool alive=false; };
std::vector<SpectatorPlayer> spectator_players();
bool select_spectator(unsigned long long entity,unsigned generation);
void clear_spectator();
unsigned long long spectator_target();
bool spectator_pose(float* position,float* rotation);
void respawn_status(char* output,unsigned capacity);
}
