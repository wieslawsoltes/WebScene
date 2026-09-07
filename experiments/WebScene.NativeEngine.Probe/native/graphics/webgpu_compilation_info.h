#pragma once
#include <webgpu/webgpu_cpp.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace webscene::graphics {
// Owned diagnostic data copied before Dawn's callback-owned pointers expire.
// Base positions are Dawn byte offsets. The pinned backend also supplies
// UTF-16 positions through DawnCompilationMessageUtf16; preserve them explicitly
// so the V8 adapter never accidentally exposes byte offsets as WebGPU offsets.
struct webgpu_compilation_message {
    std::string message;
    wgpu::CompilationMessageType type;
    uint64_t line_num, line_pos, offset, length;
    bool has_utf16{};
    uint64_t utf16_line_pos{},utf16_offset{},utf16_length{};
};
struct webgpu_compilation_info {
    std::vector<webgpu_compilation_message> messages;
    static webgpu_compilation_info copy(const wgpu::CompilationInfo& source,
        size_t maximum_messages=1024,size_t maximum_bytes=1024*1024) {
        if(source.messageCount>maximum_messages)
            throw std::length_error("Shader diagnostic count exceeds budget");
        if(source.messageCount&&!source.messages)
            throw std::invalid_argument("Shader diagnostics lack message storage");
        webgpu_compilation_info result;
        result.messages.reserve(source.messageCount);
        size_t remaining=maximum_bytes;
        for(size_t i=0;i<source.messageCount;++i) {
            const auto& input=source.messages[i];
            auto length=input.message.length;
            if(length==WGPU_STRLEN) {
                length=0;
                if(input.message.data)
                    while(length<=remaining&&input.message.data[length])++length;
            }
            if(length>remaining)throw std::length_error("Shader diagnostic text exceeds budget");
            if(length&&!input.message.data)throw std::invalid_argument("Shader diagnostic text is null");
            result.messages.push_back({length?std::string(input.message.data,length):std::string{},
                input.type,input.lineNum,input.linePos,input.offset,input.length});
            auto& copied=result.messages.back();
            size_t chain_length=0;
            for(auto* chain=input.nextInChain;chain;chain=chain->nextInChain) {
                if(++chain_length>16)throw std::length_error("Shader diagnostic extension chain exceeds budget");
                if(chain->sType!=wgpu::SType::DawnCompilationMessageUtf16)continue;
                if(copied.has_utf16)throw std::invalid_argument("Duplicate UTF-16 diagnostic extension");
                const auto* utf16=static_cast<const wgpu::DawnCompilationMessageUtf16*>(chain);
                copied.has_utf16=true;
                copied.utf16_line_pos=utf16->linePos;
                copied.utf16_offset=utf16->offset;
                copied.utf16_length=utf16->length;
            }
            remaining-=length;
        }
        return result;
    }
};
}
