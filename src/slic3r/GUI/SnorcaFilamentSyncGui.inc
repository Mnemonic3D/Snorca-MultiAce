// Included by Plater.cpp after Sidebar::priv. Native GUI-thread transaction only.
static nlohmann::json snorca_palette_state()
{
    using J=nlohmann::json;
    auto* bundle=wxGetApp().preset_bundle;
    J rows=J::array();
    const auto* colors=bundle->project_config.option<ConfigOptionStrings>("filament_colour");
    const auto* nozzles=bundle->project_config.option<ConfigOptionInts>("fos_filament_nozzle");
    const auto* slots=bundle->project_config.option<ConfigOptionInts>("fos_filament_mms_slot");
    const auto* sync=bundle->project_config.option<ConfigOptionStrings>("fos_filament_sync");
    for(size_t i=0;i<bundle->filament_presets.size();++i){
        J own=J::object();
        if(sync&&i<sync->values.size()&&!sync->values[i].empty())own=J::parse(sync->values[i]);
        rows.push_back({{"preset",bundle->filament_presets[i]},
            {"color",colors&&i<colors->values.size()?colors->values[i]:"#808080"},
            {"nozzle",nozzles&&i<nozzles->values.size()?nozzles->values[i]:-1},
            {"mms_slot",slots&&i<slots->values.size()?slots->values[i]:0},{"ownership",own}});
    }
    auto serialized=[](const DynamicPrintConfig& config){J values=J::object();for(const auto& key:config.keys())if(const auto* option=config.option(key))values[key]=option->serialize();return values;};
    return J{{"rows",rows},{"multicolor",bundle->ams_multi_color_filment},{"project_configuration",serialized(bundle->project_config)},
        {"edited_printer_configuration",serialized(bundle->printers.get_edited_preset().config)},
        {"edited_filament_configuration",serialized(bundle->filaments.get_edited_preset().config)},
        {"has_objects",!wxGetApp().plater()->model().objects.empty()},
        {"model_id",wxGetApp().plater()->model().id().id},
        {"undo_revision",wxGetApp().plater()->undo_redo_stack_main().active_snapshot_time()},
        {"project",into_u8(wxGetApp().plater()->get_project_filename())},
        {"printer",bundle->printers.get_edited_preset().name}};
}

static nlohmann::json snorca_compatible_filaments()
{
    using J=nlohmann::json;J result=J::array();auto* bundle=wxGetApp().preset_bundle;
    for(const auto& preset:bundle->filaments.get_presets()){
        if(!preset.is_compatible||!preset.is_visible)continue;
        const auto* type=preset.config.option<ConfigOptionStrings>("filament_type");
        if(!type||type->values.empty())continue;
        result.push_back({{"name",preset.name},{"material",SnorcaFilamentSync::detail::upper(type->values[0])},
            {"generic",preset.name.find("Generic ")!=std::string::npos}});
    }
    return result;
}

static std::string snorca_empty_palette_fingerprint(const nlohmann::json& state)
{
    using J=nlohmann::json;
    J settings=state.at("project_configuration");
    // These arrays are derived from per-nozzle process presets during deferred UI
    // refresh. All other project fields, including newly introduced fields, guard
    // cleanup eligibility so existing manual work cannot silently be discarded.
    for(auto it=settings.begin();it!=settings.end();)
        if(it.key().rfind("fos_nozzle_",0)==0)it=settings.erase(it);else ++it;
    return J{{"rows",state.at("rows")},{"multicolor",state.at("multicolor")},{"has_objects",state.at("has_objects")},
        {"model_id",state.at("model_id")},{"project",state.at("project")},{"user_project_settings",settings},
        {"edited_filament_configuration",state.at("edited_filament_configuration")}}.dump();
}

// Captured once after initial GUI setup and after an accepted New Project reset.
// Auto may replace unused startup rows only while this exact state is untouched.
static bool snorca_legacy_cleanup_grant_matches(const nlohmann::json& state)
{
    using J=nlohmann::json;
    try{const auto raw=wxGetApp().app_config->get("snorca_filament_startup_replace_v1");if(raw.empty()||raw.size()>65536)return false;
        const auto grant=J::parse(raw);J presets=J::array(),colors=J::array();for(const auto& row:state.at("rows")){presets.push_back(row.at("preset"));colors.push_back(row.at("color"));}
        const auto* host=wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionString>("mms_host");
        return grant.at("version")==1&&grant.at("printer")==state.at("printer")&&grant.at("host")==SnorcaFilamentSync::printer_address(host?host->value:std::string())&&grant.at("presets")==presets&&grant.at("colors")==colors;
    }catch(const std::exception&){return false;}
}
static nlohmann::json snorca_repair_context(const nlohmann::json& state)
{
    using J=nlohmann::json;J settings=state.at("project_configuration");
    for(auto it=settings.begin();it!=settings.end();)
        if(it.key().rfind("fos_nozzle_",0)==0)it=settings.erase(it);else ++it;
    return J{{"multicolor",state.at("multicolor")},{"project_configuration",settings},
        {"edited_filament_configuration",state.at("edited_filament_configuration")}};
}
// One-time repair of an explicitly verified failed empty palette. A complete row
// fingerprint and target paired identity are required; no manual/unowned row qualifies.
static bool snorca_verified_empty_repair(const nlohmann::json& state,const std::string& address,const std::string& serial)
{
    using J=nlohmann::json;
    try{
        if(state.at("has_objects").get<bool>()||!state.at("project").get<std::string>().empty())return false;
        const auto raw=wxGetApp().app_config->get("snorca_filament_repair_v1");if(raw.empty()||raw.size()>131072)return false;
        const auto grant=J::parse(raw);
        if(grant.at("version")!=1||grant.at("host")!=address||grant.at("printer_id")!=serial||grant.at("rows")!=state.at("rows"))return false;
        if(grant.at("context")!=snorca_repair_context(state))return false;
        if(state.at("rows").empty())return false;
        std::string source;
        for(const auto& row:state.at("rows")){
            const auto& own=row.at("ownership");if(own.empty())return false;
            const auto id=own.at("printer_id").get<std::string>();if(id.empty()||id==serial)return false;
            if(source.empty())source=id;else if(source!=id)return false;
        }
        return true;
    }catch(const std::exception&){return false;}
}
void Sidebar::reset_snorca_filament_sync_baseline(bool explicit_new_project)
{
    p->snorca_sync_empty_baseline.clear();
    try{const auto state=snorca_palette_state();
        if(state.at("has_objects").get<bool>()||!state.at("project").get<std::string>().empty())return;
        bool all_owned=!state.at("rows").empty();for(const auto& row:state.at("rows"))if(row.at("ownership").empty())all_owned=false;
        // Restored manual palettes are NOT untouched defaults merely because the plate
        // is empty. Only the explicitly approved legacy snapshot can be cleaned once.
        if(!explicit_new_project&&!all_owned&&!snorca_legacy_cleanup_grant_matches(state))return;
        p->snorca_sync_empty_baseline=snorca_empty_palette_fingerprint(state);
    }catch(const std::exception&){} // Failure means conservative route adoption, never cleanup.
}

// A saved, paired printer explicitly selected in Prepare is the sync target.
struct SnorcaSelectedPrinter {std::string address,serial,preset;DeviceInfo device;bool gateway{false};std::uint64_t gateway_generation{0};};
static SnorcaSelectedPrinter snorca_selected_printer()
{
    SnorcaSelectedPrinter result;const auto& preset=wxGetApp().preset_bundle->printers.get_edited_preset();result.preset=preset.name;
    const auto devices = wxGetApp().app_config->get_devices();
    const auto* option=preset.config.option<ConfigOptionString>("mms_host");
    if(!option||option->value.empty())throw std::runtime_error("Selected printer has no saved IP. Set its multiACE address once.");
    result.address=SnorcaFilamentSync::printer_address(option->value);
    if (wxGetApp().gateway_device_connected()) {
        const auto connected_address = wxGetApp().gateway_lan_address();
        if (connected_address.empty())
            throw std::runtime_error(wxGetApp().gateway_address_pending()
                ? "Waiting for the printer's LAN address. Auto sync will retry; existing filaments kept."
                : "Printer connected, but its LAN address is unavailable. Check LAN mode in Device; existing filaments kept.");
        if (connected_address != result.address)
            throw std::runtime_error("Prepare and Device use different printer addresses. Prepare: " + result.address +
                "; Device: " + connected_address +
                ". Select the same printer or correct its multiACE address; existing filaments kept.");
        result.gateway = true;
        result.gateway_generation = wxGetApp().gateway_device_generation();
        result.serial = wxGetApp().gateway_serial_number();
        return result;
    }
    unsigned matches=0;for(const auto& device:devices){
        std::string address;try{address=SnorcaFilamentSync::printer_address(device.ip);}catch(const std::exception&){continue;}
        if(address==result.address){result.serial=device.sn;result.device=device;result.device.ip=address;++matches;}
    }
    if(matches!=1||result.serial.empty())throw std::runtime_error("Waiting for a matching LAN printer. Select this printer in Device; Auto sync retries. Existing filaments kept.");
    return result;
}

// SNORCA (2026-11-12): the per-object filament settings: "extruder" and every integer "*_filament" option (FOS feature
// filaments wall_filament, inner_wall_filament, sparse_infill_filament, solid_infill_filament; support filaments). // SNORCA
static std::vector<std::string> snorca_filament_keys(const ModelConfig& c) // SNORCA
{ // SNORCA
    std::vector<std::string> keys; // SNORCA
    for(const auto& k:c.keys()){const auto* o=c.option(k); // SNORCA
        if(o&&o->type()==coInt&&(k=="extruder"||(k.size()>9&&k.compare(k.size()-9,9,"_filament")==0)))keys.push_back(k);} // SNORCA
    return keys; // SNORCA
} // SNORCA
// SNORCA (2026-11-12): 0-based filament rows used by objects: object/part/layer-range filament settings, painted colours.
// SNORCA (2026-11-14): the physical filaments (1-based) a mixed filament prints with: its pattern tokens when it has a
// pattern, else its gradient components, else its pair (same order upstream resolves them in). // SNORCA
static std::vector<unsigned> snorca_mixed_components(const MixedFilament& mf,size_t n_physical) // SNORCA
{ // SNORCA
    std::vector<unsigned> ids; // SNORCA
    const std::string norm=MixedFilamentManager::normalize_manual_pattern(mf.manual_pattern); // SNORCA
    if(!norm.empty()){for(const auto& g:MixedFilamentManager::split_pattern_groups(norm))for(const auto& t:MixedFilamentManager::split_pattern_group_to_tokens(g,n_physical)) // SNORCA
        ids.push_back(MixedFilamentManager::physical_filament_from_token(t,mf,n_physical));return ids;} // SNORCA
    ids=MixedFilamentManager::decode_gradient_component_ids(mf.gradient_component_ids,0); // SNORCA
    ids.push_back(mf.component_a);ids.push_back(mf.component_b);return ids; // SNORCA
} // SNORCA
static nlohmann::json snorca_used_rows() // SNORCA
{ // SNORCA
    std::set<int> used; // SNORCA
    auto add=[&](const ModelConfig& c){for(const auto& k:snorca_filament_keys(c))if(c.opt_int(k)>0)used.insert(c.opt_int(k)-1);}; // SNORCA
    for(const auto* o:wxGetApp().plater()->model().objects){add(o->config);for(const auto& r:o->layer_config_ranges)add(r.second); // SNORCA
        for(const auto* v:o->volumes){add(v->config);for(int e:v->get_extruders())if(e>0)used.insert(e-1);}} // SNORCA
    // SNORCA (2026-11-14): a used mixed (virtual) filament is not a row; it uses its physical components instead.
    const auto* bundle=wxGetApp().preset_bundle;const size_t n=bundle->filament_presets.size(); // SNORCA
    if(bundle->mixed_filaments.enabled_count()>0){std::set<int> rows; // SNORCA
        for(int u:used){const MixedFilament* mf=size_t(u)<n?nullptr:bundle->mixed_filaments.mixed_filament_from_id(unsigned(u+1),n); // SNORCA
            if(!mf){rows.insert(u);continue;}for(unsigned id:snorca_mixed_components(*mf,n))if(id>=1&&id<=n)rows.insert(int(id)-1);} // SNORCA
        used.swap(rows);} // SNORCA
    return nlohmann::json(std::vector<int>(used.begin(),used.end())); // SNORCA
} // SNORCA
// SNORCA (2026-11-12): move object/part/layer-range filaments and painted colours from old row i to new row remap[i]
// (1-based in the model). Returns the restore steps so a failed sync puts the model back exactly; empty = no change. // SNORCA
// SNORCA (2026-11-14): vmap (optional) = new 1-based id of each old enabled mixed filament (old id remap.size()+1+k),
// 0 = that mixed filament was dropped (object setting -> 0 = default, paint -> unpainted). Without vmap: as before. // SNORCA
static std::vector<std::function<void()>> snorca_remap_model(const std::vector<int>& remap,const std::vector<int>* vmap=nullptr) // SNORCA
{ // SNORCA
    std::vector<std::function<void()>> undo; // SNORCA
    auto map1=[&](int v){if(v<=0)return v;const size_t i=size_t(v-1); // SNORCA
        if(i<remap.size())return remap[i]>=0?remap[i]+1:v; // SNORCA
        if(vmap&&i-remap.size()<vmap->size())return (*vmap)[i-remap.size()]; // SNORCA
        return v;}; // SNORCA
    auto fix=[&](ModelConfig& c){for(const auto& k:snorca_filament_keys(c)){const int v=c.opt_int(k),n=map1(v);if(n==v)continue; // SNORCA
        c.set_key_value(k,new ConfigOptionInt(n));undo.push_back([&c,k,v](){c.set_key_value(k,new ConfigOptionInt(v));});}}; // SNORCA
    const int first=int(EnforcerBlockerType::Extruder1),last=int(EnforcerBlockerType::ExtruderMax); // SNORCA
    EnforcerBlockerStateMap states;for(int k=0;k<=last;++k)states[k]=EnforcerBlockerType(k);bool paint=false; // SNORCA
    for(size_t i=0;i<remap.size()&&first+int(i)<=last;++i)if(remap[i]>=0&&remap[i]!=int(i)&&first+remap[i]<=last){states[first+i]=EnforcerBlockerType(first+remap[i]);paint=true;} // SNORCA
    if(vmap)for(size_t k=0;k<vmap->size()&&first+int(remap.size()+k)<=last;++k){const int to=(*vmap)[k],from=int(remap.size()+k)+1; // SNORCA
        if(to==from)continue;states[first+from-1]=to<=0||first+to-1>last?EnforcerBlockerType::NONE:EnforcerBlockerType(first+to-1);paint=true;} // SNORCA
    for(auto* o:wxGetApp().plater()->model().objects){fix(o->config);for(auto& r:o->layer_config_ranges)fix(r.second); // SNORCA
        for(auto* v:o->volumes){fix(v->config); // SNORCA
            if(!paint||v->mmu_segmentation_facets.empty())continue; // SNORCA
            const auto before=v->mmu_segmentation_facets.get_data(); // SNORCA
            TriangleSelector ts(v->mesh());ts.deserialize(before,false);ts.remap_triangle_state(states); // SNORCA
            if(!v->mmu_segmentation_facets.set(ts))continue; // SNORCA
            undo.push_back([v,before](){TriangleSelector back(v->mesh());back.deserialize(before,false);v->mmu_segmentation_facets.set(back);});}} // SNORCA
    return undo; // SNORCA
} // SNORCA
// SNORCA (2026-11-14): upstream mixed (virtual) filaments under our sync. Mixed filaments are numbered after the
// physical rows and their definitions name physical rows, so when the sync reorders or removes rows the definitions are
// rewritten through the same row remap, a mixed filament whose component row is gone is dropped (named in the status),
// and objects/paint on mixed filaments follow them to their new numbers. Inactive (no change at all) without mixed
// filaments, so the ordinary sync path is untouched. // SNORCA
struct SnorcaMixedRemap { // SNORCA
    bool active{false};size_t old_physical{0};std::vector<MixedFilament> old_mixed; // SNORCA
    std::string new_definitions,old_print_definitions;bool had_print_definitions{false};std::vector<std::string> dropped; // SNORCA
}; // SNORCA
static void snorca_set_mixed_definitions(PresetBundle* bundle,const std::string& defs) // SNORCA: same two places upstream writes
{ // SNORCA
    bundle->project_config.set_key_value("mixed_filament_definitions",new ConfigOptionString(defs)); // SNORCA
    DynamicPrintConfig& print_cfg=bundle->prints.get_edited_preset().config; // SNORCA
    if(print_cfg.has("mixed_filament_definitions"))print_cfg.set_key_value("mixed_filament_definitions",new ConfigOptionString(defs)); // SNORCA
} // SNORCA
static SnorcaMixedRemap snorca_mixed_prepare(const std::vector<int>& remap) // SNORCA
{ // SNORCA
    SnorcaMixedRemap m;auto* bundle=wxGetApp().preset_bundle; // SNORCA
    m.old_physical=bundle->filament_presets.size();m.old_mixed=bundle->mixed_filaments.mixed_filaments(); // SNORCA
    if(m.old_mixed.empty())return m; // SNORCA
    m.active=true; // SNORCA
    const DynamicPrintConfig& print_cfg=bundle->prints.get_edited_preset().config; // SNORCA
    if(print_cfg.has("mixed_filament_definitions")){m.had_print_definitions=true;m.old_print_definitions=print_cfg.opt_string("mixed_filament_definitions");} // SNORCA
    std::vector<unsigned> pmap(m.old_physical+1,0); // SNORCA: 1-based old row -> new row, 0 = row gone
    for(size_t i=0;i<m.old_physical&&i<remap.size();++i)if(remap[i]>=0)pmap[i+1]=unsigned(remap[i]+1); // SNORCA
    auto mapid=[&](unsigned id){return id>=1&&id<pmap.size()?pmap[id]:0u;}; // SNORCA
    MixedFilamentManager tmp=bundle->mixed_filaments;std::vector<MixedFilament> kept;size_t vid=m.old_physical; // SNORCA
    for(MixedFilament mf:tmp.mixed_filaments()){if(mf.enabled)++vid;bool lost=false; // SNORCA
        const std::string norm=MixedFilamentManager::normalize_manual_pattern(mf.manual_pattern); // SNORCA
        std::string what="mixed filament "+std::to_string(vid)+" ("; // SNORCA
        if(!norm.empty()){std::string out; // SNORCA: pattern tokens resolved to rows, written back as literal rows
            for(const auto& g:MixedFilamentManager::split_pattern_groups(norm)){if(!out.empty())out+=','; // SNORCA
                for(const auto& t:MixedFilamentManager::split_pattern_group_to_tokens(g,m.old_physical)){ // SNORCA
                    const unsigned id=mapid(MixedFilamentManager::physical_filament_from_token(t,mf,m.old_physical)); // SNORCA
                    if(id==0){lost=true;break;}out+=id<10?std::to_string(id):"["+std::to_string(id)+"]";} // SNORCA
                if(lost)break;} // SNORCA
            what+="pattern "+norm+")"; // SNORCA
            if(!lost){mf.manual_pattern=out;mf.component_a=1;mf.component_b=2; // SNORCA: cycle-mode invariant (tokens 1/2 = a/b)
                std::vector<unsigned> g;for(unsigned id:MixedFilamentManager::decode_gradient_component_ids(mf.gradient_component_ids,0))if(mapid(id))g.push_back(mapid(id)); // SNORCA
                mf.gradient_component_ids=MixedFilamentManager::encode_gradient_component_ids(g);}} // SNORCA
        else{std::vector<unsigned> g;for(unsigned id:MixedFilamentManager::decode_gradient_component_ids(mf.gradient_component_ids,0)){if(!mapid(id)){lost=true;break;}g.push_back(mapid(id));} // SNORCA
            const unsigned a=mapid(mf.component_a),b=mapid(mf.component_b); // SNORCA
            what+="filaments "+std::to_string(mf.component_a)+" + "+std::to_string(mf.component_b)+")"; // SNORCA
            if(a==0||b==0||a==b)lost=true; // SNORCA
            if(!lost){mf.gradient_component_ids=MixedFilamentManager::encode_gradient_component_ids(g);mf.component_a=a;mf.component_b=b;}} // SNORCA
        if(lost){if(mf.enabled)m.dropped.push_back(what);continue;} // SNORCA
        kept.push_back(std::move(mf));} // SNORCA
    tmp.mixed_filaments()=std::move(kept);m.new_definitions=tmp.serialize_custom_entries(); // SNORCA
    return m; // SNORCA
} // SNORCA
// SNORCA: after the rows are in place: regenerate the mixed list from the rewritten definitions and return the virtual map.
static std::vector<int> snorca_mixed_apply(const SnorcaMixedRemap& m) // SNORCA
{ // SNORCA
    auto* bundle=wxGetApp().preset_bundle;snorca_set_mixed_definitions(bundle,m.new_definitions); // SNORCA
    bundle->update_multi_material_filament_presets(size_t(-1),m.old_physical); // SNORCA
    (void)bundle->consume_last_filament_id_remap(); // SNORCA: our model remap below replaces upstream's guess
    const size_t n=bundle->filament_presets.size();std::map<uint64_t,int> now;size_t idx=0; // SNORCA
    for(const auto& mf:bundle->mixed_filaments.mixed_filaments())if(mf.enabled)now.emplace(mf.stable_id,int(n+1+idx++)); // SNORCA
    std::vector<int> vmap;for(const auto& mf:m.old_mixed)if(mf.enabled){auto it=now.find(mf.stable_id);vmap.push_back(it==now.end()?0:it->second);} // SNORCA
    return vmap; // SNORCA
} // SNORCA
// SNORCA: after project_config was put back (failed sync, Undo, Edit > Undo/Redo): rebuild the mixed list from it.
static void snorca_mixed_reload(PresetBundle* bundle,const SnorcaMixedRemap* m=nullptr) // SNORCA
{ // SNORCA
    if(m&&m->active&&m->had_print_definitions)bundle->prints.get_edited_preset().config.set_key_value("mixed_filament_definitions",new ConfigOptionString(m->old_print_definitions)); // SNORCA
    if(bundle->mixed_filaments.mixed_filaments().empty()&&(!bundle->project_config.has("mixed_filament_definitions")||bundle->project_config.opt_string("mixed_filament_definitions").empty()))return; // SNORCA
    if(!m&&bundle->project_config.has("mixed_filament_definitions"))snorca_set_mixed_definitions(bundle,bundle->project_config.opt_string("mixed_filament_definitions")); // SNORCA
    bundle->update_multi_material_filament_presets();(void)bundle->consume_last_filament_id_remap(); // SNORCA
} // SNORCA
// SNORCA (2026-11-12): one log line per sync result (Auto repeats are logged only when the result changes).
// No serials, ids or keys: printer preset name, IP address, status text and row labels only. // SNORCA
static void snorca_sync_log(bool manual,const std::string& printer,const std::string& address,const std::string& plain,const std::string& details) // SNORCA
{ // SNORCA
    static std::string last;std::string line=plain+(details.empty()?std::string():" | "+details); // SNORCA
    for(auto& c:line)if(c=='\n')c=';'; // SNORCA
    if(!manual&&line==last)return;last=line; // SNORCA
    BOOST_LOG_TRIVIAL(warning)<<"SNORCA filament sync ("<<(manual?"sync button":"auto")<<", "<<printer<<" "<<address<<"): "<<line;
} // SNORCA

void Sidebar::sync_snorca_filaments(bool manual)
{
    using J=nlohmann::json;
    if(SnorcaPrinterConnect::refresh_device_flags())update_all_preset_comboboxes(false);
    if(p->snorca_sync_busy)return;
    // Recover missing connection metadata without asking the user to reconnect.
    wxGetApp().refresh_gateway_connection();
    SnorcaSelectedPrinter selected_printer;
    try{selected_printer=snorca_selected_printer();}
    catch (const std::exception& e) {
        const std::string detail = e.what();
        // Keep the one-line banner readable; hover shows the action and palette status.
        p->snorca_sync_status->SetLabel(wxString::FromUTF8(detail.substr(0, detail.find(". "))));
        p->snorca_sync_status->SetToolTip(wxString::FromUTF8(detail));
        return;
    }
    const auto address=selected_printer.address,serial=selected_printer.serial,preset_name=selected_printer.preset;
    const auto selection_key=preset_name+"|"+address+"|"+serial+"|"+selected_printer.device.clientId;
    const auto now=std::chrono::steady_clock::now();
    std::string palette_key;try{palette_key=snorca_palette_state().dump();}catch(...){} // SNORCA
    if(!manual&&selection_key==p->snorca_sync_last_selection&&palette_key==p->snorca_sync_last_palette&&now-p->snorca_sync_last_read<std::chrono::seconds(10))return; // SNORCA
    p->snorca_sync_last_selection=selection_key;p->snorca_sync_last_read=now;p->snorca_sync_last_palette=palette_key; // SNORCA
    J before,compatible,overrides=J::object();
    try{before=snorca_palette_state();compatible=snorca_compatible_filaments();
        const auto raw=wxGetApp().app_config->get("snorca_filament_overrides_v1");
        if(!raw.empty()){if(raw.size()>65536)throw std::runtime_error("Saved filament labels are oversized.");const auto labels=J::parse(raw);
            if(!labels.is_object())throw std::runtime_error("Saved filament labels are invalid.");
            if(labels.contains(serial)){overrides=labels.at(serial);if(!overrides.is_object())throw std::runtime_error("Saved printer filament labels are invalid.");}
        }
    }
    catch(const std::exception& e){p->snorca_sync_status->SetLabel(wxString::FromUTF8(std::string("Sync retained palette: ")+e.what()));return;}
    // SNORCA (2026-11-15): Auto never takes the printer connection away from another printer (the Device tab's).
    // It waits; the sync button (an explicit request) still connects to the Prepare printer as before.
    if(!selected_printer.gateway&&!manual&&!SnorcaPrinterConnect::connected(selected_printer.device)&&SnorcaPrinterConnect::other_printer_connected(selected_printer.device)){ // SNORCA
        const wxString l=wxString::FromUTF8("Auto waits: the Device tab is connected to another printer"); // SNORCA
        const wxString t=wxString::FromUTF8("Auto sync does not take the printer connection away from the Device tab. Palette retained.\nSelect "+preset_name+" in the Device tab, or press the sync button to connect to it."); // SNORCA
        if(p->snorca_sync_status->GetLabel()!=l){p->snorca_sync_status->SetLabel(l);Layout();} // SNORCA
        if(p->snorca_sync_status->GetToolTipText()!=t)p->snorca_sync_status->SetToolTip(t); // SNORCA
        snorca_sync_log(false,preset_name,address,"Auto waits: Device tab connected to another printer; palette retained.",""); // SNORCA
        return;} // SNORCA
    if(p->snorca_sync_thread.joinable())p->snorca_sync_thread.join();
    p->snorca_sync_busy=true; // SNORCA: no "Reading…" text every 10 s; the line changes only when the result changes
    Sidebar* self=this;auto alive=p->snorca_sync_alive;auto generation=p->snorca_sync_generation;const uint64_t ticket=++(*generation);
    const auto device=selected_printer.device;const auto printer_config=wxGetApp().preset_bundle->printers.get_edited_preset().config;
    const bool use_gateway=selected_printer.gateway;
    const auto gateway_generation=selected_printer.gateway_generation;
    const auto gateway=wxGetApp().gateway_service();
    const bool already_connected=use_gateway||SnorcaPrinterConnect::connected(device);
    std::shared_ptr<PrintHost> original_host;wxGetApp().get_connect_host(original_host);
    p->snorca_sync_thread=std::thread([self,alive,generation,ticket,device,printer_config,already_connected,original_host,address,serial,preset_name,before,compatible,overrides,manual,use_gateway,gateway_generation,gateway](){
        auto connection=std::make_shared<SnorcaPrinterConnect::Ready>();
        if(!already_connected)*connection=SnorcaPrinterConnect::prepare(device,printer_config,alive,generation,ticket);
        J inventory;std::string error;
        auto verify_gateway=[&] {
            if (!use_gateway) return;
            if (!gateway || !gateway->is_connected()) throw std::runtime_error("Printer gateway disconnected.");
            auto current=gateway->request_sync("query.device.current",J::object(),std::chrono::seconds(3));
            auto parsed=Gateway::parse_current_device(current.value);
            if(current.error||!parsed||!parsed->connected||parsed->serial_number!=serial)
                throw std::runtime_error("Gateway printer changed or disconnected.");
            auto devices=gateway->request_sync("query.device.list",J::object(),std::chrono::seconds(3));
            if(devices.error||Gateway::connected_lan_address(devices.value,serial)!=address)
                throw std::runtime_error("Gateway LAN identity no longer matches the selected printer.");
        };
        try{
            verify_gateway();
            std::string response;bool complete=false;
            Http::get("http://"+address+"/multiace/api/state").direct_no_redirects().timeout_connect(3).timeout_max(6).size_limit(1024*1024)
                .on_complete([&](std::string body,unsigned code){if(code==200){response=std::move(body);complete=true;}else error="Printer returned a non-200 response.";})
                .on_error([&](std::string,std::string message,unsigned){error=message;}).perform_sync();
            if(!complete)throw std::runtime_error(error.empty()?"Printer did not return inventory.":error);
            inventory=SnorcaFilamentSync::normalize(J::parse(response),serial,address,overrides);
            verify_gateway();
        }catch(const std::exception& e){error=e.what();}
        wxGetApp().CallAfter([self,alive,generation,ticket,device,already_connected,original_host,connection,address,serial,preset_name,before,compatible,manual,inventory,error,use_gateway,gateway_generation](){
            if(!alive->load())return;self->p->snorca_sync_busy=false;
            std::string connection_note;
            // SNORCA: short line, full text in the tooltip, rewritten only when it changes, one log line per new result.
            auto status=[&](const std::string& plain,const std::string& details=std::string()){ // SNORCA
                const wxString l=wxString::FromUTF8(plain),t=wxString::FromUTF8(plain+(details.empty()?std::string():"\n"+details)+(connection_note.empty()?std::string():"\n"+connection_note)); // SNORCA
                if(self->p->snorca_sync_status->GetLabel()!=l){self->p->snorca_sync_status->SetLabel(l);self->Layout();} // SNORCA
                if(self->p->snorca_sync_status->GetToolTipText()!=t)self->p->snorca_sync_status->SetToolTip(t); // SNORCA
                snorca_sync_log(manual,preset_name,address,plain,details+(connection_note.empty()?std::string():"; "+connection_note));}; // SNORCA
            try{const auto current=snorca_selected_printer();if(current.address!=address||current.serial!=serial||current.preset!=preset_name||current.device.clientId!=device.clientId||current.gateway!=use_gateway||(use_gateway&&current.gateway_generation!=gateway_generation)){status("Printer changed; old color response discarded.");return;}}
            catch(const std::exception&){status("Selected printer registration changed; old color response discarded.");return;}
            if(!manual&&!self->p->snorca_sync_auto->GetValue()){status("Auto sync paused; palette retained.");return;}
            if(generation->load()!=ticket){status("Connection/sync request superseded; palette retained.");return;}
            if (!use_gateway) {
            std::shared_ptr<PrintHost> current_host;wxGetApp().get_connect_host(current_host);
            if(current_host!=original_host&&!SnorcaPrinterConnect::connected(device)){status("Device connection changed during sync; old request discarded.");return;}
            // SNORCA (2026-11-15): another printer connected while Auto was preparing (Device tab switch): drop ours, keep theirs.
            if(!manual&&!SnorcaPrinterConnect::connected(device)&&SnorcaPrinterConnect::other_printer_connected(device)){ // SNORCA
                if(connection->engine){std::string ignored;connection->engine->Disconnect(ignored,200);connection->engine.reset();}connection->host.reset();connection->ok=false; // SNORCA
                status("Auto waits: Device tab connected to another printer; palette retained.");return;} // SNORCA
            if(!SnorcaPrinterConnect::connected(device)&&!already_connected&&connection->ok){std::string message;if(!SnorcaPrinterConnect::install(*connection,message))connection->error=message;else self->update_all_preset_comboboxes(false);}
            if(SnorcaPrinterConnect::connected(device))connection_note=" Connected to "+address+".";
            else {
                connection_note=" Printer connection unavailable: "+(connection->error.empty()?std::string("saved pairing did not connect."):connection->error);
                status("Previous palette retained; saved printer identity was not verified.");return;
            }

            } else connection_note=" Connected by LAN through the Device gateway.";

            if(!error.empty()){status("Sync unavailable: "+error+" Previous palette retained.");return;}
            try{
                if(snorca_palette_state()!=before){status("Project changed during sync; palette retained. Refresh again.");return;}
                auto planning_before=before;planning_before["untouched_empty_palette"]=snorca_empty_palette_fingerprint(before)==self->p->snorca_sync_empty_baseline;
                planning_before["verified_empty_repair"]=snorca_verified_empty_repair(before,address,serial);
                planning_before["used"]=snorca_used_rows(); // SNORCA
                // SNORCA (2026-11-13): Auto now moves objects off rows the printer does not have. When a filament move (Auto or     SNORCA
                // button) was undone with Edit > Undo (snorca_sync_follow_undo_redo), Auto leaves the list and the objects alone   SNORCA
                // for this printer until another printer is selected or the sync button is pressed, so the Undo is not redone.   SNORCA
                const std::string sel_key=preset_name+"|"+address+"|"+serial+"|"+device.clientId; // SNORCA
                auto& sp=*self->p; bool auto_undone=false; // SNORCA
                if(manual)sp.snorca_sync_auto_undone_key.clear(); // SNORCA
                else{ // SNORCA
                    if(!sp.snorca_sync_auto_undone_key.empty()&&sp.snorca_sync_auto_undone_key!=sel_key)sp.snorca_sync_auto_undone_key.clear(); // SNORCA
                    auto_undone=!sp.snorca_sync_auto_undone_key.empty(); // SNORCA
                    if(auto_undone)planning_before["keep_used_not_on_printer"]=true;} // SNORCA
                // SNORCA: the pool is exactly this printer's feeds (plan_exact); plan() kept other printers' rows.
                const auto planned=SnorcaFilamentSync::plan_exact(planning_before,inventory,compatible,manual,MAXIMUM_EXTRUDER_NUMBER); // SNORCA
                auto* bundle=wxGetApp().preset_bundle;
                const DynamicPrintConfig old_printer=bundle->printers.get_edited_preset().config;
                const auto* diameters=old_printer.option<ConfigOptionFloats>("nozzle_diameter");
                if(!diameters||diameters->values.size()!=4)throw std::runtime_error("Select the four-toolhead Snapmaker U1 printer profile before syncing.");
                // Filament sync must not rewrite the user's Toolheads/Unit settings.
                // A retained palette row is a route. Its remembered swatch or preset cannot tell us a spool is loaded.
                const auto& printer_feeds = inventory.at("rows");
                size_t loaded_count = 0, ace_slot_count = 0;
                for (const auto& feed : printer_feeds) {
                    loaded_count += feed.at("available").get<bool>();
                    ace_slot_count += !feed.at("ace_unit").is_null();
                }
                const auto plural=[](size_t n,const char* one,const char* many){return std::to_string(n)+" "+(n==1?one:many);};
                const size_t feed_count = printer_feeds.size();
                const bool only_ace_slots = ace_slot_count == feed_count;
                const std::string loadout = std::to_string(loaded_count) + " loaded / " +
                    plural(feed_count, only_ace_slots ? "slot" : "feed", only_ace_slots ? "slots" : "feeds");

                const auto feed_tooltip = [](const auto& row) {
                    const auto& own = row.at("ownership");
                    return own.at("label").template get<std::string>() + " → nozzle " +
                        std::to_string(row.at("nozzle").template get<int>() + 1);
                };
                const auto update_feed_tooltips = [self, &feed_tooltip](const auto& rows) {
                    for (size_t i = 0; i < rows.size() && i < self->p->combos_filament.size(); ++i) {
                        if (!rows[i].at("ownership").empty())
                            self->p->combos_filament[i]->SetToolTip(wxString::FromUTF8(feed_tooltip(rows[i])));
                    }
                };
                std::string inventory_details;
                size_t detail_index = 0;
                for (const auto& row : planned.at("rows")) {
                    ++detail_index;
                    if (!row.at("ownership").empty())
                        inventory_details += std::to_string(detail_index) + ": " + feed_tooltip(row) + "\n";
                }
                for(const auto& warning:planned.at("warnings"))inventory_details+="\n"+warning.get<std::string>();
                // "Matches" is only accurate when the palette follows the current printer.
                const size_t hand_n=planned.at("hand_edited").size(),kept_n=planned.at("kept_not_on_printer").size();
                std::string result_label,result_note; // SNORCA
                if(hand_n){result_label=plural(hand_n,"filament","filaments")+" changed by hand"; // SNORCA
                    result_note=plural(hand_n,"filament was","filaments were")+" changed by hand and "+(hand_n==1?"is":"are")+" not following the printer. Press the sync button to match the printer."; // SNORCA
                    for(const auto& h:planned.at("hand_edited"))result_note+="\n  "+h.get<std::string>();} // SNORCA
                if(kept_n){if(result_label.empty())result_label=plural(kept_n,"used filament","used filaments")+" not on "+preset_name;else result_label=plural(hand_n+kept_n,"filament","filaments")+" not following "+preset_name; // SNORCA
                    result_note+=std::string(result_note.empty()?"":"\n")+plural(kept_n,"filament is","filaments are")+" used by objects but not loaded on "+preset_name+"; kept at the end of the list. Press the sync button to move those objects to the closest printer filament."; // SNORCA
                    for(const auto& k:planned.at("kept_not_on_printer"))result_note+="\n  "+k.get<std::string>();} // SNORCA
                // SNORCA (2026-11-13): Auto moved objects off rows the printer does not have; say what moved, in one short line.
                const size_t moved_n=planned.at("moved").size(); // SNORCA
                if(!manual&&moved_n){std::string pairs; // SNORCA
                    for(const auto& m:planned.at("moved"))pairs+=(pairs.empty()?"":", ")+std::to_string(m.at("from").get<int>())+" "+m.value("from_name",std::string())+" → "+std::to_string(m.at("to").get<int>())+" "+m.value("to_name",std::string()); // SNORCA
                    result_label="Moved "+plural(moved_n,"object filament","object filaments")+" to match "+preset_name+": "+pairs; // SNORCA
                    result_note=plural(moved_n,"filament","filaments")+" used by objects "+(moved_n==1?"is":"are")+" not loaded on "+preset_name+"; Auto moved those objects to the printer filament of the same material with the closest colour (else the closest colour)."+(result_note.empty()?std::string():"\n"+result_note);} // SNORCA
                for(const auto& r:planned.at("removed"))inventory_details+="\nRemoved: "+r.get<std::string>(); // SNORCA
                for(const auto& m:planned.at("moved"))inventory_details+="\nObjects on filament "+std::to_string(m.at("from").get<int>())+" moved to "+std::to_string(m.at("to").get<int>())+(m.at("same_material").get<bool>()?" (same material, closest colour)":" (closest colour, other material)"); // SNORCA
                if(auto_undone&&planned.at("rows")!=before.at("rows")){ // SNORCA (2026-11-13): an undone move is left as it is
                    std::string note="The last filament move was undone with Edit > Undo, so Auto leaves the filament list and the objects as they are until another printer is selected or the sync button is pressed."; // SNORCA
                    for(const auto& k:planned.at("kept_not_on_printer"))note+="\nUsed by objects, not on "+preset_name+": "+k.get<std::string>(); // SNORCA
                    status("Filament move undone; press sync to match "+preset_name,note);return;} // SNORCA
                if(planned.at("rows")==before.at("rows")){ // SNORCA
                    update_feed_tooltips(planned.at("rows"));
                    if(result_label.empty())result_label="Matches "+preset_name+" ("+loadout+")";
                    status(result_label,(result_note.empty()?std::string():result_note+"\n")+inventory_details);return;} // SNORCA
                const DynamicPrintConfig old_config=bundle->project_config;
                const auto old_presets=bundle->filament_presets;const auto old_multicolor=bundle->ams_multi_color_filment;
                wxGetApp().plater()->take_snapshot("Sync printer colors");
                const size_t snapshot_time=wxGetApp().plater()->undo_redo_stack_main().active_snapshot_time(); // SNORCA (2026-11-13)
                std::vector<std::function<void()>> model_undo; // SNORCA
                const auto row_remap=planned.at("remap").get<std::vector<int>>(); // SNORCA
                SnorcaMixedRemap mixed; // SNORCA (2026-11-14)
                SnorcaQuietGradients quiet_gradients; // SNORCA: no auto-gradient question during the sync
                try{
                    mixed=snorca_mixed_prepare(row_remap); // SNORCA
                    if(!mixed.active)model_undo=snorca_remap_model(row_remap); // SNORCA: before the row count changes (no mixed filaments: as before)
                    const auto& rows=planned.at("rows");bundle->set_num_filaments(unsigned(rows.size()));
                    auto& colors=bundle->project_config.option<ConfigOptionStrings>("filament_colour",true)->values;
                    auto& nozzles=bundle->project_config.option<ConfigOptionInts>("fos_filament_nozzle",true)->values;
                    auto& slots=bundle->project_config.option<ConfigOptionInts>("fos_filament_mms_slot",true)->values;
                    auto& ownership=bundle->project_config.option<ConfigOptionStrings>("fos_filament_sync",true)->values;
                    for(size_t i=0;i<rows.size();++i){const auto& row=rows[i];bundle->filament_presets[i]=row.at("preset").get<std::string>();colors[i]=row.at("color").get<std::string>();nozzles[i]=row.at("nozzle").get<int>();slots[i]=row.at("mms_slot").get<int>();ownership[i]=row.at("ownership").empty()?"":row.at("ownership").dump();if(!row.at("ownership").empty())bundle->ams_multi_color_filment[i].clear();}
                    { // SNORCA (2026-11-14): upstream's per-row colour lists follow the rows; a printer row is a single colour.
                        auto& mc=bundle->project_config.option<ConfigOptionStrings>("filament_multi_colors",true)->values; // SNORCA
                        auto& md=bundle->project_config.option<ConfigOptionInts>("filament_colour_mode",true)->values; // SNORCA
                        const auto* omc=old_config.option<ConfigOptionStrings>("filament_multi_colors");const auto* omd=old_config.option<ConfigOptionInts>("filament_colour_mode"); // SNORCA
                        std::vector<int> from(rows.size(),-1);for(size_t i=0;i<row_remap.size();++i)if(row_remap[i]>=0&&size_t(row_remap[i])<rows.size()&&from[row_remap[i]]<0)from[row_remap[i]]=int(i); // SNORCA
                        mc.resize(rows.size());md.resize(rows.size(),0); // SNORCA
                        for(size_t j=0;j<rows.size();++j){const bool own=!rows[j].at("ownership").empty(); // SNORCA
                            if(own||from[j]<0){mc[j]=colors[j];md[j]=0;continue;} // SNORCA
                            mc[j]=omc&&size_t(from[j])<omc->values.size()?omc->values[from[j]]:colors[j];md[j]=omd&&size_t(from[j])<omd->values.size()?omd->values[from[j]]:0;}} // SNORCA
                    if(mixed.active){const auto vmap=snorca_mixed_apply(mixed);model_undo=snorca_remap_model(row_remap,&vmap);} // SNORCA: one pass, physical and mixed
                    else bundle->update_multi_material_filament_presets();
                    wxGetApp().plater()->on_filaments_change(rows.size());
                    for(auto* combo:self->p->combos_filament)combo->update();
                    self->update_nozzle_filament_slots();self->update_dynamic_filament_list();
                    wxGetApp().plater()->on_config_change(bundle->full_config());
                    wxGetApp().plater()->sidebar().obj_list()->update_filament_colors();wxGetApp().plater()->update();
                    wxGetApp().plater()->update_project_dirty_from_presets();
                    // Preset definitions, models, paint assignments and printer metadata are never rewritten.
                    if(planned.value("clean_empty_palette",false)&&snorca_legacy_cleanup_grant_matches(before))wxGetApp().app_config->set("snorca_filament_startup_replace_v1",""); // SNORCA
                    if(planned.value("clean_empty_palette",false)&&snorca_verified_empty_repair(before,address,serial))wxGetApp().app_config->set("snorca_filament_repair_v1",""); // SNORCA
                    bundle->export_selections(*wxGetApp().app_config);
                    self->p->snorca_sync_undo_config.reset(new DynamicPrintConfig(old_config));self->p->snorca_sync_undo_printer.reset(new DynamicPrintConfig(old_printer));self->p->snorca_sync_undo_presets=old_presets;self->p->snorca_sync_undo_multicolor=old_multicolor;
                    self->reset_snorca_filament_sync_baseline();self->p->snorca_sync_undo_after=snorca_palette_state().dump();self->p->snorca_sync_last_palette=self->p->snorca_sync_undo_after;self->p->snorca_sync_undo->Enable(true); // SNORCA
                    update_feed_tooltips(rows);
                    // The tooltip keeps the physical slot and its destination nozzle separate.
                    std::string summary = result_label.empty() ? "Synced " + preset_name + " (" + loadout + ")" : result_label;
                    std::string tip = (result_note.empty() ? std::string() : result_note + "\n") + loadout + " at " + address +
                        ". Loaded counts feeds the printer reports as ready.";
                    if(!mixed.dropped.empty()){summary+="; removed "+plural(mixed.dropped.size(),"mixed filament","mixed filaments"); // SNORCA (2026-11-14)
                        tip+="\n"+plural(mixed.dropped.size(),"mixed filament was","mixed filaments were")+" removed because a filament "+(mixed.dropped.size()==1?"it":"they")+" mixed is no longer in the list; objects that used "+(mixed.dropped.size()==1?"it":"them")+" now use their default filament:"; // SNORCA
                        for(const auto& d:mixed.dropped)tip+="\n  "+d;} // SNORCA
                    for(const auto& row:inventory.at("rows")){
                        if(row.value("user_override",false))tip+="\n"+row.at("label").get<std::string>()+": user label, approximate swatch"; // SNORCA
                        else if(row.at("material").get<std::string>().empty()||row.at("color").is_null())tip+="\nUnidentified: "+row.at("label").get<std::string>(); // SNORCA
                    }
                    for(const auto& warning:planned.at("warnings"))tip+="\n"+warning.get<std::string>(); // SNORCA
                    if(!model_undo.empty()){self->p->snorca_sync_undo->Enable(false);tip+="\nObjects now use the new filament numbers (Edit > Undo goes back).";} // SNORCA
                    if(!model_undo.empty()){ // SNORCA (2026-11-13): objects were renumbered; remembered so Edit > Undo / Redo can put the list back too
                        sp.snorca_sync_move_time=snapshot_time;sp.snorca_sync_move_sel=sel_key;sp.snorca_sync_auto_undone_key.clear(); // SNORCA
                        sp.snorca_sync_move_before_rows=before.at("rows").dump();sp.snorca_sync_move_after_rows=snorca_palette_state().at("rows").dump(); // SNORCA
                        sp.snorca_sync_move_before_presets=old_presets;sp.snorca_sync_move_after_presets=bundle->filament_presets; // SNORCA
                        sp.snorca_sync_move_before_cfg.reset(new DynamicPrintConfig(old_config));sp.snorca_sync_move_after_cfg.reset(new DynamicPrintConfig(bundle->project_config)); // SNORCA
                        sp.snorca_sync_move_before_mc=old_multicolor;sp.snorca_sync_move_after_mc=bundle->ams_multi_color_filment;} // SNORCA
                    status(summary,tip+"\n"+inventory_details); // SNORCA
                }catch(...){for(auto it=model_undo.rbegin();it!=model_undo.rend();++it)(*it)(); // SNORCA: model back as it was
                    bundle->filament_presets=old_presets;bundle->project_config=old_config;bundle->ams_multi_color_filment=old_multicolor;snorca_mixed_reload(bundle,&mixed);wxGetApp().plater()->on_filaments_change(old_presets.size());wxGetApp().plater()->on_config_change(bundle->full_config()); // SNORCA
                    wxGetApp().plater()->sidebar().obj_list()->update_filament_colors();wxGetApp().plater()->update();throw;}
            }catch(const std::exception& e){status(std::string("Sync retained palette: ")+e.what());}
        });
    });
}

// SNORCA (2026-11-13): Edit > Undo / Redo restore the model only (objects, their filament numbers, paint), not the filament
// list. After a jump in the undo stack, when the list is still the one the last filament move made and the stack is now
// before that move (Undo), the list from before the move is put back; when the list is the one from before the move and
// the stack is at or after the move again (Redo), the list after the move is put back. Wipe tower position stays as the
// undo stack set it. Called by Plater::priv::undo_redo_to with snapshots suppressed.
void Sidebar::snorca_sync_follow_undo_redo() // SNORCA
{ // SNORCA
    auto& sp=*p; if(!sp.snorca_sync_move_before_cfg||!sp.snorca_sync_move_after_cfg)return; // SNORCA
    try{ // SNORCA
        const size_t now=wxGetApp().plater()->undo_redo_stack_main().active_snapshot_time(); // SNORCA
        const std::string rows=snorca_palette_state().at("rows").dump(); // SNORCA
        const bool undo=now<sp.snorca_sync_move_time&&rows==sp.snorca_sync_move_after_rows,redo=now>=sp.snorca_sync_move_time&&rows==sp.snorca_sync_move_before_rows; // SNORCA
        if(!undo&&!redo)return; // SNORCA
        auto* bundle=wxGetApp().preset_bundle; // SNORCA
        DynamicPrintConfig cfg=*(undo?sp.snorca_sync_move_before_cfg:sp.snorca_sync_move_after_cfg); // SNORCA
        for(const char* k:{"wipe_tower_x","wipe_tower_y","wipe_tower_rotation_angle"})if(const auto* o=bundle->project_config.option(k))cfg.set_key_value(k,o->clone()); // SNORCA
        bundle->filament_presets=undo?sp.snorca_sync_move_before_presets:sp.snorca_sync_move_after_presets; // SNORCA
        bundle->project_config=cfg;bundle->ams_multi_color_filment=undo?sp.snorca_sync_move_before_mc:sp.snorca_sync_move_after_mc; // SNORCA
        SnorcaQuietGradients quiet_gradients;snorca_mixed_reload(bundle); // SNORCA (2026-11-14): mixed list from the restored definitions
        wxGetApp().plater()->on_filaments_change(bundle->filament_presets.size());for(auto* combo:p->combos_filament)combo->update(); // SNORCA
        update_nozzle_filament_slots();update_dynamic_filament_list();wxGetApp().plater()->on_config_change(bundle->full_config()); // SNORCA
        wxGetApp().plater()->sidebar().obj_list()->update_filament_colors();wxGetApp().plater()->update(); // SNORCA
        bundle->export_selections(*wxGetApp().app_config); // SNORCA
        sp.snorca_sync_auto_undone_key=undo?sp.snorca_sync_move_sel:std::string(); // SNORCA
        const wxString t=wxString::FromUTF8(undo?"Edit > Undo: objects and filament list are back as before the filament move.":"Edit > Redo: the filament move is applied again."); // SNORCA
        sp.snorca_sync_status->SetLabel(t);sp.snorca_sync_status->SetToolTip(t);Layout(); // SNORCA
        BOOST_LOG_TRIVIAL(warning)<<"SNORCA filament sync: "<<(undo?"Edit > Undo put the filament list from before the move back":"Edit > Redo put the filament list after the move back"); // SNORCA
    }catch(const std::exception& e){BOOST_LOG_TRIVIAL(warning)<<"SNORCA filament sync: Undo / Redo of the filament list failed: "<<e.what();} // SNORCA
} // SNORCA

void Sidebar::undo_snorca_filament_sync()
{
    if(p->snorca_sync_busy||!p->snorca_sync_undo_config)return;
    try{
        if(snorca_palette_state().dump()!=p->snorca_sync_undo_after){p->snorca_sync_status->SetLabel("Palette changed since sync; Undo retained your newer changes.");return;}
        auto* bundle=wxGetApp().preset_bundle;p->snorca_sync_auto->SetValue(false);wxGetApp().app_config->set("snorca_filament_auto_sync","false");
        bundle->filament_presets=p->snorca_sync_undo_presets;bundle->project_config=*p->snorca_sync_undo_config;bundle->ams_multi_color_filment=p->snorca_sync_undo_multicolor;
        if(p->snorca_sync_undo_printer)wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(*p->snorca_sync_undo_printer);
        SnorcaQuietGradients quiet_gradients;snorca_mixed_reload(bundle); // SNORCA (2026-11-14)
        wxGetApp().plater()->on_filaments_change(bundle->filament_presets.size());for(auto* combo:p->combos_filament)combo->update();
        update_nozzle_filament_slots();update_dynamic_filament_list();wxGetApp().plater()->on_config_change(bundle->full_config());
                    wxGetApp().plater()->sidebar().obj_list()->update_filament_colors();wxGetApp().plater()->update();
        bundle->export_selections(*wxGetApp().app_config);p->snorca_sync_undo_config.reset();p->snorca_sync_undo_printer.reset();p->snorca_sync_undo->Enable(false);
        p->snorca_sync_status->SetLabel("Previous palette restored; automatic sync paused.");
    }catch(const std::exception& e){p->snorca_sync_status->SetLabel(wxString::FromUTF8(std::string("Undo unavailable: ")+e.what()));}
}
