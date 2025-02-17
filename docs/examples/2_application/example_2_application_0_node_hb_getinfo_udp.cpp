/// @file
/// Example of creating a libcyphal node in your project using posix UDP sockets and application layer.
/// This example demonstrates how to make a simple libcyphal node using application layer `Node` class.
///
/// @copyright
/// Copyright (C) OpenCyphal Development Team  <opencyphal.org>
/// Copyright Amazon.com Inc. or its affiliates.
/// SPDX-License-Identifier: MIT
///

#include "platform/common_helpers.hpp"
#include "platform/posix/posix_single_threaded_executor.hpp"
#include "platform/posix/udp/udp_media.hpp"
#include "platform/storage.hpp"
#include "platform/tracking_memory_resource.hpp"

#include <cetl/pf17/cetlpf.hpp>
#include <libcyphal/application/node.hpp>
#include <libcyphal/application/registry/register.hpp>
#include <libcyphal/application/registry/registry_impl.hpp>
#include <libcyphal/presentation/presentation.hpp>
#include <libcyphal/transport/types.hpp>
#include <libcyphal/transport/udp/udp_transport.hpp>
#include <libcyphal/transport/udp/udp_transport_impl.hpp>
#include <libcyphal/types.hpp>

#include <uavcan/primitive/array/Bit_1_0.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{

using namespace example::platform;       // NOLINT This our main concern here in this test.
using namespace libcyphal::application;  // NOLINT This our main concern here in this test.

// https://github.com/llvm/llvm-project/issues/53444
// NOLINTBEGIN(misc-unused-using-decls, misc-include-cleaner)
using std::literals::chrono_literals::operator""s;
using std::literals::chrono_literals::operator""ms;
// NOLINTEND(misc-unused-using-decls, misc-include-cleaner)

using testing::Eq;
using testing::IsEmpty;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

class Example_2_Application_0_NodeHeartbeatGetInfo_Udp : public testing::Test
{
protected:
    using Duration        = libcyphal::Duration;
    using TimePoint       = libcyphal::TimePoint;
    using UdpTransportPtr = libcyphal::UniquePtr<libcyphal::transport::udp::IUdpTransport>;

    void SetUp() override
    {
        cetl::pmr::set_default_resource(&mr_);

        // Duration in seconds for which the test will run. Default is 10 seconds.
        if (const auto* const run_duration_str = std::getenv("CYPHAL__RUN"))
        {
            run_duration_ = std::chrono::duration<std::int64_t>{std::strtoll(run_duration_str, nullptr, 10)};
        }
        // Local node ID. Default is 42.
        if (const auto* const node_id_str = std::getenv("CYPHAL__NODE__ID"))
        {
            local_node_id_ = static_cast<libcyphal::transport::NodeId>(std::stoul(node_id_str));
        }
        // Space separated list of interface addresses, like "127.0.0.1 192.168.1.162". Default is "127.0.0.1".
        if (const auto* const iface_addresses_str = std::getenv("CYPHAL__UDP__IFACE"))
        {
            iface_addresses_ = CommonHelpers::splitInterfaceAddresses(iface_addresses_str);
        }

        startup_time_ = executor_.now();
    }

    void TearDown() override
    {
        executor_.releaseTemporaryResources();

        EXPECT_THAT(mr_.allocated_bytes, 0);
        EXPECT_THAT(mr_.allocations, IsEmpty());
        EXPECT_THAT(mr_.total_allocated_bytes, mr_.total_deallocated_bytes);
    }

    registry::IRegister::Value makeStringValue(const cetl::string_view sv) const
    {
        registry::IRegister::Value value{mr_alloc_};
        auto&                      str = value.set_string();
        std::copy(sv.begin(), sv.end(), std::back_inserter(str.value));
        return value;
    }

    // MARK: Data members:
    // NOLINTBEGIN

    struct State
    {
        posix::UdpMedia::Collection media_collection_;
        UdpTransportPtr             transport_;

    };  // State

    TrackingMemoryResource                 mr_;
    cetl::pmr::polymorphic_allocator<void> mr_alloc_{&mr_};
    posix::PollSingleThreadedExecutor      executor_{mr_};
    TimePoint                              startup_time_{};
    libcyphal::transport::NodeId           local_node_id_{42};
    Duration                               run_duration_{10s};
    std::vector<std::string>               iface_addresses_{"127.0.0.1"};
    // NOLINTEND

};  // Example_1_Presentation_1_PingUserService_Udp

TEST_F(Example_2_Application_0_NodeHeartbeatGetInfo_Udp, main)
{
    State state;

    std::cout << "-----------\n";
    std::cout << "Local  node ID: " << local_node_id_ << "\n";
    std::cout << "Interfaces    : '" << CommonHelpers::joinInterfaceAddresses(iface_addresses_) << "'\n";

    // 1. Make UDP transport with a collection of media.
    //
    constexpr std::size_t tx_capacity = 16;
    state.media_collection_.make(mr_, executor_, iface_addresses_);
    auto maybe_transport = makeTransport({mr_}, executor_, state.media_collection_.span(), tx_capacity);
    ASSERT_THAT(maybe_transport, testing::VariantWith<UdpTransportPtr>(testing::NotNull()))
        << "Can't create transport.";
    state.transport_ = cetl::get<UdpTransportPtr>(std::move(maybe_transport));
    state.transport_->setLocalNodeId(local_node_id_);
    state.transport_->setTransientErrorHandler(CommonHelpers::Udp::transientErrorReporter);

    // 2. Create a presentation layer object.
    //
    libcyphal::presentation::Presentation presentation{mr_, executor_, *state.transport_};

    // 3. Create a node with name.
    //
    auto maybe_node = Node::make(presentation);
    ASSERT_THAT(maybe_node, testing::VariantWith<Node>(testing::_)) << "Can't create node.";
    auto node = cetl::get<Node>(std::move(maybe_node));
    //
    auto& get_info_prov = node.getInfoProvider();
    get_info_prov  //
        .setSoftwareVersion(0, 1)
        .setHardwareVersion(0, 2)
        .setName("org.opencyphal.Ex_2_App_0_Node_UDP")
        .setCertificateOfAuthenticity("my_cert")
        .setUniqueId(std::array<std::uint8_t, 3>{0x12, 0x34, 0x56});

    // 4. Bring up registry provider, and expose several registers. Load persistent storage.
    //
    registry::Registry rgy{mr_};
    ASSERT_THAT(node.makeRegistryProvider(rgy), Eq(cetl::nullopt));
    //
    using BitArray = uavcan::primitive::array::Bit_1_0;
    const BitArray param_ro_val{BitArray::_traits_::TypeOf::value{{true, false}, mr_alloc_}, mr_alloc_};
    auto           param_ro = rgy.route("ro", [&param_ro_val] { return param_ro_val; });
    //
    auto param_name = rgy.route(  //
        "uavcan.node.description",
        [this, &get_info_prov] { return makeStringValue(registry::makeStringView(get_info_prov.response().name)); },
        [&get_info_prov](const registry::IRegister::Value& value) -> cetl::optional<registry::SetError> {
            //
            if (const auto* const str = value.get_string_if())
            {
                get_info_prov.setName(registry::makeStringView(str->value));
                return cetl::nullopt;
            }
            return registry::SetError::Semantics;
        },
        {true});  // persist
    //
    storage::KeyValue platform_storage("/tmp/org.opencyphal.ex_2_app_0");
    load(platform_storage, rgy);

    // 5. Main loop.
    //
    Duration        worst_lateness{0};
    const TimePoint deadline = startup_time_ + run_duration_ + 500ms;
    std::cout << "-----------\nRunning..." << std::endl;  // NOLINT
    //
    while (executor_.now() < deadline)
    {
        const auto spin_result = executor_.spinOnce();
        worst_lateness         = std::max(worst_lateness, spin_result.worst_lateness);

        cetl::optional<libcyphal::Duration> opt_timeout{1s};  // awake at least once per second
        if (spin_result.next_exec_time.has_value())
        {
            opt_timeout = std::min(*opt_timeout, spin_result.next_exec_time.value() - executor_.now());
        }
        EXPECT_THAT(executor_.pollAwaitableResourcesFor(opt_timeout), testing::Eq(cetl::nullopt));
    }

    save(platform_storage, rgy);

    std::cout << "Done.\n-----------\nStats:\n";
    std::cout << "worst_callback_lateness  = " << worst_lateness.count() << " us\n";
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

}  // namespace
