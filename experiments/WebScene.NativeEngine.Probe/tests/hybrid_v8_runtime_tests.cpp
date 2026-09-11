#include "webscene_v8_runtime.h"
#include "webscene_native_dom.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void test_compiled_template_shared_document() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    webscene_native::dom_node* constructed = nullptr;
    runtime.register_compiled_template("button", [&](auto& dom, const std::string& data) -> auto& {
        require(&dom == &document, "Template received a different document");
        require(data == R"({"label":"Native"})", "Structured template data changed");
        auto& node = dom.create_element("button");
        node.attributes["id"] = "native-button";
        node.id_attribute = "native-button";
        node.text_content = "Native";
        constructed = &node;
        return node;
    });
    bool duplicate_rejected = false;
    try { runtime.register_compiled_template("button", [](auto& dom, const auto&) -> auto& {
        return dom.body();
    }); } catch (const std::invalid_argument&) { duplicate_rejected = true; }
    require(duplicate_rejected, "Duplicate factory replaced registered template");
    require(runtime.initialize(), "Compiled template runtime failed");
    require(runtime.execute(R"JS(
        const decoder = new TextDecoder('windows-1252', {fatal:true});
        if(decoder.decode(new Uint8Array([0x41,0x80,0x81,0x91,0x9f,0xff])) !== 'A\u20ac\u0081\u2018\u0178\u00ff')
            throw Error('DXF Windows-1252 decoding mismatch');
    )JS", "windows-1252"), runtime.last_error().c_str());
    require(runtime.execute(R"JS(
        const nativeButton = document.createCompiledTemplate('button', {label:'Native'});
        document.body.appendChild(nativeButton);
        if(document.getElementById('native-button') !== nativeButton) throw Error('Identity lost');
        let clicks = 0;
        const listener = () => { clicks++; nativeButton.textContent = 'Clicked'; };
        nativeButton.addEventListener('click', listener);
        nativeButton.click();
        nativeButton.removeEventListener('click', listener);
        nativeButton.click();
        if(clicks !== 1) throw Error('Listener cleanup failed');
        nativeButton.focus();
        if(document.activeElement !== nativeButton) throw Error('Native template focus failed');
        const compatibility = document.createElement('section');
        compatibility.innerHTML = '<b id="runtime-html">Compatible</b>';
        document.body.appendChild(compatibility);
        if(!document.getElementById('runtime-html')) throw Error('Runtime HTML broken');
        const fragment = document.createDocumentFragment();
        const moved = document.createElement('button');
        fragment.appendChild(moved);
        compatibility.replaceChildren('before', fragment, 'after');
        if(fragment.childNodes.length || moved.parentNode !== compatibility || compatibility.childNodes.length !== 3)
            throw Error('replaceChildren did not flatten fragment and preserve text arguments');
        let cycleRejected = false;
        try { compatibility.replaceChildren(compatibility); } catch(e) { cycleRejected = true; }
        if(!cycleRejected || compatibility.childNodes.length !== 3) throw Error('Invalid replacement was destructive');
        compatibility.insertAdjacentHTML('beforeend', '<b id="runtime-html">Compatible</b>');

        let rejected = false;
        try { document.createCompiledTemplate('missing'); } catch(e) { rejected = true; }
        if(!rejected) throw Error('Missing template silently accepted');
    )JS", "compiled-template-shared-document"), runtime.last_error().c_str());
    require(constructed && constructed->children.size() == 1 && constructed->children.front()->text_content == "Clicked", "Native side cannot see JS mutation");
    require(document.find_by_id("runtime-html") != nullptr, "Native side cannot see runtime HTML");
    require(runtime.execute("nativeButton.remove();", "compiled-template-remove"), "Template removal failed");
    require(constructed->parent == nullptr, "Native side cannot see JS removal");
}

void test_blob_worker_source_lifetime() {
    webscene_native::native_document document;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};});
    bool received = false;
    runtime.register_compiled_template("worker-result", [&](auto& dom, const std::string& value) -> auto& {
        require(value == "42", "Worker result changed"); received = true;
        return dom.create_element("span");
    });
    require(runtime.initialize(), "Blob worker runtime failed");
    require(runtime.execute(R"JS(
        const workerURL = URL.createObjectURL(new Blob(['onmessage=e=>postMessage(e.data+1)'], {type:'text/javascript'}));
        const blobWorker = new Worker(workerURL);
        blobWorker.onmessage = e => document.createCompiledTemplate('worker-result',e.data);
        URL.revokeObjectURL(workerURL);
        let revokedRejected = false;
        try { new Worker(workerURL); } catch(e) { revokedRejected = true; }
        if(!revokedRejected) throw Error('Revoked worker URL accepted');
        blobWorker.postMessage(41);
    )JS", "blob-worker-lifetime"), runtime.last_error().c_str());
    for(unsigned i=0;i<2000 && !received;++i) {
        runtime.pump_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(received, "Worker lost source after URL revocation");
    require(runtime.execute("blobWorker.terminate()", "worker-cleanup"), runtime.last_error().c_str());
}

void test_compiled_document_lifecycle(bool strict) {
    webscene_native::native_document document;
    unsigned document_requests = 0;
    webscene_native::v8_dom_runtime runtime(document,
        []{return webscene_native::v8_dom_runtime::viewport_metrics{640,480,1,0};}, {},
        [&](uint32_t kind, const std::string&, const auto&, const std::string&, int64_t, auto&) {
            if (kind == WEBSCENE_RESOURCE_DOCUMENT) ++document_requests;
            return false;
        });
    require(runtime.initialize(), "Compiled root runtime failed");
    webscene_native::v8_dom_runtime::compiled_document package;
    package.allow_runtime_html = !strict;
    package.base_url = "https://compiled.test/app/index.html";
    package.construct = [](auto& dom) {
        auto& html = dom.create_element("html");
        auto& head = dom.create_element("head");
        auto& body = dom.create_element("body");
        auto& button = dom.create_element("button");
        button.id_attribute = "compiled-root-button";
        button.attributes["id"] = button.id_attribute;
        dom.append_child(dom.body(), html);
        dom.append_child(html, head);
        dom.append_child(html, body);
        dom.append_child(body, button);
    };
    package.templates.emplace("parts", [](auto& dom, webscene_native::compiled_template_arguments args) -> auto& {
        auto& fragment = dom.create_element("#document-fragment");
        auto& span = dom.create_element("span");
        webscene_native::compiled_append_argument(dom, span, webscene_native::compiled_argument(args, 0));
        dom.append_child(fragment, span);
        return fragment;
    });
    package.scripts.push_back({{}, R"JS(
        if(!document.getElementById('compiled-root-button')) throw Error('Scripts ran before construction');
        if(document.readyState !== 'loading') throw Error('Incorrect initial lifecycle');
        globalThis.lifecycle = ['script'];
        document.addEventListener('DOMContentLoaded', () => lifecycle.push('dom'));
        window.addEventListener('load', () => lifecycle.push('load'));
    )JS"});
    require(runtime.load_compiled_document(package), runtime.last_error().c_str());
    require(document_requests == 0, "Compiled navigation fetched an HTML shell");
    require(runtime.execute(R"JS(
        const input = document.createDocumentFragment();
        const child = document.createElement('em'); child.textContent = 'native parts';
        input.appendChild(child);
        const output = document.createCompiledTemplateParts('parts', [input]);
        if(input.childNodes.length || output.firstChild.firstChild !== child) throw Error('Native parts lost node identity');
        document.body.appendChild(output);
        if(!child.isConnected || output.childNodes.length) throw Error('Native parts not attached');
        let rejected = false;
        try { document.createCompiledTemplateParts('parts', [child]); } catch(e) { rejected = true; }
        if(!rejected || !child.isConnected) throw Error('Attached argument was moved or accepted');
        if(lifecycle.join(',') !== 'script,dom,load') throw Error('Lifecycle order');
        if(document.readyState !== 'complete') throw Error('Readiness incomplete');
        if(location.href !== 'https://compiled.test/app/index.html') throw Error('Base URL lost');

    )JS", "compiled-document-lifecycle"), runtime.last_error().c_str());
    if (strict) {
        require(runtime.execute(R"JS(
            const control = document.getElementById('compiled-root-button');
            control.textContent = 'Preserved';
            const attempts = [
                () => { control.innerHTML = '<b>Forbidden</b>'; },
                () => control.insertAdjacentHTML('beforeend', '<b>Forbidden</b>'),
                () => new DOMParser().parseFromString('<b>Forbidden</b>', 'text/html'),
                () => document.createRange().createContextualFragment('<b>Forbidden</b>')
            ];
            for(const attempt of attempts) {
                let rejected = false;
                try { attempt(); } catch(e) { rejected = String(e).includes('Runtime HTML'); }
                if(!rejected) throw Error('Strict parser entry not rejected');
                if(control.textContent !== 'Preserved') throw Error('Rejected parse mutated existing content');
            }
            control.innerHTML = '';
            if(control.textContent !== '') throw Error('Empty clear should not require parsing');
        )JS", "compiled-document-strict"), runtime.last_error().c_str());
        require(!runtime.load_url("https://compiled.test/runtime.html"), "Strict document allowed HTML navigation");
    } else {
        require(runtime.execute("document.getElementById('compiled-root-button').innerHTML = '<span>Compatible</span>';",
            "compiled-document-compatible"), runtime.last_error().c_str());
    }
}

int main() {
    try { test_blob_worker_source_lifetime(); test_compiled_template_shared_document(); test_compiled_document_lifecycle(false); test_compiled_document_lifecycle(true); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
