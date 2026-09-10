# ANGLE service loss reporting

`graphics_service::with_angle_context` throws `angle_context_lost` when activation detects a reset or when a normally returning command has lost its context. The native owner keeps loss sticky. Further commands reject before their callback runs. Independent contexts remain executable, and ordinary scope unwinding restores the previous context.

Native command callbacks are `noexcept`: a caller that submits context operations must translate `angle_context_lost` into its own completion/cancellation result. It must not catch all exceptions and silently treat invalid handles or programming errors as context loss. This service does not dispatch DOM events or recreate a browser context.

The hardware test queues four commands in order: inject loss, use the lost context, use an independent context, use the lost context again. It verifies typed loss for the first command, no callback entry for both later lost-context commands, successful independent execution, FIFO outcome order, and zero resources after teardown. ES2/ES3 owner tests separately cover nested scope restoration and texture isolation. See `result.json` and compressed build/test logs for the exact commands and scope limits.
