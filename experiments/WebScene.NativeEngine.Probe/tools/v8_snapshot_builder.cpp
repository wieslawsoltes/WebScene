#include <libplatform/libplatform.h>
#include <v8.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string read_text(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to open " + path.string());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

void write_bytes(const std::filesystem::path& path, const char* data, size_t size)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("unable to create " + path.string());
    stream.write(data, static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("unable to write " + path.string());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: webscene_v8_snapshot_builder <icu> <source.js> <blob> <metadata>\n";
        return 2;
    }

    try {
        const std::filesystem::path icu_path{argv[1]};
        const std::filesystem::path source_path{argv[2]};
        const std::filesystem::path blob_path{argv[3]};
        const std::filesystem::path metadata_path{argv[4]};
        const auto source = read_text(source_path);

        if (!v8::V8::InitializeICU(icu_path.string().c_str())) {
            throw std::runtime_error("unable to initialize ICU from " + icu_path.string());
        }
        auto platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(platform.get());
        v8::V8::Initialize();

        auto allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(
            v8::ArrayBuffer::Allocator::NewDefaultAllocator());
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = allocator.get();
        v8::SnapshotCreator creator(params);
        auto* isolate = creator.GetIsolate();
        {
            v8::HandleScope handle_scope(isolate);
            auto context = v8::Context::New(isolate);
            v8::Context::Scope context_scope(context);
            auto text = v8::String::NewFromUtf8(
                isolate,
                source.data(),
                v8::NewStringType::kNormal,
                static_cast<int>(source.size())).ToLocalChecked();
            auto script = v8::Script::Compile(context, text).ToLocalChecked();
            script->Run(context).ToLocalChecked();
            creator.SetDefaultContext(context);
        }

        auto blob = creator.CreateBlob(
            v8::SnapshotCreator::FunctionCodeHandling::kKeep);
        if (blob.data == nullptr || blob.raw_size <= 0) {
            throw std::runtime_error("V8 returned an empty startup snapshot");
        }
        write_bytes(blob_path, blob.data, static_cast<size_t>(blob.raw_size));
        delete[] blob.data;
        write_bytes(
            metadata_path,
            WEBSCENE_V8_SNAPSHOT_FINGERPRINT,
            std::char_traits<char>::length(WEBSCENE_V8_SNAPSHOT_FINGERPRINT));

        std::cout << "snapshot_bytes=" << blob.raw_size
                  << " v8=" << v8::V8::GetVersion() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
