export module vvk:completion;

import rstd;
import :ffi.vulkan;
import :dispatch;

using namespace rstd::prelude;

export namespace vvk
{

using HandleIdentity = rstd::uintptr_t;

template<typename Handle>
HandleIdentity OpaqueHandleIdentity(Handle handle) noexcept {
    if constexpr (rstd::mtp::is_ptr<Handle>) {
        return reinterpret_cast<HandleIdentity>(handle);
    } else {
        return static_cast<HandleIdentity>(handle);
    }
}

template<typename Handle>
Handle HandleFromIdentity(HandleIdentity identity) noexcept {
    if constexpr (rstd::mtp::is_ptr<Handle>) {
        return reinterpret_cast<Handle>(identity);
    } else {
        return static_cast<Handle>(identity);
    }
}

struct QueueDomain {
    HandleIdentity device {};
    HandleIdentity queue {};
    u64            generation {};
    u32            family { u32(VK_QUEUE_FAMILY_IGNORED) };
    u32            index {};

    bool valid() const noexcept { return device != 0 && queue != 0 && generation != u64(); }

    friend bool operator==(const QueueDomain&, const QueueDomain&) = default;
};

inline QueueDomain MakeQueueDomain(VkDevice device, VkQueue queue, u32 family, u32 index,
                                   u64 generation) noexcept {
    return QueueDomain {
        .device     = OpaqueHandleIdentity(device),
        .queue      = OpaqueHandleIdentity(queue),
        .generation = generation,
        .family     = family,
        .index      = index,
    };
}

enum class CompletionPrimitive
{
    TimelineSemaphore,
    Fence,
};

struct CompletionSource {
    QueueDomain         queue;
    CompletionPrimitive primitive { CompletionPrimitive::TimelineSemaphore };
    HandleIdentity      handle {};
    u64                 generation {};

    bool valid() const noexcept { return queue.valid() && handle != 0 && generation != u64(); }

    friend bool operator==(const CompletionSource&, const CompletionSource&) = default;
};

inline CompletionSource MakeTimelineCompletionSource(QueueDomain queue, VkSemaphore semaphore,
                                                     u64 generation) noexcept {
    return CompletionSource {
        .queue      = queue,
        .primitive  = CompletionPrimitive::TimelineSemaphore,
        .handle     = OpaqueHandleIdentity(semaphore),
        .generation = generation,
    };
}

inline CompletionSource MakeFenceCompletionSource(QueueDomain queue, VkFence fence,
                                                  u64 generation) noexcept {
    return CompletionSource {
        .queue      = queue,
        .primitive  = CompletionPrimitive::Fence,
        .handle     = OpaqueHandleIdentity(fence),
        .generation = generation,
    };
}

struct SubmissionToken {
    CompletionSource source;
    u64              value {};

    bool valid() const noexcept { return source.valid() && value != u64(); }

    friend bool operator==(const SubmissionToken&, const SubmissionToken&) = default;
};

struct TimelineSemaphoreDeviceDispatch {
    PFN_vkCreateSemaphore  create_semaphore { nullptr };
    PFN_vkDestroySemaphore destroy_semaphore { nullptr };

    static TimelineSemaphoreDeviceDispatch FromDispatch(const DeviceDispatch& dispatch) noexcept {
        if (! dispatch.capabilities.timeline_semaphore) return {};
        return TimelineSemaphoreDeviceDispatch {
            .create_semaphore  = dispatch.vkCreateSemaphore,
            .destroy_semaphore = dispatch.vkDestroySemaphore,
        };
    }

    bool valid() const noexcept {
        return create_semaphore != nullptr && destroy_semaphore != nullptr;
    }
};

class TimelineSemaphoreGeneration;

struct TimelineSemaphoreCreateResult {
    VkResult                                             api_result { VK_SUCCESS };
    Option<rstd::sync::Arc<TimelineSemaphoreGeneration>> generation;

    bool created() const noexcept { return api_result == VK_SUCCESS && generation.is_some(); }
};

class TimelineSemaphoreGeneration {
public:
    TimelineSemaphoreGeneration(const TimelineSemaphoreGeneration&)            = delete;
    TimelineSemaphoreGeneration& operator=(const TimelineSemaphoreGeneration&) = delete;

    ~TimelineSemaphoreGeneration() {
        if (m_device != VK_NULL_HANDLE && m_semaphore != VK_NULL_HANDLE &&
            m_dispatch.destroy_semaphore != nullptr) {
            m_dispatch.destroy_semaphore(m_device, m_semaphore, nullptr);
        }
    }

    static TimelineSemaphoreCreateResult Create(VkDevice device, QueueDomain queue,
                                                u64                             source_generation,
                                                TimelineSemaphoreDeviceDispatch dispatch,
                                                u64 initial_value = u64()) {
        if (device == VK_NULL_HANDLE || ! queue.valid() ||
            queue.device != OpaqueHandleIdentity(device) || source_generation == u64() ||
            ! dispatch.valid()) {
            return { .api_result = VK_ERROR_INITIALIZATION_FAILED };
        }

        VkSemaphoreTypeCreateInfo timeline_type {
            .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext         = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue  = initial_value.to_primitive(),
        };
        VkSemaphoreCreateInfo create_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timeline_type,
            .flags = 0,
        };
        VkSemaphore semaphore = VK_NULL_HANDLE;
        const auto  result = dispatch.create_semaphore(device, &create_info, nullptr, &semaphore);
        if (result != VK_SUCCESS) return { .api_result = result };

        auto source = MakeTimelineCompletionSource(queue, semaphore, source_generation);
        if (! source.valid()) {
            dispatch.destroy_semaphore(device, semaphore, nullptr);
            return { .api_result = VK_ERROR_INITIALIZATION_FAILED };
        }

        return TimelineSemaphoreCreateResult {
            .api_result = VK_SUCCESS,
            .generation = Some(rstd::sync::Arc<TimelineSemaphoreGeneration>::make(
                device, semaphore, source, rstd::move(dispatch))),
        };
    }

    VkDevice                device() const noexcept { return m_device; }
    VkSemaphore             handle() const noexcept { return m_semaphore; }
    const CompletionSource& source() const noexcept { return m_source; }

public:
    TimelineSemaphoreGeneration(VkDevice device, VkSemaphore semaphore, CompletionSource source,
                                TimelineSemaphoreDeviceDispatch dispatch) noexcept
        : m_device(device),
          m_semaphore(semaphore),
          m_source(rstd::move(source)),
          m_dispatch(rstd::move(dispatch)) {}

    VkDevice                        m_device { VK_NULL_HANDLE };
    VkSemaphore                     m_semaphore { VK_NULL_HANDLE };
    CompletionSource                m_source;
    TimelineSemaphoreDeviceDispatch m_dispatch;
};

struct TimelineExecutionDependency {
    Option<rstd::sync::Arc<TimelineSemaphoreGeneration>> timeline;
    SubmissionToken                                      completion;

    bool valid() const noexcept {
        return timeline.is_some() && completion.valid() &&
               completion.source == (*timeline)->source();
    }

    VkSemaphore wait_semaphore() const noexcept {
        return valid() ? (*timeline)->handle() : VK_NULL_HANDLE;
    }
    u64         wait_value() const noexcept { return valid() ? completion.value : u64(); }
    QueueDomain signal_queue() const noexcept {
        return valid() ? completion.source.queue : QueueDomain {};
    }

    TimelineExecutionDependency clone() const {
        return TimelineExecutionDependency {
            .timeline   = timeline.is_some() ? Some((*timeline).clone()) : None(),
            .completion = completion,
        };
    }
};

enum class ExecutionDependencyRelation
{
    SameQueueOrdered,
    TimelineWaitRequired,
    Invalid,
};

inline bool SameQueueExecutionDomain(const QueueDomain& lhs, const QueueDomain& rhs) noexcept {
    return lhs.valid() && rhs.valid() && lhs.device == rhs.device && lhs.queue == rhs.queue &&
           lhs.family == rhs.family && lhs.index == rhs.index;
}

inline ExecutionDependencyRelation
ClassifyExecutionDependency(const TimelineExecutionDependency& dependency,
                            const QueueDomain&                 consumer) noexcept {
    if (! dependency.valid() || ! consumer.valid() ||
        dependency.signal_queue().device != consumer.device) {
        return ExecutionDependencyRelation::Invalid;
    }
    if (SameQueueExecutionDomain(dependency.signal_queue(), consumer)) {
        return ExecutionDependencyRelation::SameQueueOrdered;
    }
    return ExecutionDependencyRelation::TimelineWaitRequired;
}

enum class CompletionOrder
{
    Earlier,
    Equivalent,
    Later,
    Unordered,
};

inline CompletionOrder CompareCompletion(const SubmissionToken& lhs,
                                         const SubmissionToken& rhs) noexcept {
    if (! lhs.valid() || ! rhs.valid() || lhs.source != rhs.source) {
        return CompletionOrder::Unordered;
    }
    if (lhs.value < rhs.value) return CompletionOrder::Earlier;
    if (lhs.value > rhs.value) return CompletionOrder::Later;
    return CompletionOrder::Equivalent;
}

inline bool CompletionCovers(const SubmissionToken& completed,
                             const SubmissionToken& required) noexcept {
    const auto order = CompareCompletion(completed, required);
    return order == CompletionOrder::Equivalent || order == CompletionOrder::Later;
}

enum class CompletionObservationStatus
{
    Observed,
    Pending,
    Invalid,
    DeviceLost,
    ApiError,
};

struct CompletionObservation {
    CompletionObservationStatus status { CompletionObservationStatus::Invalid };
    VkResult                    api_result { VK_SUCCESS };
    SubmissionToken             completed;

    bool observed() const noexcept { return status == CompletionObservationStatus::Observed; }
};

class CompletedWatermarks {
public:
    bool Observe(const SubmissionToken& completed) {
        if (! completed.valid()) return false;
        for (auto& current : m_completed) {
            if (current.source != completed.source) continue;
            if (completed.value <= current.value) return false;
            current = completed;
            return true;
        }
        auto value = completed;
        m_completed.push(rstd::move(value));
        return true;
    }

    bool Covers(const SubmissionToken& required) const noexcept {
        if (! required.valid()) return false;
        for (const auto& current : m_completed) {
            if (current.source == required.source) return CompletionCovers(current, required);
        }
        return false;
    }

    Option<SubmissionToken> Get(const CompletionSource& source) const {
        for (const auto& current : m_completed) {
            if (current.source == source) {
                auto value = current;
                return Some(rstd::move(value));
            }
        }
        return None();
    }

    usize size() const noexcept { return m_completed.len(); }
    bool  empty() const noexcept { return m_completed.is_empty(); }
    void  clear() { m_completed.clear(); }

private:
    rstd::vec::Vec<SubmissionToken> m_completed;
};

class TimelineCompletionObserver {
public:
    using QueryCallback = VkResult (*)(VkDevice, VkSemaphore, rstd::uint64_t*);
    using WaitCallback  = VkResult (*)(VkDevice, VkSemaphore, rstd::uint64_t, rstd::uint64_t);

    TimelineCompletionObserver(VkDevice device, QueryCallback get_counter,
                               WaitCallback wait) noexcept
        : m_device(device), m_get_counter(get_counter), m_wait(wait) {}

    static TimelineCompletionObserver AdoptVulkan(VkDevice                          device,
                                                  PFN_vkGetSemaphoreCounterValueKHR get_counter,
                                                  PFN_vkWaitSemaphoresKHR           wait) {
        return TimelineCompletionObserver(device, get_counter, wait);
    }

    TimelineCompletionObserver(VkDevice device, PFN_vkGetSemaphoreCounterValueKHR get_counter,
                               PFN_vkWaitSemaphoresKHR wait) noexcept
        : m_device(device), m_get_counter(get_counter), m_vk_wait(wait) {}

    bool valid() const noexcept {
        return m_device != VK_NULL_HANDLE && m_get_counter != nullptr &&
               (m_wait != nullptr || m_vk_wait != nullptr);
    }

    CompletionObservation Poll(const CompletionSource& source) const noexcept {
        if (! Matches(source) || m_get_counter == nullptr) return InvalidObservation();

        rstd::uint64_t value = 0;
        const auto     result =
            m_get_counter(m_device, HandleFromIdentity<VkSemaphore>(source.handle), &value);
        if (result == VK_ERROR_DEVICE_LOST) return DeviceLostObservation(result);
        if (result != VK_SUCCESS) return ApiErrorObservation(result);
        return CompletionObservation {
            .status     = CompletionObservationStatus::Observed,
            .api_result = result,
            .completed  = value == 0 ? SubmissionToken {} : SubmissionToken { source, u64(value) },
        };
    }

    CompletionObservation Wait(const SubmissionToken& required, u64 timeout_ns) const noexcept {
        if (! required.valid() || ! Matches(required.source) ||
            (m_wait == nullptr && m_vk_wait == nullptr)) {
            return InvalidObservation();
        }

        const auto semaphore = HandleFromIdentity<VkSemaphore>(required.source.handle);
        VkResult   result    = VK_ERROR_INITIALIZATION_FAILED;
        if (m_wait != nullptr) {
            result = m_wait(
                m_device, semaphore, required.value.to_primitive(), timeout_ns.to_primitive());
        } else {
            const auto                   required_value = required.value.to_primitive();
            const VkSemaphoreWaitInfoKHR wait_info {
                .sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR,
                .pNext          = nullptr,
                .flags          = 0,
                .semaphoreCount = 1,
                .pSemaphores    = &semaphore,
                .pValues        = &required_value,
            };
            result = m_vk_wait(m_device, &wait_info, timeout_ns.to_primitive());
        }
        if (result == VK_TIMEOUT) {
            return CompletionObservation {
                .status     = CompletionObservationStatus::Pending,
                .api_result = result,
            };
        }
        if (result == VK_ERROR_DEVICE_LOST) return DeviceLostObservation(result);
        if (result != VK_SUCCESS) return ApiErrorObservation(result);
        return CompletionObservation {
            .status     = CompletionObservationStatus::Observed,
            .api_result = result,
            .completed  = required,
        };
    }

private:
    bool Matches(const CompletionSource& source) const noexcept {
        return valid() && source.valid() &&
               source.primitive == CompletionPrimitive::TimelineSemaphore &&
               source.queue.device == OpaqueHandleIdentity(m_device);
    }

    static CompletionObservation InvalidObservation() noexcept { return {}; }

    static CompletionObservation DeviceLostObservation(VkResult result) noexcept {
        return CompletionObservation {
            .status     = CompletionObservationStatus::DeviceLost,
            .api_result = result,
        };
    }

    static CompletionObservation ApiErrorObservation(VkResult result) noexcept {
        return CompletionObservation {
            .status     = CompletionObservationStatus::ApiError,
            .api_result = result,
        };
    }

    VkDevice                m_device { VK_NULL_HANDLE };
    QueryCallback           m_get_counter { nullptr };
    WaitCallback            m_wait { nullptr };
    PFN_vkWaitSemaphoresKHR m_vk_wait { nullptr };
};

enum class SubmissionOutcomeStatus
{
    Submitted,
    RecoverableFailure,
    DeviceLost,
    Invalid,
};

struct SubmissionOutcome {
    SubmissionOutcomeStatus status { SubmissionOutcomeStatus::Invalid };
    VkResult                api_result { VK_SUCCESS };
    SubmissionToken         token;

    bool submitted() const noexcept { return status == SubmissionOutcomeStatus::Submitted; }
};

inline SubmissionOutcome ClassifySubmission(VkResult result, const CompletionSource& source,
                                            u64 value) noexcept {
    if (result == VK_ERROR_DEVICE_LOST) {
        return SubmissionOutcome {
            .status     = SubmissionOutcomeStatus::DeviceLost,
            .api_result = result,
        };
    }
    if (result != VK_SUCCESS) {
        return SubmissionOutcome {
            .status     = SubmissionOutcomeStatus::RecoverableFailure,
            .api_result = result,
        };
    }
    SubmissionToken token { source, value };
    if (! token.valid()) return {};
    return SubmissionOutcome {
        .status     = SubmissionOutcomeStatus::Submitted,
        .api_result = result,
        .token      = token,
    };
}

} // namespace vvk
