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

#ifndef AGRPC_DETAIL_HIGH_LEVEL_CLIENT_SENDER_HPP
#define AGRPC_DETAIL_HIGH_LEVEL_CLIENT_SENDER_HPP

#include <agrpc/detail/config.hpp>
#include <agrpc/detail/grpc_sender.hpp>
#include <agrpc/detail/rpc_type.hpp>
#include <agrpc/detail/utility.hpp>
#include <agrpc/grpc_context.hpp>
#include <agrpc/grpc_executor.hpp>
#include <grpcpp/generic/generic_stub.h>

AGRPC_NAMESPACE_BEGIN()

/**
 * @brief (experimental) Primary RPC template
 *
 * This is the main entrypoint into the high-level client API.
 *
 * @see
 * @c agrpc::RPC<PrepareAsync,Executor,agrpc::RPCType::CLIENT_UNARY> <br>
 * @c agrpc::RPC<agrpc::CLIENT_GENERIC_UNARY_RPC,Executor,agrpc::RPCType::CLIENT_UNARY> <br>
 * @c agrpc::RPC<PrepareAsync,Executor,agrpc::RPCType::CLIENT_CLIENT_STREAMING> <br>
 * @c agrpc::RPC<PrepareAsync,Executor,agrpc::RPCType::CLIENT_SERVER_STREAMING> <br>
 * @c agrpc::RPC<PrepareAsync,Executor,agrpc::RPCType::CLIENT_BIDIRECTIONAL_STREAMING> <br>
 * @c agrpc::RPC<agrpc::CLIENT_GENERIC_STREAMING_RPC,Executor,agrpc::RPCType::CLIENT_BIDIRECTIONAL_STREAMING> <br>
 *
 * @since 2.1.0
 */
template <auto PrepareAsync, class Executor = agrpc::GrpcExecutor, agrpc::RPCType = detail::RPC_TYPE<PrepareAsync>>
class RPC;

namespace detail
{
struct RPCAccess
{
    template <class RPC>
    static void client_initiate_finish(RPC& rpc, void* tag)
    {
        rpc.set_finished();
        rpc.grpc_context().work_started();
        rpc.responder().Finish(&rpc.status(), tag);
    }
};

struct ClientContextCancellationFunction
{
    grpc::ClientContext& client_context;

    void operator()() const { client_context.TryCancel(); }

#ifdef AGRPC_ASIO_HAS_CANCELLATION_SLOT
    void operator()(asio::cancellation_type type) const
    {
        if (static_cast<bool>(type & (asio::cancellation_type::terminal | asio::cancellation_type::partial)))
        {
            this->operator()();
        }
    }
#endif
};

template <auto PrepareAsync, class Executor>
struct ClientUnaryRequestSenderImplementation;

template <class Stub, class Request, class Response, template <class> class Responder,
          detail::ClientUnaryRequest<Stub, Request, Responder<Response>> PrepareAsync, class Executor>
struct ClientUnaryRequestSenderImplementation<PrepareAsync, Executor> : detail::GrpcSenderImplementationBase
{
    using RPC = agrpc::RPC<PrepareAsync, Executor, agrpc::RPCType::CLIENT_UNARY>;
    using Signature = void(grpc::Status);
    using StopFunction = ClientContextCancellationFunction;

    struct Initiation
    {
        grpc::ClientContext& client_context;
        Response& response;
    };

    ClientUnaryRequestSenderImplementation(agrpc::GrpcContext& grpc_context, Stub& stub,
                                           grpc::ClientContext& client_context, const Request& req)
        : responder((stub.*PrepareAsync)(&client_context, req, grpc_context.get_completion_queue()))
    {
    }

    auto& stop_function_arg(const Initiation& initiation) noexcept { return initiation.client_context; }

    void initiate(const agrpc::GrpcContext&, const Initiation& initiation, detail::OperationBase* operation) noexcept
    {
        responder->StartCall();
        responder->Finish(&initiation.response, &status, operation);
    }

    template <class OnDone>
    void done(OnDone on_done, bool)
    {
        on_done(static_cast<grpc::Status&&>(status));
    }

    grpc::Status status;
    std::unique_ptr<Responder<Response>> responder;
};

template <class Executor>
struct GenericClientUnaryRequestSenderImplementation : detail::GrpcSenderImplementationBase
{
    using RPC = agrpc::RPC<detail::GenericRPCType::CLIENT_UNARY, Executor, agrpc::RPCType::CLIENT_UNARY>;
    using Signature = void(grpc::Status);

    struct Initiation
    {
        grpc::ByteBuffer& response;
    };

    GenericClientUnaryRequestSenderImplementation(agrpc::GrpcContext& grpc_context, const std::string& method,
                                                  grpc::GenericStub& stub, grpc::ClientContext& client_context,
                                                  const grpc::ByteBuffer& req)
        : responder(stub.PrepareUnaryCall(&client_context, method, req, grpc_context.get_completion_queue()))
    {
    }

    void initiate(const agrpc::GrpcContext&, const Initiation& initiation, detail::OperationBase* operation) noexcept
    {
        responder->StartCall();
        responder->Finish(&initiation.response, &status, operation);
    }

    template <class OnDone>
    void done(OnDone on_done, bool)
    {
        on_done(static_cast<grpc::Status&&>(status));
    }

    grpc::Status status;
    std::unique_ptr<grpc::GenericClientAsyncResponseReader> responder;
};

template <auto PrepareAsync, class Executor>
struct ClientClientStreamingRequestSenderImplementation;

template <class Stub, class Request, class Response, template <class> class Responder,
          detail::PrepareAsyncClientClientStreamingRequest<Stub, Responder<Request>, Response> PrepareAsync,
          class Executor>
struct ClientClientStreamingRequestSenderImplementation<PrepareAsync, Executor> : detail::GrpcSenderImplementationBase
{
    using RPC = agrpc::RPC<PrepareAsync, Executor, agrpc::RPCType::CLIENT_CLIENT_STREAMING>;
    using Signature = void(RPC);
    using Initiation = detail::Empty;

    ClientClientStreamingRequestSenderImplementation(agrpc::GrpcContext& grpc_context, Stub& stub,
                                                     grpc::ClientContext& client_context, Response& response)
        : rpc(grpc_context.get_executor(), client_context,
              (stub.*PrepareAsync)(&client_context, &response, grpc_context.get_completion_queue()))
    {
    }

    void initiate(const agrpc::GrpcContext&, const Initiation&, void* self) noexcept
    {
        rpc.responder().StartCall(self);
    }

    template <class OnDone>
    void done(OnDone on_done, bool ok)
    {
        if (ok)
        {
            on_done(static_cast<RPC&&>(rpc));
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.self());
        }
    }

    RPC rpc;
};

template <auto PrepareAsync, class Executor>
struct ClientServerStreamingRequestSenderImplementation;

template <class Stub, class Request, class Response, template <class> class Responder,
          detail::PrepareAsyncClientServerStreamingRequest<Stub, Request, Responder<Response>> PrepareAsync,
          class Executor>
struct ClientServerStreamingRequestSenderImplementation<PrepareAsync, Executor> : detail::GrpcSenderImplementationBase
{
    using RPC = agrpc::RPC<PrepareAsync, Executor, agrpc::RPCType::CLIENT_SERVER_STREAMING>;
    using Signature = void(RPC);
    using Initiation = detail::Empty;

    ClientServerStreamingRequestSenderImplementation(agrpc::GrpcContext& grpc_context, Stub& stub,
                                                     grpc::ClientContext& client_context, const Request& req)
        : rpc(grpc_context.get_executor(), client_context,
              (stub.*PrepareAsync)(&client_context, req, grpc_context.get_completion_queue()))
    {
    }

    void initiate(const agrpc::GrpcContext&, const Initiation&, void* self) noexcept
    {
        rpc.responder().StartCall(self);
    }

    template <class OnDone>
    void done(OnDone on_done, bool ok)
    {
        if (ok)
        {
            on_done(static_cast<RPC&&>(rpc));
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.self());
        }
    }

    RPC rpc;
};

template <auto PrepareAsync, class Executor>
struct ClientBidirectionalStreamingRequestSenderImplementation;

template <class Stub, class Request, class Response, template <class, class> class Responder,
          detail::PrepareAsyncClientBidirectionalStreamingRequest<Stub, Responder<Request, Response>> PrepareAsync,
          class Executor>
struct ClientBidirectionalStreamingRequestSenderImplementation<PrepareAsync, Executor>
    : detail::GrpcSenderImplementationBase
{
    using RPC = agrpc::RPC<PrepareAsync, Executor, agrpc::RPCType::CLIENT_BIDIRECTIONAL_STREAMING>;
    using Signature = void(RPC);
    using Initiation = detail::Empty;

    ClientBidirectionalStreamingRequestSenderImplementation(agrpc::GrpcContext& grpc_context, Stub& stub,
                                                            grpc::ClientContext& client_context)
        : rpc(grpc_context.get_executor(), client_context,
              (stub.*PrepareAsync)(&client_context, grpc_context.get_completion_queue()))
    {
    }

    void initiate(const agrpc::GrpcContext&, const Initiation&, void* self) noexcept
    {
        rpc.responder().StartCall(self);
    }

    template <class OnDone>
    void done(OnDone on_done, bool ok)
    {
        if (ok)
        {
            on_done(static_cast<RPC&&>(rpc));
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.self());
        }
    }

    RPC rpc;
};

template <class Executor>
struct ClientBidirectionalStreamingRequestSenderImplementation<detail::GenericRPCType::CLIENT_STREAMING, Executor>
    : detail::GrpcSenderImplementationBase
{
    using RPC =
        agrpc::RPC<detail::GenericRPCType::CLIENT_STREAMING, Executor, agrpc::RPCType::CLIENT_BIDIRECTIONAL_STREAMING>;
    using Signature = void(RPC);
    using Initiation = detail::Empty;

    ClientBidirectionalStreamingRequestSenderImplementation(agrpc::GrpcContext& grpc_context, const std::string& method,
                                                            grpc::GenericStub& stub,
                                                            grpc::ClientContext& client_context)
        : rpc(grpc_context.get_executor(), client_context,
              stub.PrepareCall(&client_context, method, grpc_context.get_completion_queue()))
    {
    }

    void initiate(const agrpc::GrpcContext&, const Initiation&, void* self) noexcept
    {
        rpc.responder().StartCall(self);
    }

    template <class OnDone>
    void done(OnDone on_done, bool ok)
    {
        if (ok)
        {
            on_done(static_cast<RPC&&>(rpc));
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.self());
        }
    }

    RPC rpc;
};

template <class RPCBase>
struct ReadInitialMetadataSenderImplementation : detail::GrpcSenderImplementationBase
{
    using Initiation = detail::Empty;

    ReadInitialMetadataSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    void initiate(const agrpc::GrpcContext&, const Initiation&, void* self) noexcept
    {
        rpc.responder().ReadInitialMetadata(self);
    }

    template <template <int> class OnDone>
    void done(OnDone<0> on_done, bool ok)
    {
        if (ok)
        {
            on_done(true);
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.template self<1>());
        }
    }

    template <template <int> class OnDone>
    static void done(OnDone<1> on_done, bool)
    {
        on_done(false);
    }

    RPCBase& rpc;
};

template <class Responder, class Executor>
struct ReadServerStreamingSenderImplementation;

template <template <class> class Responder, class Response, class Executor>
struct ReadServerStreamingSenderImplementation<Responder<Response>, Executor> : detail::GrpcSenderImplementationBase
{
    using RPCBase = detail::RPCClientServerStreamingBase<Responder<Response>, Executor>;

    struct Initiation
    {
        Response& response;
    };

    ReadServerStreamingSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    void initiate(const agrpc::GrpcContext&, const Initiation& initiation, detail::OperationBase* operation) noexcept
    {
        rpc.responder().Read(&initiation.response, operation);
    }

    template <template <int> class OnDone>
    void done(OnDone<0> on_done, bool ok)
    {
        if (ok)
        {
            on_done(true);
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.template self<1>());
        }
    }

    template <template <int> class OnDone>
    static void done(OnDone<1> on_done, bool)
    {
        on_done(false);
    }

    RPCBase& rpc;
};

template <class Responder, class Executor>
struct WriteClientStreamingSenderImplementation;

template <class Request, template <class> class Responder, class Executor>
struct WriteClientStreamingSenderImplementation<Responder<Request>, Executor> : detail::GrpcSenderImplementationBase
{
    using RPCBase = detail::RPCClientClientStreamingBase<Responder<Request>, Executor>;

    struct Initiation
    {
        const Request& req;
        grpc::WriteOptions options;
    };

    WriteClientStreamingSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    template <class Init>
    void initiate(Init init, const Initiation& initiation) noexcept
    {
        auto& [req, options] = initiation;
        if (options.is_last_message())
        {
            rpc.set_writes_done();
            rpc.responder().Write(req, options, init.template self<1>());
        }
        else
        {
            rpc.responder().Write(req, options, init.template self<0>());
        }
    }

    template <template <int> class OnDone>
    void done(OnDone<0> on_done, bool ok)
    {
        if (ok)
        {
            on_done(true);
        }
        else
        {
            detail::RPCAccess::client_initiate_finish(rpc, on_done.template self<2>());
        }
    }

    template <template <int> class OnDone>
    void done(OnDone<1> on_done, bool)
    {
        detail::RPCAccess::client_initiate_finish(rpc, on_done.template self<2>());
    }

    template <template <int> class OnDone>
    void done(OnDone<2> on_done, bool)
    {
        on_done(rpc.ok());
    }

    RPCBase& rpc;
};

template <class RPCBase>
struct ClientFinishSenderImplementation : detail::GrpcSenderImplementationBase
{
    using Initiation = detail::Empty;

    ClientFinishSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    template <class Init>
    void initiate(Init init, const Initiation&) noexcept
    {
        if (rpc.is_writes_done())
        {
            rpc.set_finished();
            rpc.responder().Finish(&rpc.status(), init.template self<1>());
        }
        else
        {
            rpc.responder().WritesDone(init.template self<0>());
        }
    }

    template <template <int> class OnDone>
    void done(OnDone<0> on_done, bool)
    {
        detail::RPCAccess::client_initiate_finish(rpc, on_done.template self<1>());
    }

    template <template <int> class OnDone>
    void done(OnDone<1> on_done, bool) const
    {
        on_done(rpc.ok());
    }

    RPCBase& rpc;
};

template <class Responder, class Executor>
struct ClientReadBidiStreamingSenderImplementation;

template <template <class, class> class Responder, class Request, class Response, class Executor>
struct ClientReadBidiStreamingSenderImplementation<Responder<Request, Response>, Executor>
    : detail::GrpcSenderImplementationBase
{
    using RPCBase = detail::RPCBidirectionalStreamingBase<Responder<Request, Response>, Executor>;

    struct Initiation
    {
        Response& response;
    };

    ClientReadBidiStreamingSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    void initiate(const agrpc::GrpcContext&, const Initiation& initiation, detail::OperationBase* operation) noexcept
    {
        rpc.responder().Read(&initiation.response, operation);
    }

    template <class OnDone>
    static void done(OnDone on_done, bool ok)
    {
        on_done(ok);
    }

    RPCBase& rpc;
};

template <class Responder, class Executor>
struct ClientWriteBidiStreamingSenderImplementation;

template <template <class, class> class Responder, class Request, class Response, class Executor>
struct ClientWriteBidiStreamingSenderImplementation<Responder<Request, Response>, Executor>
    : detail::GrpcSenderImplementationBase
{
    using RPCBase = detail::RPCBidirectionalStreamingBase<Responder<Request, Response>, Executor>;

    struct Initiation
    {
        const Request& req;
        grpc::WriteOptions options;
    };

    ClientWriteBidiStreamingSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    void initiate(const agrpc::GrpcContext&, const Initiation& initiation, detail::OperationBase* operation) noexcept
    {
        auto& [req, options] = initiation;
        if (options.is_last_message())
        {
            rpc.set_writes_done();
        }
        rpc.responder().Write(req, options, operation);
    }

    template <class OnDone>
    static void done(OnDone on_done, bool ok)
    {
        on_done(ok);
    }

    RPCBase& rpc;
};

template <class Responder, class Executor>
struct ClientWritesDoneSenderImplementation;

template <template <class, class> class Responder, class Request, class Response, class Executor>
struct ClientWritesDoneSenderImplementation<Responder<Request, Response>, Executor>
    : detail::GrpcSenderImplementationBase
{
    using RPCBase = detail::RPCBidirectionalStreamingBase<Responder<Request, Response>, Executor>;
    using Initiation = detail::Empty;

    ClientWritesDoneSenderImplementation(RPCBase& rpc) : rpc(rpc) {}

    void initiate(const agrpc::GrpcContext&, const Initiation&, void* self) noexcept
    {
        rpc.responder().WritesDone(self);
    }

    template <class OnDone>
    void done(OnDone on_done, bool ok)
    {
        rpc.set_writes_done();
        on_done(ok);
    }

    RPCBase& rpc;
};
}

AGRPC_NAMESPACE_END

#endif  // AGRPC_DETAIL_HIGH_LEVEL_CLIENT_SENDER_HPP
