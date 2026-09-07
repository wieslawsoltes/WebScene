#pragma once
#include <webgpu/webgpu_cpp.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace webscene::graphics {
// Owned diagnostic data copied before Dawn's callback-owned pointers expire.
// Source positions remain Dawn offsets here; the V8 adapter must convert to
// WebGPU's UTF-16 coordinates using the original shader source.
struct webgpu_compilation_message {
    std::string message;
    wgpu::CompilationMessageType type;
    uint64_t line_num, line_pos, offset, length;
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
            remaining-=length;
        }
        return result;
    }
};
}
