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

#ifndef AGRPC_UTILS_MANUALLIFETIME_HPP
#define AGRPC_UTILS_MANUALLIFETIME_HPP

#include <memory>

namespace test
{
template <class T>
class ManualLifetime
{
  public:
    ManualLifetime() noexcept {}

    ~ManualLifetime() noexcept {}

    template <class... Args>
    T& construct(Args&&... args)
    {
        return *::new (voidify(std::addressof(value))) T(std::forward<Args>(args)...);
    }

    void destruct() { std::destroy_at(std::addressof(value)); }

  private:
    template <class T>
    static constexpr void* voidify(T* ptr) noexcept
    {
        return const_cast<void*>(static_cast<const volatile void*>(ptr));
    }

    union
    {
        T value;
    };
};
}  // namespace test

#endif  // AGRPC_UTILS_MANUALLIFETIME_HPP
