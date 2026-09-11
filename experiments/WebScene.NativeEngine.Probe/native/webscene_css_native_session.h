#pragma once
#include "webscene_css_native_cascade.h"

namespace webscene_native::css {
// The document outlives the session. All calls and callbacks run on its owning
// thread. DOM mutators must call invalidate(); flush once before layout/render.
class native_style_session {
    native_document& document_;
    stylesheet_owner sheets_;
    query_host query_;
    media_environment environment_{0,0,false};
    uint32_t hover_id_{},focus_id_{};
    bool focus_visible_{};
    bool pending_{true};
public:
    explicit native_style_session(native_document& document):document_(document),query_(document) {}
    native_style_session(const native_style_session&)=delete;
    native_style_session& operator=(const native_style_session&)=delete;
    void replace(uint32_t id,prepared_stylesheet sheet) {
        sheets_.replace(id,std::move(sheet)); invalidate();
    }
    bool remove(uint32_t id) {
        if(!sheets_.remove(id)) return false;
        invalidate(); return true;
    }
    void invalidate() noexcept { pending_=true; }
    bool pending() const noexcept { return pending_; }
    void set_environment(media_environment environment) {
        if(environment.width==environment_.width && environment.height==environment_.height &&
           environment.dark==environment_.dark &&
           environment.reduced_motion==environment_.reduced_motion) return;
        environment_=environment;
        sheets_.set_environment(environment);
        // Geometry can change even when no media query changes activation.
        document_.mark_dirty();
        invalidate();
    }
    void set_interaction(const dom_node* hover,const dom_node* focus,bool visible) {
        const auto hover_id=hover?hover->id:0;
        const auto focus_id=focus?focus->id:0;
        if(hover_id==hover_id_ && focus_id==focus_id_ && visible==focus_visible_) return;
        hover_id_=hover_id;focus_id_=focus_id;focus_visible_=visible;
        query_.set_interaction(hover,focus,visible);
        invalidate();
    }
    void set_target_hash(std::string hash) {
        query_.set_target_hash(std::move(hash)); invalidate();
    }
    // Returns whether a refresh ran, not whether layout changed. A pending flag
    // raised from a callback survives the pass. Callbacks must not mutate DOM/sheets.
    template<class LoadSvg,class Observe>
    bool flush(LoadSvg&& load_svg,Observe&& observe) {
        if(!pending_) return false;
        pending_=false;
        try {
            apply_native_document_cascade(document_,sheets_,query_,load_svg,observe);
        } catch(...) {
            pending_=true;
            throw;
        }
        return true;
    }
};
} // namespace webscene_native::css
