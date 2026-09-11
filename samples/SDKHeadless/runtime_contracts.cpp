#include <webscene/compiled_document.hpp>
#include <webscene_native_dom.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate> void until(Predicate predicate,const char* message) {
  const auto deadline=std::chrono::steady_clock::now()+10s;
  while(!predicate()) {require(std::chrono::steady_clock::now()<deadline,message);std::this_thread::sleep_for(1ms);}
}
using engine_ptr=std::unique_ptr<webscene_engine,decltype(&webscene_engine_destroy)>;
engine_ptr engine() {return {webscene_engine_create(0),webscene_engine_destroy};}
bool load(webscene_engine* engine,const char* name="app") {
  webscene_input_event viewport{WEBSCENE_INPUT_RESIZE,0,1,640,480,1,0};
  constexpr auto url="file:///sdk-contract/index.html";
  return webscene_engine_load_compiled_document_v1(engine,name,std::char_traits<char>::length(name),
    url,std::char_traits<char>::length(url),&viewport)!=0;
}
int main() {
  for(unsigned iteration=0;iteration<3;++iteration) {
    auto first=engine(),second=engine();require(bool(first)&&bool(second),"Engine construction failed");
    require(!load(first.get()),"Unknown package accepted");
    require(!webscene::register_compiled_document(first.get(),"",{}),"Invalid registration accepted");
    std::atomic<unsigned> original_calls{},replacement_calls{},other_calls{};
    const auto owner=std::this_thread::get_id();
    std::atomic<bool> wrong_thread{};
    std::promise<void> entered,release;
    auto gate=release.get_future().share();
    auto lifetime=std::make_shared<int>(42);std::weak_ptr<int> weak=lifetime;
    webscene::compiled_document original;
    original.construct=[&,gate,lifetime](auto& document) {
      wrong_thread=std::this_thread::get_id()==owner;
      ++original_calls;entered.set_value();gate.wait();
      auto& text=document.create_element("div");text.text_content="Original";
      document.append_child(document.body(),text);
    };
    require(webscene::register_compiled_document(first.get(),"app",original),"Registration failed");
    original={};lifetime.reset();
    require(load(first.get()),"Queued package rejected");
    require(entered.get_future().wait_for(10s)==std::future_status::ready,"Constructor did not run");
    webscene::compiled_document replacement;replacement.construct=[&](auto&){++replacement_calls;};
    require(webscene::register_compiled_document(first.get(),"app",replacement),"Replacement failed");
    require(!weak.expired(),"Queued constructor lost captured ownership");
    webscene::compiled_document other;other.construct=[&](auto&){++other_calls;};
    require(webscene::register_compiled_document(second.get(),"app",other),"Second engine registration failed");
    require(load(second.get()),"Second engine load failed");
    until([&]{return other_calls==1;},"Engines did not make independent progress");
    release.set_value();
    require(load(first.get()),"Replacement load failed");
    until([&]{return replacement_calls==1;},"Replacement constructor did not run");
    require(original_calls==1 && !wrong_thread,"Package construction owner or snapshot changed");
    // Detaching must wait for a producer callback before releasing host state.
    struct observer {std::atomic<unsigned> calls{};std::promise<void> entered,release;std::shared_future<void> gate=release.get_future().share();} observed;
    webscene_engine_set_work_available_callback_v1(first.get(),[](void* data){auto& state=*static_cast<observer*>(data);if(++state.calls==1){state.entered.set_value();state.gate.wait();}},&observed);
    require(load(first.get()),"Observer test load failed");
    require(observed.entered.get_future().wait_for(10s)==std::future_status::ready,"Host observer was not notified");
    auto detach=std::async(std::launch::async,[&]{webscene_engine_set_work_available_callback_v1(first.get(),nullptr,nullptr);});
    const bool waited=detach.wait_for(20ms)==std::future_status::timeout;
    observed.release.set_value();
    require(detach.wait_for(10s)==std::future_status::ready,"Observer detach did not finish");
    require(waited,"Observer detach returned while callback held host state");
    const auto detached=observed.calls.load();
    require(load(first.get()),"Post-detach load failed");
    until([&]{return replacement_calls>=3;},"Post-detach constructor missing");
    require(observed.calls==detached,"Detached observer was called");
    // A throwing application constructor must report failure without terminating.
    webscene::compiled_document broken;broken.construct=[](auto&){throw std::runtime_error("construction sentinel");};
    require(webscene::register_compiled_document(first.get(),"broken",broken)&&load(first.get(),"broken"),"Failure test could not queue");
    until([&]{char error[2048]{};webscene_engine_copy_last_error(first.get(),error,sizeof(error));return std::string(error).find("construction sentinel")!=std::string::npos;},"Construction failure was not reported");
    first.reset();second.reset();
    require(weak.expired(),"Engine teardown retained application callback state");
  }
  // Shutdown while application construction is already executing joins it.
  auto value=engine();require(bool(value),"Shutdown engine failed");
  std::promise<void> entered,release;auto gate=release.get_future().share();
  webscene::compiled_document package;package.construct=[&](auto&){entered.set_value();gate.wait();};
  require(webscene::register_compiled_document(value.get(),"app",package)&&load(value.get()),"Shutdown package failed");
  require(entered.get_future().wait_for(10s)==std::future_status::ready,"Shutdown constructor missing");
  auto shutdown=std::async(std::launch::async,[engine=std::move(value)]() mutable {engine.reset();});
  require(shutdown.wait_for(20ms)==std::future_status::timeout,"Shutdown did not join pending construction");
  release.set_value();require(shutdown.wait_for(10s)==std::future_status::ready,"Shutdown did not retire queued work");
  std::cout<<"Installed reusable runtime ownership contracts passed\n";
}
