// Local character files only; no paths are accepted from network peers.
namespace {
bool read_character_path(const char* path,CharacterSave& saved) {
    auto* reader=FS.r_open(path); if(!reader) return false;
    const auto size=reader->length();
    if(size<40 || size>coopnet::shared_limit) {FS.r_close(reader);return false;}
    const auto* start=static_cast<const std::uint8_t*>(reader->pointer());
    std::vector<std::uint8_t> bytes(start,start+size); FS.r_close(reader);
    return decode_character_save(bytes,saved);
}
std::string find_character_path(std::uint64_t character,CharacterSave* output=nullptr) {
    string_path pattern; FS.update_path(pattern,"$game_saves$","*.coopchar");
    WIN32_FIND_DATAA found{}; HANDLE search=FindFirstFileA(pattern,&found);
    if(search==INVALID_HANDLE_VALUE) return {};
    std::string result;
    do {
        if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        string_path path; FS.update_path(path,"$game_saves$",found.cFileName);
        CharacterSave saved;
        if(read_character_path(path,saved) && saved.character==character) {result=path;if(output) *output=std::move(saved);break;}
    } while(FindNextFileA(search,&found));
    FindClose(search); return result;
}
bool backup_character_path(const char* path,const std::string& name) {
    if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) return true;
    string_path folder; FS.update_path(folder,"$game_saves$","coopnet-backups\\");
    if(!CreateDirectoryA(folder,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    SYSTEMTIME now; GetLocalTime(&now);
    for(unsigned attempt=0;attempt<100;++attempt) {
        char stamp[64]; xr_sprintf(stamp,"-%04u%02u%02u-%02u%02u%02u-%03u-%u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,attempt);
        const auto backup=std::string(folder)+name+stamp+".bak";
        if(CopyFileA(path,backup.c_str(),TRUE)) {
            FS.rescan_path(folder,FALSE);
            auto* previous=FS.r_open(path);auto* copied=FS.r_open(backup.c_str());
            const bool same=previous && copied && previous->length()==copied->length() &&
                !memcmp(previous->pointer(),copied->pointer(),previous->length());
            if(previous) FS.r_close(previous);if(copied) FS.r_close(copied);return same;
        }
        if(GetLastError()!=ERROR_FILE_EXISTS) return false;
    }
    return false;
}
}
bool read_character_save(std::uint64_t character,coopnet::InventoryView& inventory) {
    CharacterSave saved; if(find_character_path(character,&saved).empty()) return false;
    inventory=std::move(saved.inventory);inventory.actor=1;inventory.generation=inventory.level=inventory.revision=1;inventory.has_condition=true;inventory.health=saved.condition.health;inventory.power=saved.condition.power;inventory.radiation=saved.condition.radiation;
    Msg("* CoopNet saved character loaded: %s items %u rubles %u reputation %d",saved.name.c_str(),static_cast<unsigned>(inventory.items.size()),inventory.money,inventory.progress.reputation);
    return true;
}
bool write_character_save(std::uint64_t character,const std::string& name,const coopnet::InventoryView& inventory,const ActorConditionState& condition) {
    CharacterSave save{character,name,condition,inventory};
    save.inventory.actor=1;save.inventory.generation=save.inventory.level=save.inventory.revision=1;save.inventory.npc_disposition.clear();
    const auto active=save.inventory.active;save.inventory.active=0;
    for(std::size_t i=0;i<save.inventory.items.size();++i) {auto& item=save.inventory.items[i];if(item.item==active) save.inventory.active=i+1;item.item=i+1;item.revision=1;}
    if(!valid_character_save(save)) return false;
    const auto filename=character_save_name(name);
    string_path path; FS.update_path(path,"$game_saves$",filename.c_str());
    CharacterSave existing;
    if(read_character_path(path,existing) && existing.character!=character) {
        char suffix[32]; xr_sprintf(suffix,"-%016llx",character);
        const auto unique=filename.substr(0,filename.size()-9)+suffix+".coopchar";
        FS.update_path(path,"$game_saves$",unique.c_str());
    }
    const auto old=find_character_path(character);
    const auto partial=std::string(path)+".tmp";
    const auto bytes=encode_character_save(save);
    auto* writer=FS.w_open(partial.c_str()); if(!writer) return false;
    writer->w(bytes.data(),static_cast<u32>(bytes.size())); FS.w_close(writer);
    CharacterSave verified;
    if(!read_character_path(partial.c_str(),verified) || verified.character!=character) return false;
    // Abort replacement if the previous copy cannot be backed up.
    if(!backup_character_path(path,filename) || (!old.empty() && old!=path && !backup_character_path(old.c_str(),filename))) return false;
    if(!MoveFileExA(partial.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    string_path saves;FS.update_path(saves,"$game_saves$","");FS.rescan_path(saves,FALSE);
    if(!old.empty() && old!=path && !DeleteFileA(old.c_str())) Msg("! CoopNet old character filename could not be removed: %s",old.c_str());
    Msg("* CoopNet character exit save verified: %s items %u rubles %u reputation %d",path,static_cast<unsigned>(inventory.items.size()),inventory.money,inventory.progress.reputation);
    return true;
}

bool save_host_character_world(const std::string& name) {
    if(world_level_is_replica() || !g_actor || !g_pGameLevel || !Level().Server || !ai().get_alife()) return false;
    auto scope=engine_coopnet::host_save_source_scope();if(!scope) scope=guest_save_scope();
    const auto filename=character_save_name(name); const auto basename=filename.substr(0,filename.size()-9);
    char temporary[64]; xr_sprintf(temporary,"coopnet-exit-%016llx",GetTickCount64());
    auto* game=smart_cast<game_sv_Single*>(Level().Server->game);if(!game) return false;
    string_path previous;xr_strcpy(previous,g_last_saved_game);
    NET_Packet packet;packet.B.count=0;packet.r_pos=0;packet.w_stringZ(temporary);packet.w_u8(0);game->alife().save(packet);
    xr_strcpy(g_last_saved_game,previous);
    if(!CSavedGameWrapper::valid_saved_game(temporary)) return false;
    string_path source,target;CSavedGameWrapper::saved_game_full_name(temporary,source);CSavedGameWrapper::saved_game_full_name(basename.c_str(),target);
    string_path source_script,target_script;FS.update_path(source_script,"$game_saves$",(std::string(temporary)+".scoc").c_str());
    FS.update_path(target_script,"$game_saves$",(basename+".scoc").c_str());
    const bool script_exists=GetFileAttributesA(source_script)!=INVALID_FILE_ATTRIBUTES;
    if(!backup_character_path(target,basename+".scop") || (script_exists && !backup_character_path(target_script,basename+".scoc"))) return false;
    if(script_exists && !MoveFileExA(source_script,target_script,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    if(!MoveFileExA(source,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    string_path scope_path;FS.update_path(scope_path,"$game_saves$",(basename+".coopworld").c_str());
    const auto scope_partial=std::string(scope_path)+".tmp";
    auto* scope_writer=FS.w_open(scope_partial.c_str());if(!scope_writer) return false;
    scope_writer->w_u64(0x31574f43);scope_writer->w_u64(scope);FS.w_close(scope_writer);
    if(!backup_character_path(scope_path,basename+".coopworld") || !MoveFileExA(scope_partial.c_str(),scope_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
    string_path saves;FS.update_path(saves,"$game_saves$","");FS.rescan_path(saves,FALSE);
    Msg("* CoopNet host character world exit save verified: %s",target);return true;
}
