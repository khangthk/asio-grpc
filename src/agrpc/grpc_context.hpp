// Copyright 2022 Dennis Hezel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AGRPC_AGRPC_GRPC_CONTEXT_HPP
#define AGRPC_AGRPC_GRPC_CONTEXT_HPP

#include <agrpc/detail/asio_forward.hpp>
#include <agrpc/detail/atomic_intrusive_queue.hpp>
#include <agrpc/detail/config.hpp>
#include <agrpc/detail/forward.hpp>
#include <agrpc/detail/grpc_context.hpp>
#include <agrpc/detail/grpc_context_implementation.hpp>
#include <agrpc/detail/grpc_executor_options.hpp>
#include <agrpc/detail/intrusive_list.hpp>
#include <agrpc/detail/intrusive_queue.hpp>
#include <agrpc/detail/memory_resource.hpp>
#include <agrpc/detail/notify_when_done.hpp>
#include <agrpc/detail/operation_base.hpp>
#include <grpcpp/alarm.h>
#include <grpcpp/completion_queue.h>

#include <atomic>
#include <memory>

AGRPC_NAMESPACE_BEGIN()

/**
 * @brief Execution context based on `grpc::CompletionQueue`
 *
 * Satisfies the
 * [ExecutionContext](https://www.boost.org/doc/libs/1_79_0/doc/html/boost_asio/reference/ExecutionContext.html)
 * requirements and can therefore be used in all places where Asio expects an `ExecutionContext`.
 *
 * Performance recommendation: Use one GrpcContext per thread.
 */
class GrpcContext
#if defined(AGRPC_STANDALONE_ASIO) || defined(AGRPC_BOOST_ASIO)
    : public asio::execution_context
#endif
{
  public:
    /**
     * @brief The associated executor type
     */
    using executor_type = agrpc::BasicGrpcExecutor<>;

    /**
     * @brief The associated allocator type
     */
    using allocator_type = detail::GrpcContextLocalAllocator;

    /**
     * @brief Construct a GrpcContext for gRPC clients
     *
     * @since 2.4.0
     */
    GrpcContext() = default;

    template <class = void>
    [[deprecated("For gRPC clients use the default constructor")]] explicit GrpcContext(
        std::unique_ptr<grpc::CompletionQueue>&& completion_queue);

    /**
     * @brief Construct a GrpcContext for gRPC servers
     *
     * The resulting GrpcContext can also be used for clients.
     *
     * Example:
     *
     * @snippet server.cpp create-grpc_context-server-side
     */
    explicit GrpcContext(std::unique_ptr<grpc::ServerCompletionQueue>&& completion_queue);

    /**
     * @brief Destruct the GrpcContext
     *
     * Calls Shutdown() on the `grpc::CompletionQueue` and drains it. Pending completion handlers will not be
     * invoked.
     *
     * @attention Make sure to destruct the GrpcContext before destructing the *grpc::Server*.
     */
    ~GrpcContext();

    /**
     * @brief Run ready completion handlers and `grpc::CompletionQueue`
     *
     * Runs the main event loop logic until the GrpcContext runs out of work or is stopped. The GrpcContext will be
     * brought into the ready state when this function is invoked. Upon return, the GrpcContext will be in the stopped
     * state.
     *
     * @attention Only one thread may call run*()/poll*() at a time.
     *
     * Thread-safe with regards to other functions except run*(), poll*() and the destructor.
     *
     * @return True if at least one operation has been processed.
     */
    bool run();

    /**
     * @brief Run ready completion handlers and `grpc::CompletionQueue` until deadline
     *
     * Runs the main event loop logic until the GrpcContext runs out of work, is stopped or the specified deadline has
     * been reached. The GrpcContext will be brought into the ready state when this function is invoked.
     *
     * @attention Only one thread may call run*()/poll*() at a time.
     *
     * Thread-safe with regards to other functions except run*(), poll*() and the destructor.
     *
     * @tparam Deadline A type that is compatible with `grpc::TimePoint<Deadline>`.
     *
     * @return True if at least one operation has been processed.
     *
     * @since 2.0.0
     */
    template <class Deadline>
    bool run_until(const Deadline& deadline)
    {
        grpc::TimePoint<Deadline> deadline_time_point(deadline);
        return this->run_until_impl(deadline_time_point.raw_time());
    }

    /**
     * @brief Run ready completion handlers and `grpc::CompletionQueue` while a condition holds
     *
     * Runs the main event loop logic until the GrpcContext runs out of work, is stopped or the specified condition
     * returns false. The GrpcContext will be brought into the ready state when this function is invoked.
     *
     * @attention Only one thread may call run*()/poll*() at a time.
     *
     * Thread-safe with regards to other functions except run*(), poll*() and the destructor.
     *
     * @tparam Condition A callable that returns false when the GrpcContext should stop.
     *
     * @return True if at least one operation has been processed.
     *
     * @since 2.2.0
     */
    template <class Condition>
    bool run_while(Condition&& condition);

    /**
     * @brief Run the `grpc::CompletionQueue`
     *
     * Runs the main event loop logic until the GrpcContext runs out of work or is stopped. Only events from the
     * `grpc::CompletionQueue` will be handled. That means that completion handler that were e.g. created using
     * `asio::post(grpc_context, ...)` will not be processed. The GrpcContext will be brought into the ready state when
     * this function is invoked. Upon return, the GrpcContext will be in the stopped state.
     *
     * @attention Only one thread may call run*()/poll*() at a time.
     *
     * Thread-safe with regards to other functions except run*(), poll*() and the destructor.
     *
     * @return True if at least one event has been processed.
     */
    bool run_completion_queue();

    /**
     * @brief Poll ready completion handlers and `grpc::CompletionQueue`
     *
     * Processes all ready completion handlers and ready events of the `grpc::CompletionQueue`. The GrpcContext will be
     * brought into the ready state when this function is invoked.
     *
     * @attention Only one thread may call run*()/poll*() at a time.
     *
     * Thread-safe with regards to other functions except run*(), poll*() and the destructor.
     *
     * @return True if at least one operation has been processed.
     */
    bool poll();

    /**
     * @brief Poll the `grpc::CompletionQueue`
     *
     * Processes only ready events of the `grpc::CompletionQueue`. That means that completion handler that were e.g.
     * created using `asio::post(grpc_context, ...)` will not be processed. The GrpcContext will be brought into the
     * ready state when this function is invoked.
     *
     * @attention Only one thread may call run*()/poll*() at a time.
     *
     * Thread-safe with regards to other functions except run*(), poll*() and the destructor.
     *
     * @return True if at least one operation has been processed.
     */
    bool poll_completion_queue();

    /**
     * @brief Signal the GrpcContext to stop
     *
     * Causes a call to run() to return as soon as possible.
     *
     * Thread-safe with regards to other functions except the destructor.
     */
    void stop();

    /**
     * @brief Bring a stopped GrpcContext back into the ready state
     *
     * When a call to run() or stop() returns, the GrpcContext will be in a stopped state. This function brings the
     * GrpcContext back into the ready state.
     *
     * Thread-safe with regards to other functions except the destructor.
     */
    void reset() noexcept;

    /**
     * @brief Is the GrpcContext in the stopped state?
     *
     * Thread-safe
     */
    [[nodiscard]] bool is_stopped() const noexcept;

    /**
     * @brief Get the associated executor
     *
     * Thread-safe
     */
    [[nodiscard]] executor_type get_executor() noexcept;

    /**
     * @brief Get the associated scheduler
     *
     * Thread-safe
     */
    [[nodiscard]] executor_type get_scheduler() noexcept;

    /**
     * @brief Get the associated allocator
     *
     * @attention The returned allocator may only be used for allocations within the same thread that calls run().
     *
     * Thread-safe
     */
    [[nodiscard]] allocator_type get_allocator() noexcept;

    /**
     * @brief Signal that work has started
     *
     * The GrpcContext maintains an internal counter on how many operations have been started. Once that counter reaches
     * zero it will go into the stopped state. Every call to work_started() should be matched to a call of
     * work_finished().
     *
     * Thread-safe
     */
    void work_started() noexcept;

    /**
     * @brief Signal that work has finished
     *
     * Thread-safe
     */
    void work_finished() noexcept;

    /**
     * @brief Get the underlying `grpc::CompletionQueue`
     *
     * Do not use any functions of the returned CompletionQueue that might interfere with the GrpcContext, like Next().
     *
     * Do not delete the returned pointer.
     *
     * Thread-safe, never nullptr
     */
    [[nodiscard]] grpc::CompletionQueue* get_completion_queue() noexcept;

    /**
     * @brief Get the underlying `grpc::CompletionQueue`
     *
     * Do not use any functions of the returned CompletionQueue that might interfere with the GrpcContext, like Next().
     *
     * Do not delete the returned pointer.
     *
     * @attention Only valid if the GrpcContext has been constructed with a ServerCompletionQueue:
     * @snippet server.cpp create-grpc_context-server-side
     *
     * Thread-safe, never nullptr
     */
    [[nodiscard]] grpc::ServerCompletionQueue* get_server_completion_queue() noexcept;

  private:
    using RemoteWorkQueue = detail::AtomicIntrusiveQueue<detail::QueueableOperationBase>;
    using LocalWorkQueue = detail::IntrusiveQueue<detail::QueueableOperationBase>;
    using NotifyWhenDoneList = detail::IntrusiveList<detail::NotfiyWhenDoneSenderImplementation>;

    friend detail::GrpcContextImplementation;

    bool run_until_impl(::gpr_timespec deadline);

    grpc::Alarm work_alarm;
    std::atomic_long outstanding_work{};
    std::atomic_bool stopped{false};
    std::atomic_bool shutdown{false};
    bool check_remote_work{false};
    std::unique_ptr<grpc::CompletionQueue> completion_queue{std::make_unique<grpc::CompletionQueue>()};
    detail::GrpcContextLocalMemoryResource local_resource{detail::new_delete_resource()};
    LocalWorkQueue local_work_queue;
    NotifyWhenDoneList notify_when_done_list;
    RemoteWorkQueue remote_work_queue{false};
};

AGRPC_NAMESPACE_END

template <class Alloc>
struct agrpc::detail::container::uses_allocator<agrpc::GrpcContext, Alloc> : std::false_type
{
};

#endif  // AGRPC_AGRPC_GRPC_CONTEXT_HPP
