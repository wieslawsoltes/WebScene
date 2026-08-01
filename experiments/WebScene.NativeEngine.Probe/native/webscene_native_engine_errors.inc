    void set_last_error(std::string value)
    {
        std::lock_guard lock(error_mutex_);
        last_error_ = std::move(value);
    }

    void record_input_dispatch_failure(
        const webscene_input_event& event,
        const std::string& error)
    {
        set_last_error(error);
        auto payload = std::to_string(event.sequence)
            + "\n" + std::to_string(event.kind)
            + "\n" + error;
        std::lock_guard lock(input_dispatch_failure_mutex_);
        input_dispatch_failures_.push_back(std::move(payload));
    }
