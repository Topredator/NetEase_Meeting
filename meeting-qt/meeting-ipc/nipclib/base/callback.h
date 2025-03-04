﻿// Copyright (c) 2022 NetEase, Inc. All rights reserved.
// Use of this source code is governed by a MIT license that can be
// found in the LICENSE file.

#ifndef NIPCLIB_BASE_CALLBACK_H_
#define NIPCLIB_BASE_CALLBACK_H_

#include "nipclib/config/build_config.h"
#include "nipclib/nipclib_export.h"

#include <functional>
#include <memory>

typedef std::function<void(void)> StdClosure;

NIPCLIB_BEGIN_DECLS

using Task = StdClosure;
template <typename... TParam>
using Callback = std::function<void(TParam...)>;

class NIPCLIB_EXPORT WeakFlag {};

template <typename T>
class NIPCLIB_EXPORT WeakCallback {
public:
    WeakCallback(const std::weak_ptr<WeakFlag>& weak_flag, const T& t)
        : weak_flag_(weak_flag)
        , t_(t) {}

    WeakCallback(const std::weak_ptr<WeakFlag>& weak_flag, T&& t)
        : weak_flag_(weak_flag)
        , t_(std::move(t)) {}

    template <class WeakType>
    WeakCallback(const WeakType& weak_callback)
        : weak_flag_(weak_callback.weak_flag_)
        , t_(weak_callback.t_) {}
    template <class... Args>
    auto operator()(Args&&... args) const {
        if (!weak_flag_.expired()) {
            return t_(std::forward<Args>(args)...);
        }
        return decltype(t_(std::forward<Args>(args)...))();
    }
    bool Expired() const { return weak_flag_.expired(); }
    std::weak_ptr<WeakFlag> weak_flag_;
    mutable T t_;
};

class NIPCLIB_EXPORT SupportWeakCallback {
public:
    typedef std::weak_ptr<WeakFlag> _TyWeakFlag;

public:
    virtual ~SupportWeakCallback(){};

    template <typename CallbackType>
    auto ToWeakCallback(const CallbackType& closure) const -> WeakCallback<CallbackType> {
        return WeakCallback<CallbackType>(GetWeakFlag(), closure);
    }

    std::weak_ptr<WeakFlag> GetWeakFlag() const {
        if (m_weakFlag.use_count() == 0) {
            m_weakFlag.reset((WeakFlag*)NULL);
        }
        return m_weakFlag;
    }

private:
    template <typename ReturnValue, typename... Param, typename WeakFlag>
    static std::function<ReturnValue(Param...)> ConvertToWeakCallback(const std::function<ReturnValue(Param...)>& callback,
                                                                      std::weak_ptr<WeakFlag> expiredFlag) {
        auto weakCallback = [expiredFlag, callback](Param... p) {
            if (!expiredFlag.expired()) {
                return callback(p...);
            }
            return ReturnValue();
        };

        return weakCallback;
    }

protected:
    mutable std::shared_ptr<WeakFlag> m_weakFlag;
};

// WeakCallbackFlag一般作为类成员变量使用，要继承，可使用不带Cancel()函数的SupportWeakCallback
//这里禁止继承，主要担心误用。当使用这个类的功能，打包出多个支持weak语义的callback时，一旦错误的调用了Cancel，
//将会取消所有callback，这种情况可能不是用户希望的。此时，应该使用多个不带Cancel函数的WeakCallbackFlag类型的成员变量，
//每个对应一个callback，一一对应的控制每个支持weak语义的callback。
class NIPCLIB_EXPORT WeakCallbackFlag final : public SupportWeakCallback {
public:
    void Cancel() { m_weakFlag.reset(); }

    bool HasUsed() { return m_weakFlag.use_count() != 0; }
};

// global function
template <class F, class... Args, class = typename std::enable_if<!std::is_member_function_pointer<F>::value>::type>
auto Bind(F&& f, Args&&... args) -> decltype(std::bind(f, args...)) {
    return std::bind(f, args...);
}

// const class member function
template <class R, class C, class... DArgs, class P, class... Args>
auto Bind(R (C::*f)(DArgs...) const, P&& p, Args&&... args) -> WeakCallback<decltype(std::bind(f, p, args...))> {
    std::weak_ptr<WeakFlag> weak_flag = ((SupportWeakCallback*)p)->GetWeakFlag();
    auto bind_obj = std::bind(f, p, args...);
    static_assert(std::is_base_of<SupportWeakCallback, C>::value, "nbase::SupportWeakCallback should be base of C");
    WeakCallback<decltype(bind_obj)> weak_callback(weak_flag, std::move(bind_obj));
    return weak_callback;
}

// non-const class member function
template <class R, class C, class... DArgs, class P, class... Args>
auto Bind(R (C::*f)(DArgs...), P&& p, Args&&... args) -> WeakCallback<decltype(std::bind(f, p, args...))> {
    std::weak_ptr<WeakFlag> weak_flag = ((SupportWeakCallback*)p)->GetWeakFlag();
    auto bind_obj = std::bind(f, p, args...);
    static_assert(std::is_base_of<SupportWeakCallback, C>::value, "nbase::SupportWeakCallback should be base of C");
    WeakCallback<decltype(bind_obj)> weak_callback(weak_flag, std::move(bind_obj));
    return weak_callback;
}

NIPCLIB_END_DECLS

#endif  // NIPCLIB_BASE_CALLBACK_H_
