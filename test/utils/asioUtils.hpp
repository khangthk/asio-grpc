// Copyright 2021 Dennis Hezel
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

#ifndef AGRPC_UTILS_ASIOUTILS_HPP
#define AGRPC_UTILS_ASIOUTILS_HPP

#include <boost/asio/spawn.hpp>

#include <type_traits>
#include <version>

#ifdef BOOST_ASIO_HAS_CO_AWAIT
#include <boost/asio/co_spawn.hpp>
#endif

namespace agrpc::test
{
template <class Handler, class Allocator>
struct HandlerWithAssociatedAllocator
{
    using executor_type = asio::associated_executor_t<Handler>;
    using allocator_type = Allocator;

    Handler handler;
    Allocator allocator;

    HandlerWithAssociatedAllocator(Handler handler, Allocator allocator)
        : handler(std::move(handler)), allocator(allocator)
    {
    }

    decltype(auto) operator()() { return handler(); }

    [[nodiscard]] executor_type get_executor() const noexcept { return asio::get_associated_executor(handler); }

    [[nodiscard]] allocator_type get_allocator() const noexcept { return allocator; }
};

template <class Handler>
struct RpcSpawner
{
    using executor_type = asio::associated_executor_t<Handler>;
    using allocator_type = asio::associated_allocator_t<Handler>;

    Handler handler;

    explicit RpcSpawner(Handler handler) : handler(std::move(handler)) {}

    template <class RPCHandler>
    void operator()(RPCHandler&& rpc_handler, bool) &&
    {
        auto executor = this->get_executor();
        asio::spawn(std::move(executor),
                    [handler = std::move(handler), rpc_handler = std::move(rpc_handler)](auto&& yield_context) mutable
                    {
                        std::move(rpc_handler)(std::move(handler), std::move(yield_context));
                    });
    }

    [[nodiscard]] executor_type get_executor() const noexcept { return asio::get_associated_executor(handler); }

    [[nodiscard]] allocator_type get_allocator() const noexcept { return asio::get_associated_allocator(handler); }
};

#ifdef BOOST_ASIO_HAS_CO_AWAIT
template <class Executor, class Function>
auto co_spawn(Executor&& executor, Function function)
{
    return boost::asio::co_spawn(std::forward<Executor>(executor), std::move(function),
                                 [](std::exception_ptr ep, auto&&...)
                                 {
                                     if (ep)
                                     {
                                         std::rethrow_exception(ep);
                                     }
                                 });
}
#endif
}  // namespace agrpc::test

#endif  // AGRPC_UTILS_ASIOUTILS_HPP
