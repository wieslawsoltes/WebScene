#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>
#import <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "webscene_native_engine.h"

namespace {

thread_local std::string last_error;

struct ResourceContext {
    NSURLSession* session;
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<std::string>> pending;
    std::atomic<uint64_t> successful_resource_requests{0};

    ResourceContext()
    {
        NSURLSessionConfiguration* configuration =
            [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.timeoutIntervalForRequest = 45;
        configuration.timeoutIntervalForResource = 90;
        configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        session = [NSURLSession sessionWithConfiguration:configuration];
    }

    ~ResourceContext()
    {
        [session invalidateAndCancel];
    }
};

using get_abi_version_fn = uint32_t (*)(void);
using prewarm_fn = uint8_t (*)(void);
using create_fn = webscene_engine* (*)(const webscene_engine_options*);
using destroy_fn = void (*)(webscene_engine*);

struct RuntimeApi {
    void* module{};
    get_abi_version_fn get_abi_version{};
    prewarm_fn prewarm{};
    create_fn create{};
    destroy_fn destroy{};
};

std::mutex engines_mutex;
std::unordered_map<webscene_engine*, std::unique_ptr<ResourceContext>> engines;
RuntimeApi runtime;

template <typename T>
T resolve(void* module, const char* name)
{
    return reinterpret_cast<T>(dlsym(module, name));
}

bool ensure_runtime(const char* path)
{
    if (runtime.module != nullptr) return true;
    if (path == nullptr || path[0] == '\0') {
        last_error = "The WebScene native library path is empty.";
        return false;
    }

    auto* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        last_error = dlerror() ?: "dlopen failed.";
        return false;
    }

    RuntimeApi loaded{
        module,
        resolve<get_abi_version_fn>(module, "webscene_engine_get_abi_version"),
        resolve<prewarm_fn>(module, "webscene_engine_prewarm"),
        resolve<create_fn>(module, "webscene_engine_create_with_options"),
        resolve<destroy_fn>(module, "webscene_engine_destroy"),
    };
    if (loaded.get_abi_version == nullptr || loaded.prewarm == nullptr
        || loaded.create == nullptr || loaded.destroy == nullptr) {
        last_error = "The selected library does not export the WebScene engine ABI.";
        dlclose(module);
        return false;
    }
    if (loaded.get_abi_version() != 2) {
        last_error = "The selected WebScene library does not implement ABI version 2.";
        dlclose(module);
        return false;
    }
    runtime = loaded;
    return true;
}

int64_t unix_seconds_from_http_date(NSString* value)
{
    if (value.length == 0) return 0;
    static NSDateFormatter* formatter;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      formatter = [[NSDateFormatter alloc] init];
      formatter.locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
      formatter.timeZone = [NSTimeZone timeZoneWithAbbreviation:@"GMT"];
      formatter.dateFormat = @"EEE',' dd MMM yyyy HH':'mm':'ss 'GMT'";
    });
    NSDate* date = [formatter dateFromString:value];
    return date == nil ? 0 : static_cast<int64_t>(date.timeIntervalSince1970);
}

NSString* header(NSHTTPURLResponse* response, NSString* name)
{
    for (id key in response.allHeaderFields) {
        if ([[key description] caseInsensitiveCompare:name] == NSOrderedSame) {
            return [[response.allHeaderFields objectForKey:key] description];
        }
    }
    return nil;
}

int64_t fresh_until(NSHTTPURLResponse* response)
{
    NSString* cache_control = header(response, @"Cache-Control");
    if (cache_control != nil) {
        NSRegularExpression* expression = [NSRegularExpression
            regularExpressionWithPattern:@"(?:^|,)\\s*max-age\\s*=\\s*([0-9]+)"
            options:NSRegularExpressionCaseInsensitive
            error:nil];
        NSTextCheckingResult* match =
            [expression firstMatchInString:cache_control
                                   options:0
                                     range:NSMakeRange(0, cache_control.length)];
        if (match.numberOfRanges > 1) {
            NSString* seconds = [cache_control substringWithRange:[match rangeAtIndex:1]];
            return static_cast<int64_t>(NSDate.date.timeIntervalSince1970)
                + seconds.longLongValue;
        }
    }
    return unix_seconds_from_http_date(header(response, @"Expires"));
}

bool is_cacheable(NSHTTPURLResponse* response)
{
    NSString* value = header(response, @"Cache-Control");
    return value == nil
        || [value rangeOfString:@"no-store" options:NSCaseInsensitiveSearch].location
            == NSNotFound;
}

void append_u32(std::string& target, uint32_t value)
{
    target.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

void append_i64(std::string& target, int64_t value)
{
    target.append(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::shared_ptr<std::string> load_response(
    ResourceContext* context,
    std::string_view address,
    std::string_view entity_tag,
    int64_t modified)
{
    NSString* address_string =
        [[NSString alloc] initWithBytes:address.data()
                                length:address.size()
                              encoding:NSUTF8StringEncoding];
    NSURL* url = address_string == nil ? nil : [NSURL URLWithString:address_string];
    if (url == nil) {
        last_error = "The engine requested an invalid resource URL.";
        return {};
    }

    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    request.HTTPMethod = @"GET";
    [request setValue:@"WebScene-Flutter/0.1" forHTTPHeaderField:@"User-Agent"];
    if (!entity_tag.empty()) {
        NSString* value =
            [[NSString alloc] initWithBytes:entity_tag.data()
                                    length:entity_tag.size()
                                  encoding:NSUTF8StringEncoding];
        if (value != nil) [request setValue:value forHTTPHeaderField:@"If-None-Match"];
    }
    if (modified > 0) {
        NSDate* date = [NSDate dateWithTimeIntervalSince1970:modified];
        static NSDateFormatter* formatter;
        static dispatch_once_t once;
        dispatch_once(&once, ^{
          formatter = [[NSDateFormatter alloc] init];
          formatter.locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
          formatter.timeZone = [NSTimeZone timeZoneWithAbbreviation:@"GMT"];
          formatter.dateFormat = @"EEE',' dd MMM yyyy HH':'mm':'ss 'GMT'";
        });
        [request setValue:[formatter stringFromDate:date]
            forHTTPHeaderField:@"If-Modified-Since"];
    }

    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    __block NSData* body;
    __block NSHTTPURLResponse* response;
    __block NSError* request_error;
    NSURLSessionDataTask* task = [context->session
        dataTaskWithRequest:request
          completionHandler:^(NSData* data, NSURLResponse* result, NSError* error) {
            body = data;
            response = [result isKindOfClass:NSHTTPURLResponse.class]
                ? static_cast<NSHTTPURLResponse*>(result)
                : nil;
            request_error = error;
            dispatch_semaphore_signal(completed);
          }];
    [task resume];
    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);

    if (request_error != nil || response == nil) {
        last_error = request_error == nil
            ? "The resource request did not produce an HTTP response."
            : request_error.localizedDescription.UTF8String;
        return {};
    }
    const NSInteger status_code = response.statusCode;
    if (status_code != 304 && (status_code < 200 || status_code >= 300)) {
        last_error = "HTTP " + std::to_string(status_code) + " loading "
            + std::string(address);
        return {};
    }

    NSString* response_tag = header(response, @"ETag") ?: @"";
    NSData* tag_data = [response_tag dataUsingEncoding:NSUTF8StringEncoding] ?: NSData.data;
    NSData* content = status_code == 304 ? NSData.data : (body ?: NSData.data);
    auto envelope = std::make_shared<std::string>();
    envelope->reserve(22 + tag_data.length + content.length);
    envelope->push_back(status_code == 304 ? '\2' : '\1');
    envelope->push_back(is_cacheable(response) ? '\1' : '\0');
    append_u32(*envelope, static_cast<uint32_t>(tag_data.length));
    append_i64(*envelope, unix_seconds_from_http_date(header(response, @"Last-Modified")));
    append_i64(*envelope, fresh_until(response));
    envelope->append(
        static_cast<const char*>(tag_data.bytes),
        static_cast<size_t>(tag_data.length));
    envelope->append(
        static_cast<const char*>(content.bytes),
        static_cast<size_t>(content.length));
    context->successful_resource_requests.fetch_add(1, std::memory_order_relaxed);
    return envelope;
}

size_t resource_load(
    void* user_data,
    uint32_t,
    const char* url,
    size_t url_length,
    const char* entity_tag,
    size_t entity_tag_length,
    int64_t last_modified,
    char* destination,
    size_t capacity)
{
    @autoreleasepool {
        auto* context = static_cast<ResourceContext*>(user_data);
        if (context == nullptr || url == nullptr || url_length == 0) return 0;
        std::string key(url, url_length);
        key.push_back('\n');
        if (entity_tag != nullptr) key.append(entity_tag, entity_tag_length);
        key.push_back('\n');
        key += std::to_string(last_modified);

        std::shared_ptr<std::string> envelope;
        {
            std::lock_guard lock(context->mutex);
            auto found = context->pending.find(key);
            if (found != context->pending.end()) envelope = found->second;
        }
        if (!envelope) {
            envelope = load_response(
                context,
                std::string_view(url, url_length),
                entity_tag == nullptr
                    ? std::string_view()
                    : std::string_view(entity_tag, entity_tag_length),
                last_modified);
            if (!envelope) return 0;
            std::lock_guard lock(context->mutex);
            context->pending[key] = envelope;
        }
        if (destination == nullptr || capacity < envelope->size()) {
            return envelope->size();
        }
        std::memcpy(destination, envelope->data(), envelope->size());
        {
            std::lock_guard lock(context->mutex);
            context->pending.erase(key);
        }
        return envelope->size();
    }
}

uint8_t measure_text(
    void*,
    const char* text,
    size_t text_length,
    const char* font_family,
    size_t font_family_length,
    float font_size,
    int32_t font_weight,
    float letter_spacing,
    float word_spacing,
    webscene_text_metrics* metrics)
{
    @autoreleasepool {
        if (text == nullptr || metrics == nullptr
            || metrics->struct_size < sizeof(webscene_text_metrics) || font_size <= 0) {
            return 0;
        }
        NSString* value = [[NSString alloc] initWithBytes:text
                                                   length:text_length
                                                 encoding:NSUTF8StringEncoding];
        NSString* requested = [[NSString alloc] initWithBytes:font_family
                                                       length:font_family_length
                                                     encoding:NSUTF8StringEncoding];
        if (value == nil) return 0;

        NSString* family = [[requested ?: @"sans-serif"
            componentsSeparatedByString:@","] firstObject];
        family = [family stringByTrimmingCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        family = [family stringByTrimmingCharactersInSet:
            [NSCharacterSet characterSetWithCharactersInString:@"\"'"]];
        CTFontRef font;
        if ([family isEqualToString:@"sans-serif"]
            || [family isEqualToString:@"system-ui"]
            || [family isEqualToString:@"-apple-system"]
            || [family isEqualToString:@"BlinkMacSystemFont"]) {
            font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, font_size, nullptr);
        } else {
            font = CTFontCreateWithName(
                (__bridge CFStringRef)family,
                font_size,
                nullptr);
        }
        if (font == nullptr) return 0;

        CTFontSymbolicTraits desired =
            font_weight >= 600 ? kCTFontBoldTrait : static_cast<CTFontSymbolicTraits>(0);
        CTFontRef weighted = CTFontCreateCopyWithSymbolicTraits(
            font,
            font_size,
            nullptr,
            desired,
            desired);
        if (weighted != nullptr) {
            CFRelease(font);
            font = weighted;
        }
        NSDictionary* attributes = @{
          (__bridge NSString*)kCTFontAttributeName: (__bridge id)font,
          (__bridge NSString*)kCTKernAttributeName: @(letter_spacing),
        };
        NSAttributedString* attributed =
            [[NSAttributedString alloc] initWithString:value attributes:attributes];
        CTLineRef line = CTLineCreateWithAttributedString(
            (__bridge CFAttributedStringRef)attributed);
        double ascent = 0;
        double descent = 0;
        double leading = 0;
        double advance = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        if (word_spacing != 0) {
            NSUInteger spaces =
                value.length - [[value stringByReplacingOccurrencesOfString:@" "
                                                                  withString:@""] length];
            advance += spaces * word_spacing;
        }
        metrics->advance_width = static_cast<float>(std::max(0.0, advance));
        metrics->ascent = static_cast<float>(ascent);
        metrics->descent = static_cast<float>(descent);
        metrics->leading = static_cast<float>(leading);
        CFRelease(line);
        CFRelease(font);
        return 1;
    }
}

}  // namespace

extern "C" __attribute__((visibility("default")))
webscene_engine* webscene_flutter_engine_create(
    const char* runtime_path,
    const char* cache_directory)
{
    @autoreleasepool {
        last_error.clear();
        std::lock_guard lock(engines_mutex);
        if (!ensure_runtime(runtime_path)) return nullptr;
        if (runtime.prewarm() == 0) {
            last_error = "WebScene V8 prewarm failed.";
            return nullptr;
        }
        auto context = std::make_unique<ResourceContext>();
        const size_t cache_length =
            cache_directory == nullptr ? 0 : std::strlen(cache_directory);
        webscene_engine_options options{};
        options.struct_size = sizeof(options);
        options.simulated_chart_command_count = 0;
        options.compilation_cache_directory = cache_length == 0 ? nullptr : cache_directory;
        options.compilation_cache_directory_length = cache_length;
        options.resource_load_callback = resource_load;
        options.resource_load_user_data = context.get();
        options.text_measure_callback = measure_text;
        options.text_measure_user_data = context.get();
        auto* engine = runtime.create(&options);
        if (engine == nullptr) {
            last_error = "WebScene engine creation failed.";
            return nullptr;
        }
        engines.emplace(engine, std::move(context));
        return engine;
    }
}

extern "C" __attribute__((visibility("default")))
void webscene_flutter_engine_destroy(webscene_engine* engine)
{
    if (engine == nullptr) return;
    std::unique_ptr<ResourceContext> context;
    {
        std::lock_guard lock(engines_mutex);
        auto found = engines.find(engine);
        if (found == engines.end()) return;
        context = std::move(found->second);
        engines.erase(found);
    }
    runtime.destroy(engine);
}

extern "C" __attribute__((visibility("default")))
const char* webscene_flutter_last_error(void)
{
    return last_error.c_str();
}

extern "C" __attribute__((visibility("default")))
uint64_t webscene_flutter_resource_request_count(webscene_engine* engine)
{
    std::lock_guard lock(engines_mutex);
    auto found = engines.find(engine);
    return found == engines.end()
        ? 0
        : found->second->successful_resource_requests.load(std::memory_order_relaxed);
}
