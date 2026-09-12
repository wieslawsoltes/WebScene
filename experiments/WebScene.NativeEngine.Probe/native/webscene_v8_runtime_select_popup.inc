    struct select_popup_hit final {
        dom_node* select{nullptr};
        dom_node* row{nullptr};
        dom_node* option{nullptr};
    };

    static std::string select_popup_css_color(uint32_t rgba,uint32_t fallback)
    {
        if((rgba&0xffU)==0U) rgba=fallback;
        char value[16]{};
        std::snprintf(value,sizeof(value),"#%08x",rgba);
        return value;
    }

    static bool select_popup_is_candidate(const dom_node& select)
    {
        if(select.tag!="select" || select.attributes.contains("disabled")
            || select.attributes.contains("multiple")) return false;
        if(auto size=select.attributes.find("size");size!=select.attributes.end()) {
            int parsed=0;
            const auto* first=size->second.data();
            const auto* last=first+size->second.size();
            if(std::from_chars(first,last,parsed).ec==std::errc{} && parsed>1) return false;
        }
        return true;
    }

    dom_node* select_popup_root(dom_node& select)
    {
        const auto* state=document.shadow_dom(select);
        if(state==nullptr || state->root==nullptr) return nullptr;
        auto* root=state->root;
        return root->attributes.contains("data-webscene-select-popup-root")?root:nullptr;
    }

    dom_node* select_popup_panel(dom_node& select)
    {
        auto* root=select_popup_root(select);
        if(root==nullptr) return nullptr;
        for(auto* child:root->children)
            if(child && child->attributes.contains("data-webscene-select-popup")) return child;
        return nullptr;
    }

    dom_node* select_popup_items(dom_node& select)
    {
        auto* panel=select_popup_panel(select);
        if(panel==nullptr) return nullptr;
        for(auto* child:panel->children)
            if(child && child->attributes.contains("data-webscene-select-items")) return child;
        return nullptr;
    }

    bool select_popup_is_open(dom_node& select)
    {
        auto* panel=select_popup_panel(select);
        return panel!=nullptr
            && panel->attributes.find("data-webscene-open")!=panel->attributes.end()
            && panel->attributes.at("data-webscene-open")=="true";
    }

    dom_node* active_select_popup()
    {
        for(auto* select:document.query_selector_all(active_root(),"select"))
            if(select && select_popup_is_open(*select)) return select;
        return nullptr;
    }

    dom_node* select_popup_pending_dismissal()
    {
        for(auto* select:document.query_selector_all(active_root(),"select")) {
            auto* panel=select?select_popup_panel(*select):nullptr;
            if(panel && panel->attributes.contains("data-webscene-dismiss-pointer")) return select;
        }
        return nullptr;
    }

    void select_popup_apply_private_styles(dom_node& root)
    {
        auto* previous=const_cast<dom_node*>(active_css_cascade_root);
        activate_css_cascade(&root);
        apply_css_rules_subtree(root);
        activate_css_cascade(previous==nullptr?&document.body():previous);
        document.mark_dirty();
    }

    bool ensure_select_popup_template(dom_node& select)
    {
        if(select_popup_root(select)!=nullptr) return true;
        if(const auto* existing=document.shadow_dom(select);
            existing!=nullptr && existing->root!=nullptr) return false;
        auto& root=document.create_node(dom_node_kind::document_fragment,"#document-fragment");
        root.parent=&select;
        root.visible=true;
        root.attributes["data-webscene-select-popup-root"]="";
        auto& host_state=document.mutable_shadow_dom(select);
        host_state.root=&root;
        host_state.open=false;
        host_state.delegates_focus=false;
        auto& root_state=document.mutable_shadow_dom(root);
        root_state.host=&select;
        root_state.open=false;
        root_state.delegates_focus=false;
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        static constexpr std::string_view markup=R"HTML(
<div data-webscene-select-popup="" role="listbox" style="display:none;position:fixed;box-sizing:border-box;overflow-y:auto;padding:4px;border:1px solid rgba(127,127,127,.45);border-radius:6px;z-index:2147483000">
  <div data-webscene-select-items="" style="display:block;box-sizing:border-box"></div>
</div>)HTML";
        const auto parsed=parse_html_fragment(document,root,markup,"div");
        if(!parsed) {
            document.mutable_shadow_dom(select).root=nullptr;
            root_state.host=nullptr;
            root.parent=nullptr;
            document.erase_detached_subtree(root);
            return false;
        }
#else
        auto& panel=document.create_element("div");
        panel.attributes["data-webscene-select-popup"]="";
        panel.attributes["role"]="listbox";
        panel.attributes["style"]="display:none;position:fixed;box-sizing:border-box;overflow-y:auto;padding:4px;border:1px solid rgba(127,127,127,.45);border-radius:6px;z-index:2147483000";
        document.append_child(root,panel);
        auto& items=document.create_element("div");
        items.attributes["data-webscene-select-items"]="";
        items.attributes["style"]="display:block;box-sizing:border-box";
        document.append_child(panel,items);
#endif
        document.refresh_shadow_distributions();
        select_popup_apply_private_styles(root);
        return select_popup_panel(select)!=nullptr && select_popup_items(select)!=nullptr;
    }

    static std::string select_popup_signature(dom_node& select)
    {
        std::ostringstream result;
        for(auto* option:forms::select_options(select)) {
            result<<option->id<<':'
                <<(forms::option_is_disabled(*option)?'d':'e')<<':'
                <<(forms::option_is_selected(*option)?'s':'u')<<':'
                <<forms::option_label(*option).size()<<':'
                <<forms::option_label(*option)<<';';
        }
        return result.str();
    }

    static dom_node* select_popup_option_by_id(native_document& document,dom_node& select,uint32_t id)
    {
        auto* option=document.find_by_native_id(id);
        if(option==nullptr || option->tag!="option"
            || forms::containing_select(*option)!=&select) return nullptr;
        return option;
    }

    static uint32_t select_popup_row_option_id(const dom_node& row)
    {
        const auto authored=row.attributes.find("data-webscene-select-option");
        if(authored==row.attributes.end()) return 0U;
        uint32_t result=0;
        const auto* first=authored->second.data();
        const auto* last=first+authored->second.size();
        if(std::from_chars(first,last,result).ec!=std::errc{}) return 0U;
        return result;
    }

    dom_node* select_popup_highlighted_option(dom_node& select)
    {
        auto* panel=select_popup_panel(select);
        if(panel==nullptr) return nullptr;
        const auto value=panel->attributes.find("data-webscene-highlighted-option");
        if(value==panel->attributes.end()) return nullptr;
        uint32_t id=0;
        const auto* first=value->second.data();
        const auto* last=first+value->second.size();
        if(std::from_chars(first,last,id).ec!=std::errc{}) return nullptr;
        return select_popup_option_by_id(document,select,id);
    }

    void select_popup_update_row_styles(dom_node& select)
    {
        auto* root=select_popup_root(select);
        auto* items=select_popup_items(select);
        auto* highlighted=select_popup_highlighted_option(select);
        if(root==nullptr || items==nullptr) return;
        for(auto* row:items->children) {
            if(row==nullptr || !row->attributes.contains("data-webscene-select-option")) continue;
            auto* option=select_popup_option_by_id(document,select,select_popup_row_option_id(*row));
            const auto disabled=option==nullptr || forms::option_is_disabled(*option);
            const auto selected=option!=nullptr && forms::option_is_selected(*option);
            const auto hot=option!=nullptr && option==highlighted;
            std::string style="display:block;box-sizing:border-box;min-height:30px;padding:6px 10px;border-radius:4px;white-space:nowrap;cursor:default;color:inherit;";
            if(disabled) style+="opacity:.42;";
            if(hot) style+="background:rgba(96,145,255,.32);";
            else if(selected) style+="background:rgba(127,127,127,.18);";
            else style+="background:transparent;";
            row->attributes["style"]=std::move(style);
        }
        select_popup_apply_private_styles(*root);
    }

    void select_popup_set_highlight(dom_node& select,dom_node* option)
    {
        auto* panel=select_popup_panel(select);
        if(panel==nullptr) return;
        if(option==nullptr) panel->attributes.erase("data-webscene-highlighted-option");
        else panel->attributes["data-webscene-highlighted-option"]=std::to_string(option->id);
        select_popup_update_row_styles(select);
    }

    bool rebuild_select_popup_rows(dom_node& select)
    {
        auto* root=select_popup_root(select);
        auto* items=select_popup_items(select);
        if(root==nullptr || items==nullptr) return false;
        auto old=items->children;
        document.remove_all_children(*items);
        for(auto* node:old) if(node && node->parent==nullptr) document.erase_detached_subtree(*node);
        const auto add_text=[&](dom_node& parent,std::string value) {
            auto& text=document.create_element("#text");
            text.text_content=std::move(value);
            document.append_child(parent,text);
        };
        const auto append_option=[&](dom_node& option) {
            auto& row=document.create_element("div");
            row.attributes["data-webscene-select-option"]=std::to_string(option.id);
            row.attributes["role"]="option";
            row.attributes["style"]="display:block;box-sizing:border-box;min-height:30px;padding:6px 10px;border-radius:4px;white-space:nowrap;cursor:default;color:inherit;background:transparent";
            if(forms::option_is_disabled(option)) row.attributes["aria-disabled"]="true";
            if(forms::option_is_selected(option)) row.attributes["aria-selected"]="true";
            add_text(row,forms::option_label(option));
            document.append_child(*items,row);
        };
        for(auto* child:select.children) {
            if(child==nullptr) continue;
            if(child->tag=="option") {append_option(*child);continue;}
            if(child->tag!="optgroup") continue;
            auto& group=document.create_element("div");
            group.attributes["data-webscene-select-group"]="";
            group.attributes["role"]="group";
            group.attributes["style"]="display:block;box-sizing:border-box;padding:7px 10px 4px;opacity:.62;color:inherit";
            const auto label=child->attributes.find("label");
            add_text(group,label==child->attributes.end()?std::string{}:label->second);
            document.append_child(*items,group);
            for(auto* option:child->children) if(option && option->tag=="option") append_option(*option);
        }
        select_popup_apply_private_styles(*root);
        return true;
    }

    void position_select_popup(dom_node& select)
    {
        auto* root=select_popup_root(select);
        auto* panel=select_popup_panel(select);
        auto* items=select_popup_items(select);
        if(root==nullptr || panel==nullptr || items==nullptr) return;
        const auto viewport=viewport_provider();
        const auto option_count=forms::select_options(select).size();
        const auto group_count=static_cast<size_t>(std::count_if(select.children.begin(),select.children.end(),[](auto* child){return child&&child->tag=="optgroup";}));
        const auto desired_height=std::min(260.0F,8.0F+static_cast<float>(option_count)*30.0F+static_cast<float>(group_count)*27.0F);
        const auto width=std::min(std::max(140.0F,select.layout.width),std::max(80.0F,static_cast<float>(viewport.width)-8.0F));
        const auto below=static_cast<float>(viewport.height)-(select.layout.y+select.layout.height)-4.0F;
        const auto above=select.layout.y-4.0F;
        const auto height=std::max(48.0F,std::min(desired_height,std::max(below,above)));
        const auto top=below>=std::min(desired_height,120.0F)
            ?select.layout.y+select.layout.height
            :std::max(4.0F,select.layout.y-height);
        const auto left=std::clamp(select.layout.x,4.0F,std::max(4.0F,static_cast<float>(viewport.width)-width-4.0F));
        const auto background=select_popup_css_color(select.style.background_rgba,0x202124ffU);
        const auto foreground=select_popup_css_color(select.style.foreground_rgba,0xf2f2f2ffU);
        std::ostringstream style;
        style<<"display:block;position:fixed;box-sizing:border-box;left:"<<left<<"px;top:"<<top
            <<"px;width:"<<width<<"px;max-height:"<<height
            <<"px;overflow-y:auto;padding:4px;background:"<<background<<";color:"<<foreground
            <<";border:1px solid rgba(127,127,127,.45);border-radius:6px;z-index:2147483000";
        panel->attributes["style"]=style.str();
        select_popup_apply_private_styles(*root);
    }

    bool open_select_popup(dom_node& select)
    {
        if(!select_popup_is_candidate(select) || document.is_inert(select)) return false;
        if(auto* current=active_select_popup();current && current!=&select) close_select_popup(*current,false);
        if(!ensure_select_popup_template(select) || !rebuild_select_popup_rows(select)) return false;
        auto* panel=select_popup_panel(select);
        if(panel==nullptr) return false;
        panel->attributes["data-webscene-open"]="true";
        panel->attributes.erase("data-webscene-dismiss-pointer");
        panel->attributes.erase("data-webscene-pressed-option");
        panel->attributes["data-webscene-option-signature"]=select_popup_signature(select);
        auto* selected=forms::selected_option(select);
        if(selected==nullptr || forms::option_is_disabled(*selected)) {
            for(auto* option:forms::select_options(select))
                if(!forms::option_is_disabled(*option)){selected=option;break;}
        }
        if(selected) panel->attributes["data-webscene-highlighted-option"]=std::to_string(selected->id);
        else panel->attributes.erase("data-webscene-highlighted-option");
        position_select_popup(select);
        select_popup_update_row_styles(select);
        ensure_layout();
        if(selected) {
            auto* items=select_popup_items(select);
            if(items) for(auto* row:items->children) {
                if(row && select_popup_row_option_id(*row)==selected->id) {
                    const auto view_top=panel->scroll_top;
                    const auto view_bottom=view_top+panel->scroll_viewport_height;
                    const auto row_top=row->layout.y-panel->layout.y+panel->scroll_top;
                    const auto row_bottom=row_top+row->layout.height;
                    if(row_top<view_top) panel->scroll_top=std::max(0.0F,row_top);
                    else if(row_bottom>view_bottom) panel->scroll_top=std::max(0.0F,row_bottom-panel->scroll_viewport_height);
                    break;
                }
            }
        }
        document.mark_dirty();
        return true;
    }

    void close_select_popup(dom_node& select,bool pointer_dismissal)
    {
        auto* root=select_popup_root(select);
        auto* panel=select_popup_panel(select);
        if(root==nullptr || panel==nullptr) return;
        panel->attributes["data-webscene-open"]="false";
        panel->attributes.erase("data-webscene-pressed-option");
        panel->attributes.erase("data-webscene-highlighted-option");
        if(pointer_dismissal) panel->attributes["data-webscene-dismiss-pointer"]="true";
        else panel->attributes.erase("data-webscene-dismiss-pointer");
        panel->attributes["style"]="display:none;position:fixed";
        select_popup_apply_private_styles(*root);
    }

    bool select_popup_snapshot_valid(dom_node& select)
    {
        auto* panel=select_popup_panel(select);
        if(panel==nullptr) return false;
        const auto expected=panel->attributes.find("data-webscene-option-signature");
        return expected!=panel->attributes.end() && expected->second==select_popup_signature(select);
    }

    select_popup_hit select_popup_hit_from_node(dom_node* node)
    {
        select_popup_hit hit;
        for(auto* current=node;current!=nullptr;current=document.event_parent(*current)) {
            const auto row_id=current->attributes.find("data-webscene-select-option");
            if(row_id!=current->attributes.end() && hit.row==nullptr) hit.row=current;
            if(current->tag=="select" && select_popup_is_open(*current)) {
                hit.select=current;
                break;
            }
        }
        if(hit.select && hit.row) hit.option=select_popup_option_by_id(document,*hit.select,select_popup_row_option_id(*hit.row));
        return hit;
    }

    bool select_popup_move_highlight(dom_node& select,int offset,bool first=false,bool last=false)
    {
        std::vector<dom_node*> enabled;
        for(auto* option:forms::select_options(select))
            if(option && !forms::option_is_disabled(*option)) enabled.push_back(option);
        if(enabled.empty()) return true;
        auto* current=select_popup_highlighted_option(select);
        auto at=std::find(enabled.begin(),enabled.end(),current);
        size_t index=at==enabled.end()?0U:static_cast<size_t>(at-enabled.begin());
        if(first) index=0;
        else if(last) index=enabled.size()-1;
        else if(offset<0 && index>0) --index;
        else if(offset>0 && index+1<enabled.size()) ++index;
        select_popup_set_highlight(select,enabled[index]);
        ensure_layout();
        auto* panel=select_popup_panel(select);
        auto* items=select_popup_items(select);
        if(panel&&items) for(auto* row:items->children) {
            if(row && select_popup_row_option_id(*row)==enabled[index]->id) {
                const auto row_top=row->layout.y-panel->layout.y+panel->scroll_top;
                const auto row_bottom=row_top+row->layout.height;
                if(row_top<panel->scroll_top) panel->scroll_top=std::max(0.0F,row_top);
                else if(row_bottom>panel->scroll_top+panel->scroll_viewport_height)
                    panel->scroll_top=std::max(0.0F,row_bottom-panel->scroll_viewport_height);
                document.mark_dirty();
                break;
            }
        }
        return true;
    }

    bool select_popup_typeahead(dom_node& select,char key)
    {
        const auto needle=static_cast<char>(std::tolower(static_cast<unsigned char>(key)));
        auto options=forms::select_options(select);
        auto* current=select_popup_highlighted_option(select);
        auto start=std::find(options.begin(),options.end(),current);
        size_t begin=start==options.end()?0U:(static_cast<size_t>(start-options.begin())+1U)%std::max<size_t>(1U,options.size());
        for(size_t offset=0;offset<options.size();++offset) {
            auto* option=options[(begin+offset)%options.size()];
            if(forms::option_is_disabled(*option)) continue;
            const auto label=forms::option_label(*option);
            if(!label.empty() && static_cast<char>(std::tolower(static_cast<unsigned char>(label.front())))==needle) {
                select_popup_set_highlight(select,option);
                return true;
            }
        }
        return true;
    }

    bool select_popup_commit(dom_node& select,const webscene_input_event& input,dom_node* option=nullptr)
    {
        if(option==nullptr) option=select_popup_highlighted_option(select);
        if(option==nullptr || forms::option_is_disabled(*option)) {close_select_popup(select,false);return true;}
        const auto changed=forms::select_option(select,*option);
        close_select_popup(select,false);
        document.mark_dirty();
        if(!changed) return true;
        return dispatch_input_event_type(input,"input",select)
            && dispatch_input_event_type(input,"change",select);
    }

    std::optional<bool> dispatch_select_popup_pointer_input(
        const webscene_input_event& input,dom_node* hit_target)
    {
        if(auto* pending=select_popup_pending_dismissal()) {
            auto* panel=select_popup_panel(*pending);
            if(input.kind==WEBSCENE_INPUT_POINTER_UP) {
                if(panel) panel->attributes.erase("data-webscene-dismiss-pointer");
                return true;
            }
            if(input.kind==WEBSCENE_INPUT_POINTER_MOVE || input.kind==WEBSCENE_INPUT_POINTER_DOWN) return true;
        }
        auto* select=active_select_popup();
        if(select==nullptr) return std::nullopt;
        position_select_popup(*select);
        if(!select_popup_snapshot_valid(*select)) {
            close_select_popup(*select,input.kind==WEBSCENE_INPUT_POINTER_DOWN);
            return input.kind==WEBSCENE_INPUT_POINTER_DOWN?std::optional<bool>(true):std::nullopt;
        }
        const auto hit=select_popup_hit_from_node(hit_target);
        const auto inside=hit.select==select;
        auto* panel=select_popup_panel(*select);
        if(input.kind==WEBSCENE_INPUT_WHEEL && inside) {
            if(panel) {
                ensure_layout();
                const auto maximum=std::max(0.0F,panel->scroll_content_height-panel->scroll_viewport_height);
                panel->scroll_top=std::clamp(panel->scroll_top+static_cast<float>(input.delta_y),0.0F,maximum);
                document.mark_dirty();
            }
            return true;
        }
        if(input.kind==WEBSCENE_INPUT_POINTER_MOVE && inside) {
            if(hit.option && !forms::option_is_disabled(*hit.option)) select_popup_set_highlight(*select,hit.option);
            return true;
        }
        if(input.kind==WEBSCENE_INPUT_POINTER_DOWN) {
            if(!inside) {close_select_popup(*select,true);return true;}
            if(panel && hit.option && !forms::option_is_disabled(*hit.option))
                panel->attributes["data-webscene-pressed-option"]=std::to_string(hit.option->id);
            return true;
        }
        if(input.kind==WEBSCENE_INPUT_POINTER_UP && inside) {
            uint32_t pressed=0;
            if(panel) {
                const auto value=panel->attributes.find("data-webscene-pressed-option");
                if(value!=panel->attributes.end()) {
                    const auto* first=value->second.data();const auto* last=first+value->second.size();
                    std::from_chars(first,last,pressed);
                }
                panel->attributes.erase("data-webscene-pressed-option");
            }
            if(hit.option && hit.option->id==pressed && !forms::option_is_disabled(*hit.option))
                return select_popup_commit(*select,input,hit.option);
            return true;
        }
        return inside?std::optional<bool>(true):std::nullopt;
    }
