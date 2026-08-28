    std::filesystem::path compilation_cache_path(const std::array<uint8_t, 32>& key) const
    {
        if (compilation_cache_directory.empty()) return {};
        return std::filesystem::path(compilation_cache_directory)
            / (to_hex(key) + ".v8cache");
    }

    std::filesystem::path resource_cache_path(const std::array<uint8_t, 32>& key) const
    {
        if (compilation_cache_directory.empty()) return {};
        return std::filesystem::path(compilation_cache_directory)
            / "resources"
            / (to_hex(key) + ".websceneresource");
    }

    static bool read_process_resource_cache(
        const std::filesystem::path& path,
        cached_text_resource& resource)
    {
        const auto memory_key = path.generic_string();
        std::lock_guard lock(process_resource_cache_mutex);
        const auto known = process_resource_cache.find(memory_key);
        if (known == process_resource_cache.end()) return false;
        known->second.access = ++process_resource_cache_access;
        resource = known->second.resource;
        return true;
    }

    static void remember_process_resource_cache(
        const std::filesystem::path& path,
        cached_text_resource resource,
        bool preserve_existing_content = false)
    {
        const auto memory_key = path.generic_string();
        if (memory_key.empty()) return;
        std::lock_guard lock(process_resource_cache_mutex);
        const auto known = process_resource_cache.find(memory_key);
        if (known != process_resource_cache.end()) {
            if (preserve_existing_content && known->second.resource.content != nullptr) {
                resource.content = known->second.resource.content;
            }
        }
        const auto bytes =
            (resource.content == nullptr ? 0U : resource.content->size())
            + resource.entity_tag.size();
        const auto mapped_bytes = resource.content != nullptr
                && resource.content->mapped()
            ? resource.content->size()
            : 0U;
        if (bytes > maximum_process_resource_cache_bytes) return;
        if (known != process_resource_cache.end()) {
            process_resource_cache_bytes -= known->second.bytes;
            process_resource_mapped_cache_bytes -= known->second.mapped_bytes;
            process_resource_cache.erase(known);
        }
        process_resource_cache_bytes += bytes;
        process_resource_mapped_cache_bytes += mapped_bytes;
        process_resource_cache.emplace(
            memory_key,
            process_resource_cache_entry{
                std::move(resource),
                bytes,
                mapped_bytes,
                ++process_resource_cache_access});
        while (process_resource_cache.size() > maximum_process_resource_cache_entries
            || process_resource_cache_bytes > maximum_process_resource_cache_bytes) {
            const auto oldest = std::min_element(
                process_resource_cache.begin(),
                process_resource_cache.end(),
                [](const auto& left, const auto& right) {
                    return left.second.access < right.second.access;
                });
            if (oldest == process_resource_cache.end()) break;
            process_resource_cache_bytes -= oldest->second.bytes;
            process_resource_mapped_cache_bytes -= oldest->second.mapped_bytes;
            process_resource_cache.erase(oldest);
        }
    }

    static void forget_process_resource_cache(const std::filesystem::path& path)
    {
        const auto memory_key = path.generic_string();
        std::lock_guard lock(process_resource_cache_mutex);
        const auto known = process_resource_cache.find(memory_key);
        if (known == process_resource_cache.end()) return;
        process_resource_cache_bytes -= known->second.bytes;
        process_resource_mapped_cache_bytes -= known->second.mapped_bytes;
        process_resource_cache.erase(known);
    }

    static bool replace_cache_file(
        const std::filesystem::path& temporary,
        const std::filesystem::path& destination)
    {
#if defined(_WIN32)
        const auto native_temporary = runtime_file_path(temporary);
        const auto native_destination = runtime_file_path(destination);
        return MoveFileExW(
            native_temporary.c_str(),
            native_destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code error;
        std::filesystem::rename(temporary, destination, error);
        return !error;
#endif
    }

    bool read_resource_cache(
        const std::array<uint8_t, 32>& key,
        uint32_t kind,
        cached_text_resource& resource)
    {
        const auto path = resource_cache_path(key);
        if (path.empty()) return false;
        if (read_process_resource_cache(path, resource)) {
            ++resource_cache_memory_hit_count;
            return true;
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;

        constexpr std::array<char, 8> magic{'H', 'M', 'L', 'R', 'E', 'S', '0', '1'};
        std::array<char, 8> stored_magic{};
        uint32_t schema = 0U;
        uint32_t stored_kind = 0U;
        std::array<uint8_t, 32> stored_key{};
        std::array<uint8_t, 32> stored_content_hash{};
        uint32_t entity_tag_length = 0U;
        int64_t last_modified_unix_seconds = 0;
        int64_t fresh_until_unix_seconds = 0;
        uint64_t content_length = 0U;
        stream.read(stored_magic.data(), static_cast<std::streamsize>(stored_magic.size()));
        stream.read(reinterpret_cast<char*>(&schema), sizeof(schema));
        stream.read(reinterpret_cast<char*>(&stored_kind), sizeof(stored_kind));
        stream.read(reinterpret_cast<char*>(stored_key.data()), static_cast<std::streamsize>(stored_key.size()));
        stream.read(reinterpret_cast<char*>(stored_content_hash.data()), static_cast<std::streamsize>(stored_content_hash.size()));
        stream.read(reinterpret_cast<char*>(&entity_tag_length), sizeof(entity_tag_length));
        stream.read(reinterpret_cast<char*>(&last_modified_unix_seconds), sizeof(last_modified_unix_seconds));
        stream.read(reinterpret_cast<char*>(&fresh_until_unix_seconds), sizeof(fresh_until_unix_seconds));
        stream.read(reinterpret_cast<char*>(&content_length), sizeof(content_length));
        if (!stream || stored_magic != magic || schema != 3U || stored_kind != kind
            || stored_key != key || entity_tag_length > maximum_resource_entity_tag_bytes
            || content_length > maximum_resource_cache_entry_bytes) {
            ++resource_cache_rejection_count;
            std::error_code error;
            std::filesystem::remove(path, error);
            return false;
        }
        resource.entity_tag.resize(entity_tag_length);
        if (!resource.entity_tag.empty()) {
            stream.read(resource.entity_tag.data(), static_cast<std::streamsize>(resource.entity_tag.size()));
        }
        const auto content_offset_position = stream.tellg();
        std::error_code size_error;
        const auto file_size = std::filesystem::file_size(path, size_error);
        const auto content_offset = content_offset_position < 0
            ? 0U
            : static_cast<uint64_t>(content_offset_position);
        if (!stream || size_error
            || content_offset > std::numeric_limits<uint64_t>::max() - content_length
            || file_size != content_offset + content_length) {
            resource = {};
            ++resource_cache_rejection_count;
            std::error_code error;
            std::filesystem::remove(path, error);
            return false;
        }

        std::shared_ptr<const immutable_byte_buffer> bytes;
        if (content_length >= minimum_mapped_cache_body_bytes) {
            bytes = immutable_byte_buffer::map_file(
                path,
                static_cast<size_t>(content_offset),
                static_cast<size_t>(content_length));
        }
        if (bytes == nullptr && content_length > 0U) {
            std::vector<uint8_t> owned(static_cast<size_t>(content_length));
            stream.seekg(static_cast<std::streamoff>(content_offset), std::ios::beg);
            stream.read(
                reinterpret_cast<char*>(owned.data()),
                static_cast<std::streamsize>(owned.size()));
            if (stream) bytes = immutable_byte_buffer::take(std::move(owned));
        }
        if ((content_length > 0U && bytes == nullptr)
            || (bytes != nullptr
                && sha256(bytes->data(), bytes->size()) != stored_content_hash)
            || (content_length == 0U
                && sha256(nullptr, 0U) != stored_content_hash)) {
            resource = {};
            ++resource_cache_rejection_count;
            std::error_code error;
            std::filesystem::remove(path, error);
            return false;
        }
        resource.content = bytes != nullptr
            ? immutable_text_source::from_bytes(std::move(bytes))
            : immutable_text_source::from_string(
                std::make_shared<const std::string>());
        resource.last_modified_unix_seconds = last_modified_unix_seconds;
        resource.fresh_until_unix_seconds = fresh_until_unix_seconds;
        std::error_code access_error;
        std::filesystem::last_write_time(
            path,
            std::filesystem::file_time_type::clock::now(),
            access_error);
        remember_process_resource_cache(path, resource);
        return true;
    }

    void write_resource_cache(
        const std::array<uint8_t, 32>& key,
        uint32_t kind,
        std::shared_ptr<const immutable_text_source> content,
        const std::string& entity_tag,
        int64_t last_modified_unix_seconds,
        int64_t fresh_until_unix_seconds)
    {
        const auto path = resource_cache_path(key);
        if (path.empty() || content == nullptr
            || content->size() > maximum_resource_cache_entry_bytes) {
            return;
        }
        const auto entity_tag_length = static_cast<uint32_t>(std::min(
            entity_tag.size(),
            static_cast<size_t>(maximum_resource_entity_tag_bytes)));
        const auto remember_unpersisted_content = [&] {
            remember_process_resource_cache(
                path,
                cached_text_resource{
                    content,
                    entity_tag.substr(0U, entity_tag_length),
                    last_modified_unix_seconds,
                    fresh_until_unix_seconds},
                true);
        };
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            remember_unpersisted_content();
            return;
        }

        static std::atomic<uint64_t> temporary_sequence{0};
        const auto temporary = path.string()
            + "." + std::to_string(current_process_id())
            + "." + std::to_string(temporary_sequence.fetch_add(1, std::memory_order_relaxed))
            + ".tmp";
        std::ofstream stream(
            runtime_file_path(temporary),
            std::ios::binary | std::ios::trunc);
        if (!stream) {
            remember_unpersisted_content();
            return;
        }
        constexpr std::array<char, 8> magic{'H', 'M', 'L', 'R', 'E', 'S', '0', '1'};
        constexpr uint32_t schema = 3U;
        const auto content_hash = sha256(
            reinterpret_cast<const uint8_t*>(content->data()),
            content->size());
        const auto content_length = static_cast<uint64_t>(content->size());
        stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        stream.write(reinterpret_cast<const char*>(&schema), sizeof(schema));
        stream.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
        stream.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
        stream.write(reinterpret_cast<const char*>(content_hash.data()), static_cast<std::streamsize>(content_hash.size()));
        stream.write(reinterpret_cast<const char*>(&entity_tag_length), sizeof(entity_tag_length));
        stream.write(
            reinterpret_cast<const char*>(&last_modified_unix_seconds),
            sizeof(last_modified_unix_seconds));
        stream.write(
            reinterpret_cast<const char*>(&fresh_until_unix_seconds),
            sizeof(fresh_until_unix_seconds));
        stream.write(reinterpret_cast<const char*>(&content_length), sizeof(content_length));
        if (entity_tag_length > 0U) {
            stream.write(entity_tag.data(), static_cast<std::streamsize>(entity_tag_length));
        }
        const auto content_offset = static_cast<size_t>(stream.tellp());
        if (!content->empty()) {
            stream.write(
                content->data(),
                static_cast<std::streamsize>(content->size()));
        }
        stream.close();
        if (!stream) {
            std::filesystem::remove(temporary, error);
            remember_unpersisted_content();
            return;
        }
        if (!replace_cache_file(temporary, path)) {
            std::filesystem::remove(temporary, error);
            remember_unpersisted_content();
            return;
        }
        auto retained_content = content;
        if (content_length >= minimum_mapped_cache_body_bytes) {
            if (auto mapped = immutable_byte_buffer::map_file(
                    path,
                    content_offset,
                    static_cast<size_t>(content_length));
                mapped != nullptr) {
                retained_content =
                    immutable_text_source::from_bytes(std::move(mapped));
            }
        }
        remember_process_resource_cache(
            path,
            cached_text_resource{
                std::move(retained_content),
                entity_tag.substr(0U, entity_tag_length),
                last_modified_unix_seconds,
                fresh_until_unix_seconds});
        resource_cache_bytes_written_count += content_length;
        if (++resource_writes_since_prune >= 64U) {
            resource_writes_since_prune = 0U;
            prune_resource_cache();
        }
    }

    void remove_resource_cache(const std::array<uint8_t, 32>& key)
    {
        const auto path = resource_cache_path(key);
        if (path.empty()) return;
        forget_process_resource_cache(path);
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    void prune_resource_cache()
    {
        const auto directory = std::filesystem::path(compilation_cache_directory) / "resources";
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) return;
        struct cache_file final {
            std::filesystem::path path;
            std::filesystem::file_time_type last_write;
            uint64_t size;
        };
        std::vector<cache_file> files;
        uint64_t total_bytes = 0U;
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const auto path = iterator->path();
            iterator.increment(error);
            if (path.extension() != ".websceneresource") continue;
            std::error_code metadata_error;
            const auto size = std::filesystem::file_size(path, metadata_error);
            if (metadata_error) continue;
            const auto last_write = std::filesystem::last_write_time(path, metadata_error);
            if (metadata_error) continue;
            files.push_back({path, last_write, size});
            total_bytes += size;
        }
        std::sort(files.begin(), files.end(), [](const cache_file& left, const cache_file& right) {
            return left.last_write < right.last_write;
        });
        size_t first_retained = 0U;
        while (first_retained < files.size()
            && (files.size() - first_retained > maximum_resource_cache_entries
                || total_bytes > maximum_resource_cache_bytes)) {
            std::error_code remove_error;
            if (std::filesystem::remove(files[first_retained].path, remove_error)) {
                total_bytes -= files[first_retained].size;
            }
            ++first_retained;
        }
    }

    void prune_persistent_compilation_cache()
    {
        if (compilation_cache_directory.empty()) return;
        const auto directory = std::filesystem::path(compilation_cache_directory);
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) return;

        struct cache_file final {
            std::filesystem::path path;
            std::filesystem::file_time_type last_write;
            uint64_t size;
        };
        std::vector<cache_file> files;
        uint64_t total_bytes = 0;
        std::filesystem::directory_iterator iterator(directory, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end) {
            const auto path = iterator->path();
            iterator.increment(error);
            if (path.extension() != ".v8cache") continue;
            std::error_code metadata_error;
            const auto size = std::filesystem::file_size(path, metadata_error);
            if (metadata_error) continue;
            const auto last_write = pending_compilation_cache_touches.contains(
                    path.string())
                ? std::filesystem::file_time_type::clock::now()
                : std::filesystem::last_write_time(path, metadata_error);
            if (metadata_error) continue;
            files.push_back(cache_file{path, last_write, size});
            total_bytes += size;
        }
        std::sort(files.begin(), files.end(), [](const cache_file& left, const cache_file& right) {
            return left.last_write < right.last_write;
        });
        size_t first_retained = 0;
        while (first_retained < files.size()
            && (files.size() - first_retained > maximum_persistent_cache_entries
                || total_bytes > maximum_persistent_cache_bytes)) {
            std::error_code remove_error;
            if (std::filesystem::remove(files[first_retained].path, remove_error)) {
                total_bytes -= files[first_retained].size;
            }
            ++first_retained;
        }
    }

    void flush_compilation_cache_touches()
    {
        if (pending_compilation_cache_touches.empty()) return;
        const auto now = std::filesystem::file_time_type::clock::now();
        for (const auto& value : pending_compilation_cache_touches) {
            std::error_code error;
            std::filesystem::last_write_time(
                runtime_file_path(value),
                now,
                error);
        }
        pending_compilation_cache_touches.clear();
    }

    std::shared_ptr<const immutable_byte_buffer> read_compilation_cache(
        const std::array<uint8_t, 32>& key,
        bool record_access = true)
    {
        const auto path = compilation_cache_path(key);
        if (path.empty()) return {};
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            if (record_access) ++compilation_persistent_miss_count;
            return {};
        }

        constexpr std::array<char, 8> magic{'H', 'M', 'L', 'N', 'V', '8', '0', '1'};
        std::array<char, 8> stored_magic{};
        uint32_t schema = 0;
        uint32_t version_tag = 0;
        std::array<uint8_t, 32> stored_key{};
        std::array<uint8_t, 32> stored_payload_hash{};
        uint64_t payload_length = 0;
        stream.read(stored_magic.data(), static_cast<std::streamsize>(stored_magic.size()));
        stream.read(reinterpret_cast<char*>(&schema), sizeof(schema));
        stream.read(reinterpret_cast<char*>(&version_tag), sizeof(version_tag));
        stream.read(reinterpret_cast<char*>(stored_key.data()), static_cast<std::streamsize>(stored_key.size()));
        stream.read(reinterpret_cast<char*>(stored_payload_hash.data()), static_cast<std::streamsize>(stored_payload_hash.size()));
        stream.read(reinterpret_cast<char*>(&payload_length), sizeof(payload_length));
        if (!stream || stored_magic != magic || schema != 1U
            || version_tag != v8::ScriptCompiler::CachedDataVersionTag()
            || stored_key != key || payload_length == 0U
            || payload_length > maximum_persistent_cache_entry_bytes) {
            ++compilation_cache_rejection_count;
            std::error_code error;
            std::filesystem::remove(path, error);
            return {};
        }
        constexpr size_t payload_offset =
            magic.size()
            + sizeof(schema)
            + sizeof(version_tag)
            + stored_key.size()
            + stored_payload_hash.size()
            + sizeof(payload_length);
        std::error_code size_error;
        const auto file_size = std::filesystem::file_size(path, size_error);
        if (size_error
            || file_size != payload_offset + payload_length) {
            ++compilation_cache_rejection_count;
            std::error_code error;
            std::filesystem::remove(path, error);
            return {};
        }
        std::shared_ptr<const immutable_byte_buffer> payload;
        if (payload_length >= minimum_mapped_cache_body_bytes) {
            stream.close();
            payload = immutable_byte_buffer::map_file(
                path,
                payload_offset,
                static_cast<size_t>(payload_length));
        } else {
            std::vector<uint8_t> owned(static_cast<size_t>(payload_length));
            stream.read(
                reinterpret_cast<char*>(owned.data()),
                static_cast<std::streamsize>(owned.size()));
            if (!stream || stream.peek() != std::ifstream::traits_type::eof()) {
                ++compilation_cache_rejection_count;
                std::error_code error;
                std::filesystem::remove(path, error);
                return {};
            }
            payload = immutable_byte_buffer::take(std::move(owned));
        }
        if (payload == nullptr
            || sha256(payload->data(), payload->size()) != stored_payload_hash) {
            ++compilation_cache_rejection_count;
            std::error_code error;
            std::filesystem::remove(path, error);
            return {};
        }
        if (record_access) {
            compilation_cache_bytes_read_count += payload->size();
            if (defer_compilation_cache_touches) {
                pending_compilation_cache_touches.insert(path.string());
            } else {
                std::error_code access_error;
                std::filesystem::last_write_time(
                    path,
                    std::filesystem::file_time_type::clock::now(),
                    access_error);
            }
        }
        return payload;
    }

    void write_compilation_cache(
        const std::array<uint8_t, 32>& key,
        const v8::ScriptCompiler::CachedData& data)
    {
        const auto path = compilation_cache_path(key);
        if (path.empty() || data.data == nullptr || data.length <= 0
            || static_cast<size_t>(data.length) > maximum_persistent_cache_entry_bytes) return;
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return;

        static std::atomic<uint64_t> temporary_sequence{0};
        const auto temporary = path.string()
            + "." + std::to_string(current_process_id())
            + "." + std::to_string(temporary_sequence.fetch_add(1, std::memory_order_relaxed))
            + ".tmp";
        std::ofstream stream(
            runtime_file_path(temporary),
            std::ios::binary | std::ios::trunc);
        if (!stream) return;
        constexpr std::array<char, 8> magic{'H', 'M', 'L', 'N', 'V', '8', '0', '1'};
        constexpr uint32_t schema = 1U;
        const auto version_tag = v8::ScriptCompiler::CachedDataVersionTag();
        const auto payload_hash = sha256(data.data, static_cast<size_t>(data.length));
        const auto payload_length = static_cast<uint64_t>(data.length);
        stream.write(magic.data(), static_cast<std::streamsize>(magic.size()));
        stream.write(reinterpret_cast<const char*>(&schema), sizeof(schema));
        stream.write(reinterpret_cast<const char*>(&version_tag), sizeof(version_tag));
        stream.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
        stream.write(reinterpret_cast<const char*>(payload_hash.data()), static_cast<std::streamsize>(payload_hash.size()));
        stream.write(reinterpret_cast<const char*>(&payload_length), sizeof(payload_length));
        stream.write(reinterpret_cast<const char*>(data.data), data.length);
        stream.close();
        if (!stream) {
            std::filesystem::remove(temporary, error);
            return;
        }
        if (!replace_cache_file(temporary, path)) {
            std::filesystem::remove(temporary, error);
            return;
        }
        compilation_cache_bytes_written_count += static_cast<uint64_t>(data.length);
        if (++persistent_writes_since_prune >= 64U) {
            persistent_writes_since_prune = 0;
            prune_persistent_compilation_cache();
        }
    }

    static process_compilation_acquisition acquire_process_compilation(
        const std::string& key)
    {
        std::lock_guard lock(process_compilation_cache_mutex);
        if (const auto known = process_compilation_cache.find(key);
            known != process_compilation_cache.end()) {
            known->second->access = ++process_compilation_cache_access;
            return {known->second, false};
        }

        auto entry = std::make_shared<process_compilation_entry>();
        entry->access = ++process_compilation_cache_access;
        process_compilation_cache.emplace(key, entry);
        return {std::move(entry), true};
    }

    static void publish_process_compilation(
        const std::string& key,
        const std::shared_ptr<process_compilation_entry>& entry,
        std::shared_ptr<const immutable_byte_buffer> cached_data)
    {
        {
            std::lock_guard lock(entry->mutex);
            entry->cached_data = std::move(cached_data);
            entry->bytes = entry->cached_data == nullptr
                ? 0U
                : entry->cached_data->size();
            entry->state = process_compilation_state::ready;
        }
        entry->condition.notify_all();

        std::lock_guard lock(process_compilation_cache_mutex);
        const auto known = process_compilation_cache.find(key);
        if (known == process_compilation_cache.end()
            || known->second.get() != entry.get()) {
            return;
        }
        process_compilation_cache_bytes += entry->bytes;
        if (entry->cached_data != nullptr && entry->cached_data->mapped()) {
            process_compilation_mapped_cache_bytes += entry->bytes;
        }
        while (process_compilation_cache.size() > maximum_process_compilation_cache_entries
            || process_compilation_cache_bytes > maximum_process_compilation_cache_bytes) {
            auto oldest = process_compilation_cache.end();
            for (auto candidate = process_compilation_cache.begin();
                candidate != process_compilation_cache.end();
                ++candidate) {
                if (candidate->second->bytes == 0U
                    || candidate->second.get() == entry.get()) {
                    continue;
                }
                if (oldest == process_compilation_cache.end()
                    || candidate->second->access < oldest->second->access) {
                    oldest = candidate;
                }
            }
            if (oldest == process_compilation_cache.end()) break;
            process_compilation_cache_bytes -= oldest->second->bytes;
            if (oldest->second->cached_data != nullptr
                && oldest->second->cached_data->mapped()) {
                process_compilation_mapped_cache_bytes -= oldest->second->bytes;
            }
            process_compilation_cache.erase(oldest);
        }
    }

    static void fail_process_compilation(
        const std::string& key,
        const std::shared_ptr<process_compilation_entry>& entry)
    {
        {
            std::lock_guard lock(entry->mutex);
            entry->state = process_compilation_state::failed;
        }
        entry->condition.notify_all();

        std::lock_guard lock(process_compilation_cache_mutex);
        const auto known = process_compilation_cache.find(key);
        if (known != process_compilation_cache.end()
            && known->second.get() == entry.get()) {
            process_compilation_cache.erase(known);
        }
    }

    std::shared_ptr<const immutable_byte_buffer> await_process_compilation(
        const std::shared_ptr<process_compilation_entry>& entry)
    {
        std::unique_lock lock(entry->mutex);
        if (entry->state == process_compilation_state::producing) {
            ++process_compilation_waiter_count;
            entry->condition.wait(lock, [&entry] {
                return entry->state != process_compilation_state::producing;
            });
        }
        if (entry->state != process_compilation_state::ready
            || entry->cached_data == nullptr
            || entry->cached_data->empty()) {
            return {};
        }
        ++process_compilation_memory_hit_count;
        process_compilation_shared_byte_count += entry->cached_data->size();
        return entry->cached_data;
    }

    void retain_compiled_script(
        const std::string& key,
        const std::array<uint8_t, 32>& key_digest,
        v8::Local<v8::UnboundScript> script,
        size_t source_bytes)
    {
        compilation_cache_entry entry;
        entry.script.Reset(isolate, script);
        entry.key_digest = key_digest;
        entry.source_bytes = source_bytes;
        compilation_cache_bytes += source_bytes;
        compilation_cache.emplace(key, std::move(entry));
        compilation_cache_order.push_back(key);
        while (compilation_cache.size() > maximum_compilation_cache_entries
            || compilation_cache_bytes > maximum_compilation_cache_source_bytes) {
            const auto oldest = std::move(compilation_cache_order.front());
            compilation_cache_order.pop_front();
            const auto iterator = compilation_cache.find(oldest);
            if (iterator == compilation_cache.end()) continue;
            compilation_cache_bytes -= iterator->second.source_bytes;
            iterator->second.script.Reset();
            compilation_cache.erase(iterator);
        }
    }

    void retain_shared_compiled_script(
        const std::string& key,
        const std::array<uint8_t, 32>& key_digest,
        v8::Local<v8::UnboundScript> script,
        size_t source_bytes)
    {
        if (shared_isolate == nullptr
            || shared_isolate->compilation_cache.contains(key)) {
            return;
        }
        compilation_cache_entry entry;
        entry.script.Reset(isolate, script);
        entry.key_digest = key_digest;
        entry.source_bytes = source_bytes;
        shared_isolate->compilation_cache_bytes += source_bytes;
        shared_isolate->compilation_cache.emplace(key, std::move(entry));
        shared_isolate->compilation_cache_order.push_back(key);
        while (
            shared_isolate->compilation_cache.size()
                    > maximum_compilation_cache_entries
            || shared_isolate->compilation_cache_bytes
                    > maximum_compilation_cache_source_bytes) {
            const auto oldest =
                std::move(shared_isolate->compilation_cache_order.front());
            shared_isolate->compilation_cache_order.pop_front();
            const auto iterator =
                shared_isolate->compilation_cache.find(oldest);
            if (iterator == shared_isolate->compilation_cache.end()) continue;
            shared_isolate->compilation_cache_bytes -=
                iterator->second.source_bytes;
            iterator->second.script.Reset();
            shared_isolate->compilation_cache.erase(iterator);
        }
    }

    static bool is_ascii_script_source(std::string_view source) noexcept
    {
        return std::all_of(source.begin(), source.end(), [](char value) {
            return static_cast<unsigned char>(value) < 0x80U;
        });
    }

    std::shared_ptr<const immutable_text_source> acquire_process_script_source(
        const std::string& key,
        std::string_view source,
        const std::shared_ptr<const immutable_text_source>& preferred_source = {})
    {
        std::lock_guard lock(process_script_source_mutex);
        if (const auto known = process_script_sources.find(key);
            known != process_script_sources.end()) {
            if (auto shared = known->second.source.lock();
                shared != nullptr
                    && shared->size() == source.size()) {
                // `key` is the SHA-256 compilation digest of document name
                // and source bytes, so a second full multi-megabyte comparison
                // would only repeat work already performed by the digest.
                known->second.access = ++process_script_source_access;
                ++process_script_source_memory_hit_count;
                process_script_source_shared_byte_count += shared->size();
                return shared;
            }
            process_script_sources.erase(known);
        }

        auto shared = preferred_source != nullptr
                && preferred_source->size() == source.size()
            ? preferred_source
            : immutable_text_source::from_string(
                std::make_shared<const std::string>(source));
        process_script_sources.emplace(
            key,
            process_script_source_entry{
                shared,
                ++process_script_source_access});

        if (process_script_sources.size() > maximum_process_script_source_entries) {
            std::erase_if(process_script_sources, [](const auto& candidate) {
                return candidate.second.source.expired();
            });
            while (process_script_sources.size()
                > maximum_process_script_source_entries) {
                const auto oldest = std::min_element(
                    process_script_sources.begin(),
                    process_script_sources.end(),
                    [](const auto& left, const auto& right) {
                        return left.second.access < right.second.access;
                    });
                if (oldest == process_script_sources.end()) break;
                // The map holds weak references only. Removing metadata never
                // invalidates a source still owned by an isolate.
                process_script_sources.erase(oldest);
            }
        }
        return shared;
    }

    std::shared_ptr<const std::vector<uint16_t>>
    acquire_process_two_byte_script_source(
        const std::string& key,
        std::string_view source)
    {
        std::lock_guard lock(process_script_source_mutex);
        if (const auto known = process_two_byte_script_sources.find(key);
            known != process_two_byte_script_sources.end()) {
            if (auto shared = known->second.source.lock();
                shared != nullptr) {
                known->second.access = ++process_script_source_access;
                ++process_script_source_memory_hit_count;
                process_script_source_shared_byte_count +=
                    shared->size() * sizeof(uint16_t);
                return shared;
            }
            process_two_byte_script_sources.erase(known);
        }

        auto shared = std::make_shared<const std::vector<uint16_t>>(
            decode_wtf8_to_utf16(source));
        process_two_byte_script_sources.emplace(
            key,
            process_two_byte_script_source_entry{
                shared,
                ++process_script_source_access});

        if (process_two_byte_script_sources.size()
            > maximum_process_script_source_entries) {
            std::erase_if(
                process_two_byte_script_sources,
                [](const auto& candidate) {
                    return candidate.second.source.expired();
                });
            while (process_two_byte_script_sources.size()
                > maximum_process_script_source_entries) {
                const auto oldest = std::min_element(
                    process_two_byte_script_sources.begin(),
                    process_two_byte_script_sources.end(),
                    [](const auto& left, const auto& right) {
                        return left.second.access < right.second.access;
                    });
                if (oldest == process_two_byte_script_sources.end()) break;
                process_two_byte_script_sources.erase(oldest);
            }
        }
        return shared;
    }

    v8::Local<v8::String> create_script_source_value(
        const std::string& key,
        std::string_view source,
        std::shared_ptr<const immutable_text_source> preferred_source = {})
    {
        // Keep this owner alive through the fallback below. The process-sharing
        // helper intentionally declines small or non-ASCII sources; moving the
        // sole mapped/aliased owner into that call would release its backing
        // storage and leave `source` dangling before NewFromUtf8 copies it.
        if (source.size() >= minimum_shared_script_source_bytes) {
            if (is_ascii_script_source(source)) {
                if (auto shared = acquire_process_script_source(
                        key,
                        source,
                        preferred_source);
                    shared != nullptr) {
                    auto resource =
                        std::make_unique<shared_external_one_byte_source>(
                            std::move(shared),
                            external_script_source_byte_count);
                    v8::Local<v8::String> external;
                    if (v8::String::NewExternalOneByte(
                            isolate,
                            resource.get()).ToLocal(&external)) {
                        // V8 owns and disposes the resource after this point.
                        static_cast<void>(resource.release());
                        return external;
                    }
                }
            } else if (
                auto shared =
                    acquire_process_two_byte_script_source(key, source);
                shared != nullptr) {
                auto resource =
                    std::make_unique<shared_external_two_byte_source>(
                        std::move(shared),
                        external_script_source_byte_count);
                v8::Local<v8::String> external;
                if (v8::String::NewExternalTwoByte(isolate, resource.get())
                        .ToLocal(&external)) {
                    static_cast<void>(resource.release());
                    return external;
                }
            }
        }
        return v8::String::NewFromUtf8(
            isolate,
            source.data(),
            v8::NewStringType::kNormal,
            static_cast<int>(source.size())).ToLocalChecked();
    }

    bool compile_script(
        std::string_view source,
        const std::string& document_name,
        v8::Local<v8::Script>& script,
        std::shared_ptr<const immutable_text_source> immutable_source = {})
    {
        ++compilation_request_count;
        const auto key_digest = compilation_key_digest(document_name, source);
        const auto key = to_hex(key_digest);
        const auto cached = compilation_cache.find(key);
        if (cached != compilation_cache.end()) {
            ++compilation_memory_hit_count;
            script = cached->second.script.Get(isolate)->BindToCurrentContext();
            return true;
        }
        if (shared_isolate != nullptr) {
            const auto shared_cached =
                shared_isolate->compilation_cache.find(key);
            if (shared_cached != shared_isolate->compilation_cache.end()) {
                ++compilation_memory_hit_count;
                const auto unbound = shared_cached->second.script.Get(isolate);
                retain_compiled_script(
                    key,
                    key_digest,
                    unbound,
                    shared_cached->second.source_bytes);
                script = unbound->BindToCurrentContext();
                return true;
            }
        }

        const auto process_acquisition = acquire_process_compilation(key);
        std::shared_ptr<const immutable_byte_buffer> shared_bytes;
        auto has_persistent = false;
        if (process_acquisition.producer) {
            ++process_compilation_leader_count;
            shared_bytes = read_compilation_cache(key_digest);
            has_persistent = shared_bytes != nullptr;
        } else {
            shared_bytes = await_process_compilation(process_acquisition.entry);
            if (shared_bytes == nullptr) {
                return false;
            }
        }

        auto source_value = create_script_source_value(
            key,
            source,
            std::move(immutable_source));
        auto name_value = v8::String::NewFromUtf8(
            isolate,
            document_name.data(),
            v8::NewStringType::kNormal,
            static_cast<int>(document_name.size())).ToLocalChecked();
        v8::ScriptOrigin origin(name_value);
        v8::ScriptCompiler::CachedData* cached_data = nullptr;
        if (shared_bytes != nullptr && !shared_bytes->empty()) {
            cached_data = new v8::ScriptCompiler::CachedData(
                shared_bytes->data(),
                static_cast<int>(shared_bytes->size()),
                v8::ScriptCompiler::CachedData::BufferNotOwned);
        }
        v8::ScriptCompiler::Source compiler_source(source_value, origin, cached_data);
        v8::Local<v8::UnboundScript> unbound;
        const auto started = std::chrono::steady_clock::now();
        const auto options = cached_data != nullptr
            ? v8::ScriptCompiler::kConsumeCodeCache
            : v8::ScriptCompiler::kNoCompileOptions;
        const auto compiled = v8::ScriptCompiler::CompileUnboundScript(
            isolate,
            &compiler_source,
            options).ToLocal(&unbound);
        compilation_time_nanosecond_count += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started).count());
        if (!compiled) {
            if (process_acquisition.producer) {
                fail_process_compilation(key, process_acquisition.entry);
            }
            return false;
        }

        std::shared_ptr<const immutable_byte_buffer> published_bytes = shared_bytes;
        if (has_persistent && cached_data != nullptr && !cached_data->rejected) {
            ++compilation_persistent_hit_count;
        } else if (cached_data == nullptr || cached_data->rejected) {
            if (has_persistent) {
                ++compilation_cache_rejection_count;
                std::error_code error;
                std::filesystem::remove(compilation_cache_path(key_digest), error);
            }
            std::unique_ptr<v8::ScriptCompiler::CachedData> generated(
                v8::ScriptCompiler::CreateCodeCache(unbound));
            if (generated != nullptr
                && generated->data != nullptr
                && generated->length > 0) {
                published_bytes = immutable_byte_buffer::copy(
                    generated->data,
                    static_cast<size_t>(generated->length));
                if (!compilation_cache_directory.empty()) {
                    write_compilation_cache(key_digest, *generated);
                    if (auto mapped = read_compilation_cache(
                            key_digest,
                            false);
                        mapped != nullptr) {
                        published_bytes = std::move(mapped);
                    }
                }
            }
        }
        if (process_acquisition.producer) {
            if (published_bytes == nullptr || published_bytes->empty()) {
                fail_process_compilation(key, process_acquisition.entry);
            } else {
                publish_process_compilation(
                    key,
                    process_acquisition.entry,
                    std::move(published_bytes));
            }
        }
        retain_shared_compiled_script(
            key,
            key_digest,
            unbound,
            source.size());
        retain_compiled_script(key, key_digest, unbound, source.size());
        script = unbound->BindToCurrentContext();
        return true;
    }

    bool execute_in_context(
        v8::Local<v8::Context> local_context,
        std::string_view source,
        const std::string& document_name,
        std::string& error,
        bool checkpoint_microtasks = true,
        std::shared_ptr<const immutable_text_source> immutable_source = {})
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        binding_callback_timer timer(profile_startup ? &startup_script_execute : nullptr);
        const auto profile_started = profile_startup
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const auto script_phase_before = profile_startup
            ? capture_startup_phase_snapshot()
            : startup_phase_snapshot{};
#endif
        v8::Context::Scope context_scope(local_context);
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::Script> script;
        auto compiled = false;
        {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            binding_callback_timer compile_timer(
                profile_startup ? &startup_script_compile : nullptr);
#endif
            compiled = compile_script(
                source,
                document_name,
                script,
                std::move(immutable_source));
        }
        if (!compiled) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, local_context);
#endif
            error = describe_exception(try_catch, local_context);
            return false;
        }
        style_recascade_batch_scope style_batch(*this);
        auto ran = false;
        {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            binding_callback_timer run_timer(
                profile_startup ? &startup_script_run : nullptr);
#endif
            ran = !script->Run(local_context).IsEmpty();
        }
        if (!ran) {
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
            report_inspector_exception(try_catch, local_context);
#endif
            error = describe_exception(try_catch, local_context);
            return false;
        }
        if (checkpoint_microtasks) perform_microtask_checkpoint();
        style_batch.finish();
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup) {
            const auto script_name = resource_leaf_name(document_name);
            const auto elapsed_nanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - profile_started).count());
            auto& stats = startup_script_profiles[script_name];
            ++stats.calls;
            stats.nanoseconds += elapsed_nanoseconds;
            record_startup_nested_phases(
                startup_script_phase_profiles,
                script_name,
                script_phase_before,
                elapsed_nanoseconds);
        }
#endif
        return true;
    }

    void report_document_start_error(const std::string& error)
    {
        std::lock_guard lock(console_message_mutex);
        constexpr size_t maximum_console_messages = 1024U;
        if (console_messages.size() >= maximum_console_messages) {
            console_messages.pop_front();
        }
        console_messages.push_back("error\n" + error);
    }

    bool execute_document_start_scripts(
        v8::Local<v8::Context> local_context,
        bool frame)
    {
        for (const auto& script : current_document_start_scripts) {
            if (frame && !script.all_frames) continue;
            std::string error;
            if (execute_in_context(
                    local_context,
                    script.source,
                    script.name,
                    error,
                    true)) {
                continue;
            }
            const auto diagnostic = std::string(frame
                    ? "Frame document-start script '"
                    : "Document-start script '")
                + script.name + "' failed: " + error;
            frame_last_error_value = diagnostic;
            last_error = diagnostic;
            ++frame_script_error_count;
            report_document_start_error(diagnostic);
            return false;
        }
        return true;
    }

    static void current_script_get_attribute(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
    }

    void install_frame_globals(
        v8::Local<v8::Context> local_context,
        dom_node& frame,
        dom_node& frame_body)
    {
        v8::Local<v8::Object> frame_element;
        {
            v8::Context::Scope owner_context_scope(context.Get(isolate));
            frame_element = wrap_node(frame);
        }
        v8::Context::Scope context_scope(local_context);
        auto global = local_context->Global();
        auto frame_document_value = document_template.Get(isolate)->NewInstance(local_context).ToLocalChecked();
        frame_document_value->SetAlignedPointerInInternalField(
            0,
            &frame_body,
            v8::kEmbedderDataTypeTagDefault);
        frame_document_value->Set(
            local_context,
            js_string(isolate, "readyState"),
            js_string(isolate, "loading")).Check();
        frame_document_value->Set(
            local_context,
            js_string(isolate, "currentScript"),
            v8::Null(isolate)).Check();
        frame_document_value->Set(
            local_context,
            js_string(isolate, "oninput"),
            v8::Null(isolate)).Check();

        global->Set(local_context, js_string(isolate, "window"), global).Check();
        global->Set(local_context, js_string(isolate, "self"), global).Check();
        global->Set(local_context, js_string(isolate, "document"), frame_document_value).Check();
        auto& frame_storage = frame_session_storage.try_emplace(
            frame.id,
            std::make_unique<session_storage_state>()).first->second;
        global->Set(
            local_context,
            js_string(isolate, "sessionStorage"),
            create_session_storage(local_context, *frame_storage)).Check();
        global->Set(
            local_context,
            js_string(isolate, "localStorage"),
            create_session_storage(local_context, outer_local_storage)).Check();
        actual_frame_windows[frame.id].Reset(isolate, global);
        actual_frame_contexts[frame.id].Reset(isolate, local_context);
        actual_frame_documents[frame.id].Reset(isolate, frame_document_value);
        global->Set(local_context, js_string(isolate, "name"), js_string(isolate, frame.attributes["name"].c_str())).Check();
        auto parent_window = context.Get(isolate)->Global();
        for (auto* ancestor = frame.parent; ancestor != nullptr; ancestor = ancestor->parent) {
            if (ancestor->tag != "iframe") continue;
            const auto owner_window = actual_frame_windows.find(ancestor->id);
            if (owner_window != actual_frame_windows.end()
                && !owner_window->second.IsEmpty()) {
                parent_window = owner_window->second.Get(isolate);
            }
            break;
        }
        global->Set(local_context, js_string(isolate, "parent"), parent_window).Check();
        global->Set(local_context, js_string(isolate, "top"), context.Get(isolate)->Global()).Check();
        global->Set(local_context, js_string(isolate, "frameElement"), frame_element).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "devicePixelRatio"),
            get_device_pixel_ratio).Check();
        global->SetNativeDataProperty(local_context, js_string(isolate, "innerWidth"), get_inner_width).Check();
        global->SetNativeDataProperty(local_context, js_string(isolate, "innerHeight"), get_inner_height).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "scrollX"),
            get_window_scroll_x).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "scrollY"),
            get_window_scroll_y).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "pageXOffset"),
            get_window_scroll_x).Check();
        global->SetNativeDataProperty(
            local_context,
            js_string(isolate, "pageYOffset"),
            get_window_scroll_y).Check();
        install_screen(local_context, global);
        global->Set(local_context, js_string(isolate, "setTimeout"), v8::Function::New(local_context, set_timeout).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "clearTimeout"), v8::Function::New(local_context, clear_timeout).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "setInterval"), v8::Function::New(local_context, set_interval).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "clearInterval"), v8::Function::New(local_context, clear_timeout).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "requestAnimationFrame"), v8::Function::New(local_context, request_animation_frame).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "cancelAnimationFrame"), v8::Function::New(local_context, clear_timeout).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "getComputedStyle"), v8::Function::New(local_context, get_computed_style).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "addEventListener"), v8::Function::New(local_context, add_event_listener).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "removeEventListener"), v8::Function::New(local_context, remove_event_listener).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "dispatchEvent"), v8::Function::New(local_context, dispatch_event).ToLocalChecked()).Check();
        install_window_post_message(local_context, global);
        auto event_template = v8::FunctionTemplate::New(isolate, event_constructor);
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "preventDefault"),
            v8::FunctionTemplate::New(isolate, event_prevent_default));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "initEvent"),
            v8::FunctionTemplate::New(isolate, event_init_event));
        event_template->PrototypeTemplate()->SetAccessorProperty(
            js_string(isolate, "returnValue"),
            v8::FunctionTemplate::New(isolate, event_return_value_get),
            v8::FunctionTemplate::New(isolate, event_return_value_set));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "stopPropagation"),
            v8::FunctionTemplate::New(isolate, event_stop_propagation));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "stopImmediatePropagation"),
            v8::FunctionTemplate::New(isolate, event_stop_immediate_propagation));
        event_template->PrototypeTemplate()->Set(
            js_string(isolate, "composedPath"),
            v8::FunctionTemplate::New(isolate, event_composed_path));
        global->Set(local_context, js_string(isolate, "Event"), event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "CustomEvent"), event_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto mouse_event_template = v8::FunctionTemplate::New(isolate, mouse_event_constructor);
        mouse_event_template->Inherit(event_template);
        auto css = v8::Object::New(isolate);
        css->Set(
            local_context,
            js_string(isolate, "escape"),
            v8::Function::New(local_context, css_escape, {}, 1).ToLocalChecked()).Check();
        css->Set(
            local_context,
            js_string(isolate, "supports"),
            v8::Function::New(local_context, css_supports).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "CSS"), css).Check();
        auto mutation_observer_template = v8::FunctionTemplate::New(
            isolate,
            observer_constructor,
            js_string(isolate, "MutationObserver.constructor"));
        global->Set(local_context, js_string(isolate, "MutationObserver"), mutation_observer_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "ResizeObserver"),
            v8::FunctionTemplate::New(isolate, resize_observer_constructor)
                ->GetFunction(local_context).ToLocalChecked()).Check();
        auto intersection_observer_template = v8::FunctionTemplate::New(
            isolate,
            observer_constructor,
            js_string(isolate, "IntersectionObserver.constructor"));
        global->Set(local_context, js_string(isolate, "IntersectionObserver"), intersection_observer_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "AbortController"), v8::FunctionTemplate::New(isolate, abort_controller_constructor)->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "matchMedia"), v8::Function::New(local_context, match_media).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "open"), v8::Function::New(local_context, window_open).ToLocalChecked()).Check();
#if defined(WEBSCENE_NATIVE_ENGINE_GENERATED_DOM_BINDINGS)
        install_generated_dom_constructors(local_context, global);
#else
        auto element_constructor = element_template.Get(isolate)->GetFunction(local_context).ToLocalChecked();
        element_constructor->Set(
            local_context,
            js_string(isolate, "ELEMENT_NODE"),
            v8::Integer::New(isolate, 1)).Check();
        element_constructor->Set(
            local_context,
            js_string(isolate, "DOCUMENT_NODE"),
            v8::Integer::New(isolate, 9)).Check();
        global->Set(local_context, js_string(isolate, "Node"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "Element"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLButtonElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLCanvasElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLIFrameElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLImageElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "HTMLInputElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "SVGElement"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "DocumentFragment"), element_constructor).Check();
        global->Set(local_context, js_string(isolate, "Window"), element_constructor).Check();
#endif
        global->SetLazyDataProperty(
            local_context,
            js_string(isolate, "HTMLCollection"),
            get_html_collection_constructor).Check();
        install_document_constructor(
            local_context,
            global,
            frame_document_value);
        global->Set(local_context, js_string(isolate, "KeyboardEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "MouseEvent"),
            mouse_event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "PointerEvent"),
            mouse_event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "WheelEvent"),
            mouse_event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "FocusEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "InputEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "UIEvent"),
            event_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "getSelection"),
            v8::Function::New(local_context, get_selection).ToLocalChecked()).Check();
        auto text_encoder_template = v8::FunctionTemplate::New(isolate);
        text_encoder_template->PrototypeTemplate()->Set(
            js_string(isolate, "encode"),
            v8::FunctionTemplate::New(isolate, text_encoder_encode));
        global->Set(local_context, js_string(isolate, "TextEncoder"), text_encoder_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto path_template = v8::FunctionTemplate::New(isolate, path_2d_constructor);
        global->Set(local_context, js_string(isolate, "Path2D"), path_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto dom_parser_template = v8::FunctionTemplate::New(isolate);
        dom_parser_template->PrototypeTemplate()->Set(
            js_string(isolate, "parseFromString"),
            v8::FunctionTemplate::New(isolate, dom_parser_parse));
        global->Set(local_context, js_string(isolate, "DOMParser"), dom_parser_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto xml_serializer_template = v8::FunctionTemplate::New(isolate);
        xml_serializer_template->PrototypeTemplate()->Set(
            js_string(isolate, "serializeToString"),
            v8::FunctionTemplate::New(isolate, xml_serializer_serialize));
        global->Set(
            local_context,
            js_string(isolate, "XMLSerializer"),
            xml_serializer_template->GetFunction(local_context).ToLocalChecked()).Check();
        auto matrix_template = v8::FunctionTemplate::New(isolate, dom_matrix_constructor);
        global->Set(local_context, js_string(isolate, "DOMMatrix"), matrix_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "DOMMatrixReadOnly"), matrix_template->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "Image"),
            v8::FunctionTemplate::New(isolate, image_constructor)
                ->GetFunction(local_context).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneCreateObjectUrl"),
            v8::Function::New(local_context, create_object_url).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneResolveUrl"),
            v8::Function::New(local_context, resolve_url).ToLocalChecked()).Check();
        global->Set(
            local_context,
            js_string(isolate, "__webSceneRecordWebApi"),
            v8::Function::New(local_context, record_web_api_use).ToLocalChecked()).Check();

        const auto& current_frame_base = current_base_address();
        const auto frame_href = !current_frame_base.empty()
            ? current_frame_base
            : std::string("http://127.0.0.1/")
                + resource_root.filename().generic_string() + '/';
        set_context_location(local_context, global, frame_href);

        install_navigator(isolate, local_context, global);

        auto console = v8::Object::New(isolate);
        console->Set(local_context, js_string(isolate, "log"),
            v8::Function::New(local_context, console_log, v8::Integer::New(isolate, 0)).ToLocalChecked()).Check();
        console->Set(local_context, js_string(isolate, "warn"),
            v8::Function::New(local_context, console_log, v8::Integer::New(isolate, 1)).ToLocalChecked()).Check();
        console->Set(local_context, js_string(isolate, "error"),
            v8::Function::New(local_context, console_log, v8::Integer::New(isolate, 2)).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "console"), console).Check();

        auto performance = v8::Object::New(isolate);
        performance->Set(local_context, js_string(isolate, "now"), v8::Function::New(local_context, performance_now).ToLocalChecked()).Check();
        performance->Set(
            local_context,
            js_string(isolate, "getEntriesByName"),
            v8::Function::New(local_context, performance_get_entries_by_name).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "mark"),
            v8::Function::New(local_context, performance_entry).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "measure"),
            v8::Function::New(local_context, performance_entry).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "clearMarks"),
            v8::Function::New(local_context, console_log).ToLocalChecked()).Check();
        performance->Set(local_context, js_string(isolate, "clearMeasures"),
            v8::Function::New(local_context, console_log).ToLocalChecked()).Check();
        global->Set(local_context, js_string(isolate, "performance"), performance).Check();

        // React's scheduler prefers MessageChannel over timers. Keep the ports
        // realm-local and route delivery through this frame's setTimeout so a
        // posted message is a serialized browser task rather than a nested V8
        // call. This mirrors the working managed V8 iframe implementation.
        constexpr std::string_view message_channel_source = R"JS(
            (() => {
              class MessagePort {
                constructor() {
                  this.onmessage = null;
                  this._listeners = [];
                  this._peer = null;
                  this._closed = false;
                }
                postMessage(data) {
                  __webSceneRecordWebApi(
                    'MessagePort.postMessage', 'partially-supported',
                    'same-realm queued delivery without structured clone or transferable objects');
                  const target = this._peer;
                  if (this._closed || target === null || target._closed) return;
                  setTimeout(() => {
                    if (target._closed) return;
                    const event = {
                      type: 'message', data, target, currentTarget: target,
                      ports: [], origin: '', lastEventId: ''
                    };
                    if (typeof target.onmessage === 'function') {
                      target.onmessage.call(target, event);
                    }
                    for (const listener of target._listeners.slice()) {
                      listener.call(target, event);
                    }
                  }, 0);
                }
                addEventListener(type, listener) {
                  if (type === 'message' && typeof listener === 'function'
                      && !this._listeners.includes(listener)) {
                    this._listeners.push(listener);
                  }
                }
                removeEventListener(type, listener) {
                  if (type !== 'message') return;
                  const index = this._listeners.indexOf(listener);
                  if (index >= 0) this._listeners.splice(index, 1);
                }
                start() {}
                close() { this._closed = true; }
              }
              class MessageChannel {
                constructor() {
                  __webSceneRecordWebApi(
                    'MessageChannel.constructor', 'partially-supported',
                    'same-realm paired ports without transferable-object semantics');
                  this.port1 = new MessagePort();
                  this.port2 = new MessagePort();
                  this.port1._peer = this.port2;
                  this.port2._peer = this.port1;
                }
              }
              class WebSceneBlob {
                constructor(parts = [], options = {}) {
                  __webSceneRecordWebApi(
                    'Blob.constructor', 'partially-supported',
                    'byte-preserving construction, type, size, object URLs, and downloads without slicing or streaming');
                  const chunks = [];
                  let size = 0;
                  for (const part of parts) {
                    let bytes;
                    if (part instanceof ArrayBuffer) {
                      bytes = new Uint8Array(part);
                    } else if (ArrayBuffer.isView(part)) {
                      bytes = new Uint8Array(part.buffer, part.byteOffset, part.byteLength);
                    } else {
                      bytes = new TextEncoder().encode(String(part));
                    }
                    chunks.push(bytes);
                    size += bytes.byteLength;
                  }
                  this._bytes = new Uint8Array(size);
                  let offset = 0;
                  for (const chunk of chunks) {
                    this._bytes.set(chunk, offset);
                    offset += chunk.byteLength;
                  }
                  this.size = size;
                  this.type = String(options.type || '').toLowerCase();
                  this._text = Array.from(parts, String).join('');
                }
                toString() { return this._text; }
              }
              class WebSceneURLSearchParams {
                constructor(init = null) {
                  this._owner = init && typeof init === 'object'
                    && 'href' in init ? init : null;
                  this._pairs = [];
                  if (!this._owner && init !== null && typeof init === 'object') {
                    if (typeof init[Symbol.iterator] === 'function') {
                      for (const entry of init) {
                        const pair = Array.from(entry);
                        if (pair.length !== 2) {
                          throw new TypeError(
                            'URLSearchParams sequence entries must contain exactly two items');
                        }
                        this._pairs.push([String(pair[0]), String(pair[1])]);
                      }
                    } else {
                      for (const name of Object.keys(init)) {
                        this._pairs.push([String(name), String(init[name])]);
                      }
                    }
                    return;
                  }
                  const query = String(this._owner ? this._owner.search : init ?? '')
                    .replace(/^\?/, '');
                  for (const item of query ? query.split('&') : []) {
                    const separator = item.indexOf('=');
                    const decode = value => {
                      value = String(value).replace(/\+/g, ' ');
                      try { return decodeURIComponent(value); }
                      catch { return value; }
                    };
                    this._pairs.push(separator < 0
                      ? [decode(item), '']
                      : [decode(item.slice(0, separator)),
                        decode(item.slice(separator + 1))]);
                  }
                }
                _updateOwner() {
                  if (!this._owner) return;
                  const serialized = this.toString();
                  this._owner.search = serialized ? `?${serialized}` : '';
                  this._owner.href = this._owner.origin + this._owner.pathname
                    + this._owner.search + this._owner.hash;
                }
                append(name, value) {
                  __webSceneRecordWebApi(
                    'URLSearchParams.append', 'partially-supported',
                    'ordered query parsing and common mutation/query operations');
                  this._pairs.push([String(name), String(value)]);
                  this._updateOwner();
                }
                get(name) {
                  name = String(name);
                  const pair = this._pairs.find(entry => entry[0] === name);
                  return pair ? pair[1] : null;
                }
                getAll(name) {
                  name = String(name);
                  return this._pairs
                    .filter(entry => entry[0] === name)
                    .map(entry => entry[1]);
                }
                has(name) {
                  name = String(name);
                  return this._pairs.some(entry => entry[0] === name);
                }
                set(name, value) {
                  name = String(name);
                  value = String(value);
                  const first = this._pairs.findIndex(entry => entry[0] === name);
                  this._pairs = this._pairs.filter(entry => entry[0] !== name);
                  this._pairs.splice(first < 0 ? this._pairs.length : first, 0, [name, value]);
                  this._updateOwner();
                }
                delete(name) {
                  name = String(name);
                  this._pairs = this._pairs.filter(entry => entry[0] !== name);
                  this._updateOwner();
                }
                sort() {
                  this._pairs = this._pairs
                    .map((entry, index) => ({ entry, index }))
                    .sort((left, right) => left.entry[0] < right.entry[0] ? -1
                      : left.entry[0] > right.entry[0] ? 1
                      : left.index - right.index)
                    .map(item => item.entry);
                  this._updateOwner();
                }
                entries() { return this._pairs.map(entry => [...entry])[Symbol.iterator](); }
                keys() { return this._pairs.map(entry => entry[0])[Symbol.iterator](); }
                values() { return this._pairs.map(entry => entry[1])[Symbol.iterator](); }
                forEach(callback, thisArg = undefined) {
                  for (const [name, value] of this._pairs) {
                    callback.call(thisArg, value, name, this);
                  }
                }
                [Symbol.iterator]() { return this.entries(); }
                get size() { return this._pairs.length; }
                toString() {
                  const encode = value => encodeURIComponent(String(value))
                    .replace(/%20/g, '+')
                    .replace(/[!'()~]/g, character =>
                      `%${character.charCodeAt(0).toString(16).toUpperCase()}`);
                  return this._pairs.map(([name, value]) =>
                    `${encode(name)}=${encode(value)}`).join('&');
                }
              }
        )JS";
        constexpr std::string_view message_channel_source_suffix = R"JS(
              class WebSceneFormData {
                constructor(form = undefined) {
                  __webSceneRecordWebApi(
                    'FormData.constructor', 'partially-supported',
                    'ordered string and Blob fields with multipart fetch serialization');
                  this._entries = [];
                  if (form !== undefined && form !== null) {
                    throw new TypeError('Constructing FormData from a form is not yet supported');
                  }
                }
                append(name, value, filename = undefined) {
                  this._entries.push([
                    String(name), value instanceof Blob ? value : String(value),
                    filename === undefined ? undefined : String(filename)
                  ]);
                }
                delete(name) {
                  name = String(name);
                  this._entries = this._entries.filter(entry => entry[0] !== name);
                }
                get(name) {
                  name = String(name);
                  const entry = this._entries.find(item => item[0] === name);
                  return entry ? entry[1] : null;
                }
                getAll(name) {
                  name = String(name);
                  return this._entries.filter(item => item[0] === name).map(item => item[1]);
                }
                has(name) {
                  name = String(name);
                  return this._entries.some(item => item[0] === name);
                }
                set(name, value, filename = undefined) {
                  name = String(name);
                  const index = this._entries.findIndex(item => item[0] === name);
                  this.delete(name);
                  const entry = [
                    name, value instanceof Blob ? value : String(value),
                    filename === undefined ? undefined : String(filename)
                  ];
                  this._entries.splice(index < 0 ? this._entries.length : index, 0, entry);
                }
                entries() { return this._entries.map(entry => [entry[0], entry[1]])[Symbol.iterator](); }
                keys() { return this._entries.map(entry => entry[0])[Symbol.iterator](); }
                values() { return this._entries.map(entry => entry[1])[Symbol.iterator](); }
                forEach(callback, thisArg = undefined) {
                  for (const [name, value] of this._entries) {
                    callback.call(thisArg, value, name, this);
                  }
                }
                [Symbol.iterator]() { return this.entries(); }
                _encode(boundary) {
                  const escape = value => String(value)
                    .replace(/\r|\n/g, ' ')
                    .replace(/"/g, '%22');
                  let result = '';
                  for (const [name, value, filename] of this._entries) {
                    result += `--${boundary}\r\nContent-Disposition: form-data; name="${escape(name)}"`;
                    if (value instanceof Blob) {
                      result += `; filename="${escape(filename ?? 'blob')}"\r\n`;
                      if (value.type) result += `Content-Type: ${value.type}\r\n`;
                      result += `\r\n${value.toString()}\r\n`;
                    } else {
                      result += `\r\n\r\n${value}\r\n`;
                    }
                  }
                  return `${result}--${boundary}--\r\n`;
                }
              }
              class WebSceneURL {
                constructor(value, base = globalThis.location?.href || '') {
                  __webSceneRecordWebApi(
                    'URL.constructor', 'partially-supported',
                    'hierarchical URL resolution and common components without the complete URL parser');
                  this.href = __webSceneResolveUrl(String(value), String(base));
                  const match = /^(?:([a-z][a-z0-9+.-]*:))?(?:\/\/([^/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(this.href) || [];
                  this.protocol = match[1] || '';
                  this.host = match[2] || '';
                  const separator = this.host.lastIndexOf(':');
                  this.hostname = separator > 0 ? this.host.slice(0, separator) : this.host;
                  this.port = separator > 0 ? this.host.slice(separator + 1) : '';
                  this.pathname = match[3] || '';
                  this.search = match[4] || '';
                  this.hash = match[5] || '';
                  this.origin = this.protocol && this.host ? `${this.protocol}//${this.host}` : 'null';
                  this.searchParams = new WebSceneURLSearchParams(this);
                }
                toString() {
                  const search = this.search && !this.search.startsWith('?')
                    ? `?${this.search}` : this.search;
                  const hash = this.hash && !this.hash.startsWith('#')
                    ? `#${this.hash}` : this.hash;
                  return `${this.protocol}${this.host ? `//${this.host}` : ''}`
                    + `${this.pathname}${search}${hash}`;
                }
                toJSON() { return this.toString(); }
                static createObjectURL(blob) { return __webSceneCreateObjectUrl(blob); }
                static revokeObjectURL() {}
              }
              class WebSceneDOMException extends Error {
                constructor(message = '', name = 'Error') {
                  super(String(message));
                  this.name = String(name);
                  const legacyCodes = {
                    IndexSizeError: 1,
                    HierarchyRequestError: 3,
                    WrongDocumentError: 4,
                    InvalidCharacterError: 5,
                    NoModificationAllowedError: 7,
                    NotFoundError: 8,
                    NotSupportedError: 9,
                    InUseAttributeError: 10,
                    InvalidStateError: 11,
                    SyntaxError: 12,
                    InvalidModificationError: 13,
                    NamespaceError: 14,
                    InvalidAccessError: 15,
                    TypeMismatchError: 17,
                    SecurityError: 18,
                    NetworkError: 19,
                    AbortError: 20,
                    URLMismatchError: 21,
                    QuotaExceededError: 22,
                    TimeoutError: 23,
                    InvalidNodeTypeError: 24,
                    DataCloneError: 25
                  };
                  this.code = legacyCodes[this.name] || 0;
                }
              }
              class WebSceneAbortSignal {
                constructor() {
                  this.aborted = false;
                  this.reason = undefined;
                  this.onabort = null;
                  this._listeners = [];
                }
                addEventListener(type, listener, options) {
                  if (type !== 'abort' || (typeof listener !== 'function'
                      && typeof listener?.handleEvent !== 'function')) return;
                  if (this._listeners.some(entry => entry.listener === listener)) return;
                  this._listeners.push({
                    listener,
                    once: options === true || Boolean(options?.once)
                  });
                }
                removeEventListener(type, listener) {
                  if (type !== 'abort') return;
                  const index = this._listeners.findIndex(entry => entry.listener === listener);
                  if (index >= 0) this._listeners.splice(index, 1);
                }
                throwIfAborted() {
                  if (this.aborted) throw this.reason;
                }
                _abort(reason) {
                  if (this.aborted) return;
                  this.aborted = true;
                  const error = reason === undefined
                    ? new WebSceneDOMException('This operation was aborted', 'AbortError')
                    : reason;
                  this.reason = error;
                  const event = { type: 'abort', target: this, currentTarget: this };
                  if (typeof this.onabort === 'function') this.onabort.call(this, event);
                  for (const entry of this._listeners.slice()) {
                    if (typeof entry.listener === 'function') entry.listener.call(this, event);
                    else entry.listener.handleEvent(event);
                    if (entry.once) this.removeEventListener('abort', entry.listener);
                  }
                }
              }
              class WebSceneAbortController {
                constructor() {
                  __webSceneRecordWebApi(
                    'AbortController.constructor', 'partially-supported',
                    'abort state and listener delivery without static AbortSignal composition helpers');
                  this.signal = new WebSceneAbortSignal();
                }
                abort(reason) { this.signal._abort(reason); }
              }
              Object.defineProperties(globalThis, {
                MessagePort: { value: MessagePort, configurable: true },
                MessageChannel: { value: MessageChannel, configurable: true },
                DOMException: { value: WebSceneDOMException, configurable: true },
                AbortSignal: { value: WebSceneAbortSignal, configurable: true },
                AbortController: { value: WebSceneAbortController, configurable: true },
                Blob: { value: WebSceneBlob, configurable: true },
                URL: { value: WebSceneURL, configurable: true },
                URLSearchParams: { value: WebSceneURLSearchParams, configurable: true },
                FormData: { value: WebSceneFormData, configurable: true },
                CSS: {
                  value: {
                    supports() {
                      __webSceneRecordWebApi(
                        'CSS.supports', 'unsupported',
                        'capability conditions are reported false without property/value evaluation');
                      return false;
                    },
                    escape(value) {
                      __webSceneRecordWebApi(
                        'CSS.escape', 'partially-supported',
                        'ASCII identifier escaping without the complete CSSOM serialization algorithm');
                      return String(value).replace(/[^a-zA-Z0-9_-]/g, character => `\\${character}`);
                    }
                  },
                  configurable: true
                },
                crypto: {
                  value: {
                    getRandomValues(array) {
                      __webSceneRecordWebApi(
                        'Crypto.getRandomValues', 'partially-supported',
                        'integer TypedArray filling without a cryptographic entropy guarantee');
                      if (!ArrayBuffer.isView(array) || array instanceof DataView) {
                        throw new TypeError('Expected an integer TypedArray');
                      }
                      for (let index = 0; index < array.length; index++) {
                        array[index] = Math.floor(Math.random() * 256);
                      }
                      return array;
                    }
                  },
                  configurable: true
                }
              });
            })();
        )JS";
        std::string message_channel_bootstrap;
        message_channel_bootstrap.reserve(
            message_channel_source.size() + message_channel_source_suffix.size());
        message_channel_bootstrap.append(message_channel_source);
        message_channel_bootstrap.append(message_channel_source_suffix);
        auto message_channel_script = v8::Script::Compile(
            local_context,
            js_string(isolate, message_channel_bootstrap.c_str())).ToLocalChecked();
        message_channel_script->Run(local_context).ToLocalChecked();
        install_clipboard_api(local_context);
        install_websocket_globals(local_context);
        install_editor_web_platform_globals(local_context);
        install_tree_walker_platform(local_context);
        install_custom_elements_platform(local_context);
        install_fetch_globals(local_context);
        install_intersection_observer_polyfill(local_context);
        static_cast<void>(frame_body);
    }

    void start_frame_preparation(dom_node& frame, const std::string& source)
    {
        for (auto iterator = pending_frame_preparations.begin();
             iterator != pending_frame_preparations.end();) {
            auto* candidate = document.find_by_native_id(iterator->first);
            auto stale = candidate == nullptr || !is_connected(*candidate);
            if (!stale) {
                const auto candidate_source = candidate->attributes.find("src");
                stale = candidate_source == candidate->attributes.end()
                    || candidate_source->second != iterator->second.source;
            }
            if (stale
                && iterator->second.future.valid()
                && iterator->second.future.wait_for(std::chrono::seconds(0))
                    == std::future_status::ready) {
                iterator = pending_frame_preparations.erase(iterator);
            } else {
                ++iterator;
            }
        }
        if (!prepare_iframe_subresources
            || !prefetch_iframe_documents
            || source.empty()
            || pending_frame_preparations.size() >= maximum_frame_preparations
            || pending_frame_preparations.contains(frame.id)) {
            return;
        }
        const auto document_key = resource_prefetch_key(
            WEBSCENE_RESOURCE_DOCUMENT,
            source);
        std::shared_future<prefetched_resource> document_future;
        {
            std::lock_guard lock(resource_prefetch_mutex);
            const auto known = prefetched_resources.find(document_key);
            if (known == prefetched_resources.end()) return;
            document_future = known->second;
        }
        if (!document_future.valid()) return;
        const auto outer_base_address = document_base_address;

        pending_frame_preparation preparation;
        preparation.source = source;
        preparation.future = std::async(
            std::launch::async,
            [this, source, outer_base_address, document_future]()
                -> std::shared_ptr<const prepared_frame> {
                const auto started = std::chrono::steady_clock::now();
                try {
                    const auto& document = document_future.get();
                    const immutable_text_source* content = nullptr;
                    if (document.from_fresh_cache && document.cached.content != nullptr) {
                        content = document.cached.content.get();
                    } else if (document.loaded && document.response != nullptr
                        && !document.response->not_modified) {
                        auto result = std::make_shared<prepared_frame>();
                        result->source = source;
                        result->html = document.response->content;
                        result->started = started;
                        prepare_frame_resource_plan(*result, outer_base_address);
                        result->completed = std::chrono::steady_clock::now();
                        return result;
                    } else if (document.has_cached && document.cached.content != nullptr) {
                        content = document.cached.content.get();
                    }
                    if (content == nullptr) return {};
                    auto result = std::make_shared<prepared_frame>();
                    result->source = source;
                    result->html.assign(content->data(), content->size());
                    result->started = started;
                    prepare_frame_resource_plan(*result, outer_base_address);
                    result->completed = std::chrono::steady_clock::now();
                    return result;
                } catch (...) {
                    return {};
                }
            }).share();
        pending_frame_preparations.emplace(frame.id, std::move(preparation));
    }

    void prepare_frame_resource_plan(
        prepared_frame& result,
        const std::string& outer_base_address)
    {
        const auto normalized_html = lower_html_name(result.html);
        const auto opening_tag = [&](std::string_view name) {
            const auto open = normalized_html.find("<" + std::string(name));
            const auto close = open == std::string::npos
                ? std::string::npos : normalized_html.find('>', open);
            return open == std::string::npos || close == std::string::npos
                ? std::string{}
                : result.html.substr(open, close - open + 1U);
        };
        result.base_address = resolve_resource_url(result.source, outer_base_address);
        const auto base_opening = opening_tag("base");
        if (!base_opening.empty()) {
            for (const auto& [name, value] : parse_html_attributes(base_opening, 5U)) {
                if (lower_html_name(name) == "href" && !value.empty()) {
                    result.base_address = resolve_resource_url(value, outer_base_address);
                    break;
                }
            }
        }
        result.html_opening = opening_tag("html");
        result.body_opening = opening_tag("body");
        result.stylesheets = parse_frame_stylesheets(result.html);
        result.scripts = parse_frame_scripts(result.html);
        for (const auto& href : result.stylesheets) {
            start_resource_prefetch(
                href,
                result.base_address,
                WEBSCENE_RESOURCE_STYLESHEET);
        }
        for (const auto& script : result.scripts) {
            if (!script.source.empty()) {
                start_resource_prefetch(
                    script.source,
                    result.base_address,
                    WEBSCENE_RESOURCE_SCRIPT);
            }
        }
    }

    std::shared_ptr<const prepared_frame> take_frame_preparation(
        const dom_node& frame,
        const std::string& html)
    {
        const auto known = pending_frame_preparations.find(frame.id);
        if (known == pending_frame_preparations.end()) return {};
        const auto source = frame.attributes.find("src");
        if (source == frame.attributes.end()
            || source->second != known->second.source
            || !known->second.future.valid()) {
            if (known->second.future.valid()
                && known->second.future.wait_for(std::chrono::seconds(0))
                    == std::future_status::ready) {
                pending_frame_preparations.erase(known);
            }
            return {};
        }
        auto preparation = std::move(known->second);
        pending_frame_preparations.erase(known);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto wait_started = std::chrono::steady_clock::now();
#endif
        std::shared_ptr<const prepared_frame> result;
        try {
            result = preparation.future.get();
        } catch (...) {
            return {};
        }
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto consumed = std::chrono::steady_clock::now();
        if (profile_startup) {
            ++startup_frame_prepare_wait.calls;
            startup_frame_prepare_wait.nanoseconds += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    consumed - wait_started).count());
            if (result != nullptr) {
                ++startup_frame_prepare.calls;
                startup_frame_prepare.nanoseconds += static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        result->completed - result->started).count());
                if (consumed > result->completed) {
                    startup_frame_prepare_lead_nanoseconds += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            consumed - result->completed).count());
                }
            }
        }
#endif
        return result != nullptr && result->html == html ? result : nullptr;
    }

    bool execute_frame_hydration_script(
        frame_hydration_state& hydration,
        const frame_script& script)
    {
        auto local_frame_context = hydration.context.Get(isolate);
        if (local_frame_context.IsEmpty()) {
            frame_last_error_value = "Iframe context was discarded during hydration";
            ++frame_script_error_count;
            return false;
        }
        std::string source = script.code;
        std::shared_ptr<const immutable_text_source> immutable_source;
        auto name = "webscene-frame-inline-" + std::to_string(script.index) + ".js";
        if (!script.source.empty()) {
            if (!load_text_resource(
                    script.source,
                    hydration.base_address,
                    WEBSCENE_RESOURCE_SCRIPT,
                    source,
                    name,
                    &immutable_source)) {
                frame_last_error_value = "Unable to load iframe script: " + script.source;
                ++frame_script_error_count;
                return false;
            }
        }
        {
            v8::Context::Scope context_scope(local_frame_context);
            auto document_value = local_frame_context->Global()->Get(
                local_frame_context,
                js_string(isolate, "document")).ToLocalChecked().As<v8::Object>();
            set_current_script(local_frame_context, document_value, name);
        }
        std::string error;
        const auto script_source = immutable_source != nullptr
            ? immutable_source->view()
            : std::string_view(source);
        if (!execute_in_context(
                local_frame_context,
                script_source,
                name,
                error,
                true,
                std::move(immutable_source))) {
            frame_last_error_value = "Iframe script #" + std::to_string(script.index)
                + " failed: " + error;
            ++frame_script_error_count;
            return false;
        }
        ++frame_script_execution_count;
        return true;
    }

    frame_hydration_result continue_frame_hydration(
        dom_node& frame,
        frame_hydration_state& hydration,
        bool cooperative)
    {
        if (hydration.body == nullptr || hydration.context.IsEmpty()) {
            frame_last_error_value = "Iframe hydration state was discarded";
            ++frame_script_error_count;
            return frame_hydration_result::failed;
        }
        activate_css_cascade(hydration.body);
        const auto restore_outer = [&](frame_hydration_result result) {
            activate_css_cascade(context.Get(isolate));
            return result;
        };
        const auto slice_started = std::chrono::steady_clock::now();
        constexpr auto slice_budget = std::chrono::milliseconds(8);

        while (hydration.phase != frame_hydration_phase::lifecycle) {
            const auto defer =
                hydration.phase == frame_hydration_phase::deferred_scripts;
            auto executed_script = false;
            while (hydration.next_script < hydration.scripts.size()) {
                const auto& script = hydration.scripts[hydration.next_script++];
                if (script.defer != defer) continue;
                if (!execute_frame_hydration_script(hydration, script)) {
                    return restore_outer(frame_hydration_result::failed);
                }
                executed_script = true;
                if (cooperative
                    && std::chrono::steady_clock::now() - slice_started >= slice_budget) {
                    return restore_outer(frame_hydration_result::pending);
                }
            }

            hydration.next_script = 0U;
            if (hydration.phase == frame_hydration_phase::blocking_scripts) {
                auto local_frame_context = hydration.context.Get(isolate);
                v8::Context::Scope context_scope(local_frame_context);
                auto document_value = local_frame_context->Global()->Get(
                    local_frame_context,
                    js_string(isolate, "document")).ToLocalChecked().As<v8::Object>();
                document_value->Set(
                    local_frame_context,
                    js_string(isolate, "readyState"),
                    js_string(isolate, "interactive")).Check();
                hydration.phase = frame_hydration_phase::deferred_scripts;
                if (cooperative && executed_script) {
                    return restore_outer(frame_hydration_result::pending);
                }
            } else {
                hydration.phase = frame_hydration_phase::lifecycle;
                if (cooperative && executed_script) {
                    return restore_outer(frame_hydration_result::pending);
                }
            }
        }

        auto local_frame_context = hydration.context.Get(isolate);
        v8::Context::Scope lifecycle_context_scope(local_frame_context);
        const auto dispatch_lifecycle_event = [&](v8::Local<v8::Object> target, const char* type) {
            auto event = create_event_instance(local_frame_context);
            event->Set(
                local_frame_context,
                js_string(isolate, "type"),
                js_string(isolate, type)).Check();
            event->Set(
                local_frame_context,
                js_string(isolate, "bubbles"),
                v8::Boolean::New(
                    isolate,
                    std::string_view(type) == "DOMContentLoaded")).Check();
            event->Set(
                local_frame_context,
                js_string(isolate, "cancelable"),
                v8::False(isolate)).Check();
            event->Set(
                local_frame_context,
                js_string(isolate, "defaultPrevented"),
                v8::False(isolate)).Check();
            v8::Local<v8::Value> dispatcher;
            if (!target->Get(
                    local_frame_context,
                    js_string(isolate, "dispatchEvent")).ToLocal(&dispatcher)
                || !dispatcher->IsFunction()) {
                frame_last_error_value =
                    std::string("Iframe ") + type + " dispatcher is unavailable";
                ++frame_script_error_count;
                return false;
            }
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> arguments[] = {event};
            if (dispatcher.As<v8::Function>()->Call(
                    local_frame_context,
                    target,
                    1,
                    arguments).IsEmpty()) {
                frame_last_error_value = std::string("Iframe ") + type
                    + " dispatch failed: "
                    + describe_exception(try_catch, local_frame_context);
                ++frame_script_error_count;
                return false;
            }
            perform_microtask_checkpoint();
            return true;
        };
        auto frame_global = local_frame_context->Global();
        auto document_value = frame_global->Get(
            local_frame_context,
            js_string(isolate, "document")).ToLocalChecked().As<v8::Object>();
        if (!dispatch_lifecycle_event(document_value, "DOMContentLoaded")) {
            return restore_outer(frame_hydration_result::failed);
        }
        document_value->Set(
            local_frame_context,
            js_string(isolate, "readyState"),
            js_string(isolate, "complete")).Check();
        auto body_onload = std::string{};
        for (auto [name, value] : parse_html_attributes(hydration.body_opening, 5U)) {
            if (lower_html_name(name) != "onload") continue;
            body_onload = std::move(value);
            break;
        }
        if (!body_onload.empty()) {
            std::string error;
            const auto handler_source = "(function(event){" + body_onload
                + "\n}).call(document.body, { type: 'load' });";
            if (!execute_in_context(
                    local_frame_context,
                    handler_source,
                    hydration.base_address + "#body-onload",
                    error,
                    true)) {
                frame_last_error_value = "Iframe body load handler failed: " + error;
                ++frame_script_error_count;
                return restore_outer(frame_hydration_result::failed);
            }
        }
        if (!dispatch_lifecycle_event(frame_global, "load")) {
            return restore_outer(frame_hydration_result::failed);
        }
        perform_microtask_checkpoint();
        frame_last_error_value.clear();
        static_cast<void>(frame);
        return restore_outer(frame_hydration_result::complete);
    }

    frame_hydration_result hydrate_frame(dom_node& frame)
    {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        if (profile_startup && startup_frame_started == std::chrono::steady_clock::time_point{}) {
            startup_frame_started = std::chrono::steady_clock::now();
        }
        binding_callback_timer timer(profile_startup ? &startup_frame_hydrate : nullptr);
        auto frame_profile_name = "iframe#" + std::to_string(frame.id);
        if (const auto source = frame.attributes.find("src");
            source != frame.attributes.end() && !source->second.empty()) {
            auto source_name = resource_leaf_name(source->second);
            constexpr size_t maximum_source_name = 80U;
            if (source_name.size() > maximum_source_name) {
                source_name.resize(maximum_source_name);
            }
            frame_profile_name += ':' + source_name;
        }
        const auto frame_profile_started = profile_startup
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const auto frame_phase_before = profile_startup
            ? capture_startup_phase_snapshot()
            : startup_phase_snapshot{};
        const auto finish_frame_profile = [
            this,
            frame_profile_name,
            frame_profile_started,
            frame_phase_before](void*) {
            if (!profile_startup) return;
            const auto elapsed_nanoseconds = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - frame_profile_started).count());
            record_startup_nested_phases(
                startup_frame_phase_profiles,
                frame_profile_name,
                frame_phase_before,
                elapsed_nanoseconds);
        };
        std::unique_ptr<void, decltype(finish_frame_profile)> frame_profile_scope(
            profile_startup ? this : nullptr,
            finish_frame_profile);
#endif
        if (auto existing = frame_hydration_states.find(frame.id);
            existing != frame_hydration_states.end()) {
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
            const auto cooperative = cooperative_iframe_hydration;
#else
            constexpr auto cooperative = true;
#endif
            const auto result = continue_frame_hydration(
                frame,
                existing->second,
                cooperative);
            if (result != frame_hydration_result::pending) {
                frame_hydration_states.erase(existing);
            }
            return result;
        }
        if (!frame.attributes.contains("frame-html")
            && !frame.attributes.contains("object-html")) {
            const auto remote_result = frame.attributes.find(
                "data-webscene-remote-result");
            const auto source = frame.attributes.find("src");
            if (remote_result != frame.attributes.end()
                && remote_result->second == "pending"
                && source != frame.attributes.end()) {
                std::string html;
                std::string resolved_name;
                if (!load_text_resource(
                        source->second,
                        {},
                        WEBSCENE_RESOURCE_DOCUMENT,
                        html,
                        resolved_name)) {
                    frame.attributes["data-webscene-remote-result"] = "failed";
                    frame_last_error_value = "Unable to load iframe document: "
                        + source->second;
                    ++frame_script_error_count;
                    return frame_hydration_result::failed;
                }
                frame.attributes["src"] = std::move(resolved_name);
                frame.attributes["object-html"] = std::move(html);
                frame.attributes["data-webscene-remote-result"] = "loaded";
            }
        }
        const auto html_iterator = frame.attributes.find("frame-html");
        const auto object_iterator = frame.attributes.find("object-html");
        const auto& html = html_iterator != frame.attributes.end() && !html_iterator->second.empty()
            ? html_iterator->second
            : object_iterator != frame.attributes.end() ? object_iterator->second : empty_string;
        if (html.empty()) {
            frame_last_error_value = "Iframe document.close() received no HTML";
            ++frame_script_error_count;
            return frame_hydration_result::failed;
        }
        const auto preparation = take_frame_preparation(frame, html);
        std::vector<std::string> stylesheets;
        std::vector<frame_script> scripts;
        std::string html_opening;
        std::string body_opening;
        if (preparation != nullptr) {
            frame_base_address = preparation->base_address;
            stylesheets = preparation->stylesheets;
            scripts = preparation->scripts;
            html_opening = preparation->html_opening;
            body_opening = preparation->body_opening;
        } else {
            const auto normalized_html = lower_html_name(html);
            const auto opening_tag = [&](std::string_view name) {
                const auto open = normalized_html.find("<" + std::string(name));
                const auto close = open == std::string::npos
                    ? std::string::npos : normalized_html.find('>', open);
                return open == std::string::npos || close == std::string::npos
                    ? std::string{}
                    : html.substr(open, close - open + 1U);
            };
            const auto frame_source = frame.attributes.find("src");
            frame_base_address = frame_source != frame.attributes.end()
                && (frame_source->second.starts_with("http://")
                    || frame_source->second.starts_with("https://"))
                ? resolve_resource_url(frame_source->second, document_base_address)
                : document_base_address;
            const auto base_opening = opening_tag("base");
            if (!base_opening.empty()) {
                for (const auto& [name, value] : parse_html_attributes(base_opening, 5U)) {
                    if (lower_html_name(name) == "href" && !value.empty()) {
                        frame_base_address = resolve_resource_url(value, document_base_address);
                        break;
                    }
                }
            }
            html_opening = opening_tag("html");
            body_opening = opening_tag("body");
            stylesheets = parse_frame_stylesheets(html);
            scripts = parse_frame_scripts(html);
        }
        const auto requires_resource_loader = !stylesheets.empty()
            || std::any_of(scripts.begin(), scripts.end(), [](const auto& script) {
                return !script.source.empty();
            });
        if (requires_resource_loader && resource_root.empty() && !load_resource_callback) {
            frame_last_error_value = "The connected component resource root was not configured";
            ++frame_script_error_count;
            return frame_hydration_result::failed;
        }
        for (const auto& href : stylesheets) {
            start_resource_prefetch(href, frame_base_address, WEBSCENE_RESOURCE_STYLESHEET);
        }
        for (const auto& script : scripts) {
            if (!script.source.empty()) {
                start_resource_prefetch(
                    script.source,
                    frame_base_address,
                    WEBSCENE_RESOURCE_SCRIPT);
            }
        }

        auto& frame_body = document.create_element("body");
        frame_base_addresses[&frame_body] = frame_base_address;
        activate_css_cascade(&frame_body);
        css_rules.clear();
        opacity_keyframes.clear();
        css_rules_by_class.clear();
        css_rules_by_id.clear();
        css_rules_by_tag.clear();
        css_rules_by_attribute.clear();
        css_rules_by_variable_reference.clear();
        css_focus_rules.clear();
        unindexed_css_rules.clear();
        css_attribute_dependencies.clear();
        css_descendant_attribute_dependencies.clear();
        css_variables.clear();
        important_css_variables.clear();
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        for (auto stylesheet : parse_inline_stylesheets(html)) {
            add_stylesheet(std::move(stylesheet), frame_base_address);
        }
#endif
        for (const auto& href : stylesheets) {
            std::string stylesheet;
            std::string stylesheet_name;
            if (!load_text_resource(
                    href,
                    frame_base_address,
                    WEBSCENE_RESOURCE_STYLESHEET,
                    stylesheet,
                    stylesheet_name)) {
                frame_last_error_value = "Unable to load iframe stylesheet: " + href;
                ++frame_script_error_count;
                activate_css_cascade(context.Get(isolate));
                return frame_hydration_result::failed;
            }
            add_stylesheet(std::move(stylesheet), stylesheet_name);
        }

        record_feature(
            "html",
            "element:body",
            "supported",
            {},
            "html-element");
        // A browsing context currently uses one native layout root for the
        // browser's HTML and BODY boxes. Preserve the authored state of both
        // elements on that root so `html[data-theme=dark]`, `body.chart-page`,
        // inherited custom properties, language, and direction all cascade.
        const auto html_class = extract_html_attribute(html_opening, "class");
        const auto body_class = extract_html_attribute(body_opening, "class");
        if (!html_class.empty() || !body_class.empty()) {
            record_html_attribute_feature("class", "html-parser");
        }
        frame_body.class_name = html_class;
        if (!body_class.empty()) {
            if (!frame_body.class_name.empty()) frame_body.class_name.push_back(' ');
            frame_body.class_name += body_class;
        }
        if (!frame_body.class_name.empty()) frame_body.attributes["class"] = frame_body.class_name;
        for (const auto* attribute : {"data-theme", "lang", "dir"}) {
            auto value = extract_html_attribute(html_opening, attribute);
            if (value.empty()) value = extract_html_attribute(body_opening, attribute);
            if (!value.empty()) {
                record_html_attribute_feature(attribute, "html-parser");
                frame_body.attributes[attribute] = std::move(value);
            }
        }
        frame_body.style.width = {100, length_unit::percent};
        frame_body.style.height = {100, length_unit::percent};
        frame_body.style.background_rgba = 0x131722FFU;
        apply_css_rules(frame_body);
        retire_provisional_frame_document(frame);
        document.append_child(frame, frame_body);
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        auto& parsed_root = document.create_node(
            dom_node_kind::internal, "#iframe-parser-document");
        parsed_root.visible = false;
        const auto parsed_frame = parse_html_document(document, parsed_root, html);
        html_parse_duration_ns += parsed_frame.duration_ns;
        html_parse_error_count += parsed_frame.parse_error_count;
        html_parse_callback_count += parsed_frame.callback_count;
        html_parse_element_count += parsed_frame.element_count;
        html_parse_text_append_count += parsed_frame.text_append_count;
        html_parse_comment_count += parsed_frame.comment_count;
        html_parse_doctype_count += parsed_frame.doctype_count;
        html_parse_rust_allocation_count += parsed_frame.rust_allocation_count;
        html_parse_rust_peak_bytes = std::max(
            html_parse_rust_peak_bytes, parsed_frame.rust_peak_bytes);
        html_parse_rust_retained_bytes += parsed_frame.rust_retained_bytes;
        if (!parsed_frame) {
            frame_last_error_value = parsed_frame.error.empty()
                ? "html5ever iframe parsing failed"
                : parsed_frame.error;
            ++frame_script_error_count;
            activate_css_cascade(context.Get(isolate));
            return frame_hydration_result::failed;
        }
        dom_node* parsed_html = nullptr;
        dom_node* parsed_body = nullptr;
        for (auto* child : parsed_root.children) {
            if (child != nullptr && child->tag == "html") parsed_html = child;
        }
        if (parsed_html != nullptr) {
            for (auto* child : parsed_html->children) {
                if (child != nullptr && child->tag == "body") parsed_body = child;
            }
        }
        if (parsed_body == nullptr) {
            frame_last_error_value = "html5ever iframe parse produced no body";
            ++frame_script_error_count;
            activate_css_cascade(context.Get(isolate));
            return frame_hydration_result::failed;
        }
        auto parsed_children = parsed_body->children;
        for (auto* child : parsed_children) {
            if (child == nullptr || child->tag == "script" || child->tag == "style") continue;
            document.parser_append_child(frame_body, *child);
        }
        activate_html5ever_subtree(frame_body, false);
        document.erase_detached_subtree(parsed_root);
#endif
        auto has_authored_loading_indicator = false;
#if defined(WEBSCENE_NATIVE_ENGINE_HTML5EVER)
        const auto find_loading_indicator = [&](const auto& self, const dom_node& node) -> bool {
            if (node.kind == dom_node_kind::element
                && (node.id_attribute == "loading-indicator"
                    || has_class(node, "loading-indicator"))) {
                return true;
            }
            return std::any_of(
                node.children.begin(),
                node.children.end(),
                [&](const auto* child) {
                    return child != nullptr && self(self, *child);
                });
        };
        has_authored_loading_indicator = find_loading_indicator(
            find_loading_indicator, frame_body);
#endif
        // The legacy loader did not retain authored frame markup, so it needed
        // a synthetic loading box for frame startup. html5ever retains that
        // markup; appending another box duplicates real pages that already
        // provide `.loading-indicator` and can leave an unstyled viewport cover.
        if (!has_authored_loading_indicator) {
            auto& loading = document.create_element("div");
            loading.id_attribute = "loading-indicator";
            loading.class_name = "loading-indicator";
            loading.attributes["id"] = loading.id_attribute;
            loading.attributes["class"] = loading.class_name;
            apply_css_rules(loading);
            document.append_child(frame_body, loading);
        }
        append_script_elements(frame_body, scripts);

        auto frame_global_template = v8::ObjectTemplate::New(isolate);
        frame_global_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
            get_window_named_property,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            {},
            v8::PropertyHandlerFlags::kNonMasking));
        auto local_frame_context = v8::Context::New(isolate, nullptr, frame_global_template);
        local_frame_context->SetSecurityToken(context.Get(isolate)->GetSecurityToken());
        local_frame_context->SetAlignedPointerInEmbedderData(
            runtime_context_embedder_slot,
            this,
            v8::kEmbedderDataTypeTagDefault);
        local_frame_context->SetAlignedPointerInEmbedderData(
            1,
            &frame_body,
            v8::kEmbedderDataTypeTagDefault);
        frame_context.Reset(isolate, local_frame_context);
        install_frame_globals(local_frame_context, frame, frame_body);
#if defined(WEBSCENE_NATIVE_ENGINE_WITH_V8_INSPECTOR)
        register_inspector_context(
            local_frame_context,
            "WebScene frame " + std::to_string(frame.id));
#endif
        if (!execute_document_start_scripts(local_frame_context, true)) {
            activate_css_cascade(context.Get(isolate));
            return frame_hydration_result::failed;
        }
        frame_hydration_state hydration;
        hydration.body = &frame_body;
        hydration.scripts = std::move(scripts);
        hydration.body_opening = std::move(body_opening);
        hydration.base_address = frame_base_address;
        hydration.context.Reset(isolate, local_frame_context);
        auto [state, inserted] = frame_hydration_states.insert_or_assign(
            frame.id,
            std::move(hydration));
        static_cast<void>(inserted);
#if defined(WEBSCENE_NATIVE_ENGINE_CERTIFICATION)
        const auto cooperative = cooperative_iframe_hydration;
#else
        constexpr auto cooperative = true;
#endif
        const auto result = continue_frame_hydration(
            frame,
            state->second,
            cooperative);
        if (result != frame_hydration_result::pending) {
            frame_hydration_states.erase(state);
        }
        return result;
    }

    bool hydrate_initial_document_start_frames()
    {
        while (!pending_frame_hydrations.empty()) {
            auto* frame = pending_frame_hydrations.front();
            pending_frame_hydrations.erase(pending_frame_hydrations.begin());
            if (frame == nullptr) continue;
            auto result = hydrate_frame(*frame);
            while (result == frame_hydration_result::pending) {
                result = hydrate_frame(*frame);
            }
            if (result == frame_hydration_result::complete) continue;
            frame->attributes["data-webscene-frame-error"] =
                frame_last_error_value;
            last_error = frame_last_error_value;
            return false;
        }
        return true;
    }
