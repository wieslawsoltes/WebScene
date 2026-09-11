#include "webscene/shared_css.hpp"
#include "webscene_css_native_session.h"

namespace webscene::native_web {
namespace {
class shared_resolver final : public stylesheet_resolver {
  std::vector<webscene_native::css::prepared_stylesheet> sheets_;
  std::shared_ptr<shared_css_report> report_;
  std::unique_ptr<webscene_native::css::native_style_session> session_;
public:
  shared_resolver(std::vector<webscene_native::css::prepared_stylesheet> sheets,
                  std::shared_ptr<shared_css_report> report)
      : sheets_(std::move(sheets)),report_(std::move(report)) {}
  void resolve(webscene_native::native_document& document,
               const style_environment& environment) override {
    if(!session_) {
      session_=std::make_unique<webscene_native::css::native_style_session>(document,true);
      uint32_t id=1;
      for(const auto& sheet:sheets_) session_->replace(id++,sheet);
    }
    if(report_) {
      report_->diagnostics.clear();
      for(const auto& sheet:sheets_)
        report_->diagnostics.insert(report_->diagnostics.end(),
            sheet.diagnostics.begin(),sheet.diagnostics.end());
    }
    session_->set_environment({environment.width,environment.height,environment.dark_color_scheme,environment.reduced_motion});
    session_->set_interaction(document.find_by_native_id(environment.hover),
        document.find_by_native_id(environment.focus),environment.focus_visible);
    // document::render calls this only for style/DOM/environment invalidation.
    session_->invalidate();
    session_->flush([](const auto&,auto&,auto&,auto&) {return false;},
        [&](const auto& declaration,const auto& result) {
          if(report_ && result.classification!="supported")
            report_->diagnostics.push_back({declaration.name,result.classification,result.semantic_slice});
        });
  }
};
}
std::unique_ptr<stylesheet_resolver> make_shared_stylesheet_resolver(
    std::vector<webscene_native::css::prepared_stylesheet> sheets,
    std::shared_ptr<shared_css_report> report) {
  return std::make_unique<shared_resolver>(std::move(sheets),std::move(report));
}
}
