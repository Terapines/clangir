// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

namespace std {

template<typename T> struct remove_reference       { typedef T type; };
template<typename T> struct remove_reference<T &>  { typedef T type; };
template<typename T> struct remove_reference<T &&> { typedef T type; };

template<typename T>
typename remove_reference<T>::type &&move(T &&t) noexcept;

template <class Ret, typename... T>
struct coroutine_traits { using promise_type = typename Ret::promise_type; };

template <class Promise = void>
struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
};
template <>
struct coroutine_handle<void> {
  template <class PromiseType>
  coroutine_handle(coroutine_handle<PromiseType>) noexcept;
  static coroutine_handle from_address(void *);
};

struct suspend_always {
  bool await_ready() noexcept { return false; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

struct suspend_never {
  bool await_ready() noexcept { return true; }
  void await_suspend(coroutine_handle<>) noexcept {}
  void await_resume() noexcept {}
};

struct string {
  int size() const;
  string();
  string(char const *s);
};

template<typename T>
struct optional {
  optional();
  optional(const T&);
  T &operator*() &;
  T &&operator*() &&;
  T &value() &;
  T &&value() &&;
};
} // namespace std

namespace folly {
namespace coro {

using std::suspend_always;
using std::suspend_never;
using std::coroutine_handle;

using SemiFuture = int;

template<class T>
struct Task {
    struct promise_type {
        Task<T> get_return_object() noexcept;
        suspend_always initial_suspend() noexcept;
        suspend_always final_suspend() noexcept;
        void return_value(T);
        void unhandled_exception();
        auto yield_value(Task<T>) noexcept { return final_suspend(); }
    };
    bool await_ready() noexcept { return false; }
    void await_suspend(coroutine_handle<>) noexcept {}
    T await_resume();
};

template<>
struct Task<void> {
    struct promise_type {
        Task<void> get_return_object() noexcept;
        suspend_always initial_suspend() noexcept;
        suspend_always final_suspend() noexcept;
        void return_void() noexcept;
        void unhandled_exception() noexcept;
        auto yield_value(Task<void>) noexcept { return final_suspend(); }
    };
    bool await_ready() noexcept { return false; }
    void await_suspend(coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
    SemiFuture semi();
};

// FIXME: add CIRGen support here.
// struct blocking_wait_fn {
//   template <typename T>
//   T operator()(Task<T>&& awaitable) const {
//     return T();
//   }
// };

// inline constexpr blocking_wait_fn blocking_wait{};
// static constexpr blocking_wait_fn const& blockingWait = blocking_wait;

template <typename T>
T blockingWait(Task<T>&& awaitable) {
  return T();
}

template <typename T>
Task<T> collectAllRange(Task<T>* awaitable);

template <typename... SemiAwaitables>
Task<void> collectAll(SemiAwaitables&&... awaitables);

struct co_invoke_fn {
  template <typename F, typename... A>
  Task<void> operator()(F&& f, A&&... a) const {
    return Task<void>();
  }
};

co_invoke_fn co_invoke;

}} // namespace folly::coro

// CHECK-DAG: ![[IntTask:.*]] = !cir.record<struct "folly::coro::Task<int>" padded {!u8i}>
// CHECK-DAG: ![[VoidTask:.*]] = !cir.record<struct "folly::coro::Task<void>" padded {!u8i}>
// CHECK-DAG: ![[VoidPromisse:.*]] = !cir.record<struct "folly::coro::Task<void>::promise_type" padded {!u8i}>
// CHECK-DAG: ![[CoroHandleVoid:.*]] = !cir.record<struct "std::coroutine_handle<void>" padded {!u8i}>
// CHECK-DAG: ![[CoroHandlePromise:rec_.*]]  = !cir.record<struct "std::coroutine_handle<folly::coro::Task<void>::promise_type>" padded {!u8i}>
// CHECK-DAG: ![[StdString:.*]] = !cir.record<struct "std::string" padded {!u8i}>
// CHECK-DAG: ![[SuspendAlways:.*]] = !cir.record<struct "std::suspend_always" padded {!u8i}>

// CHECK: module {{.*}} {
// CHECK-NEXT: cir.global external @_ZN5folly4coro9co_invokeE = #cir.zero : !rec_folly3A3Acoro3A3Aco_invoke_fn

// CHECK: cir.func builtin private @__builtin_coro_id(!u32i, !cir.ptr<!void>, !cir.ptr<!void>, !cir.ptr<!void>) -> !u32i
// CHECK: cir.func builtin private @__builtin_coro_alloc(!u32i) -> !cir.bool
// CHECK: cir.func builtin private @__builtin_coro_size() -> !u64i
// CHECK: cir.func builtin private @__builtin_coro_begin(!u32i, !cir.ptr<!void>) -> !cir.ptr<!void>

using VoidTask = folly::coro::Task<void>;

VoidTask silly_task() {
  co_await std::suspend_always();
}

// CHECK: cir.func coroutine dso_local @_Z10silly_taskv() -> ![[VoidTask]] extra{{.*}}{

// Allocate promise.

// CHECK: %[[#VoidTaskAddr:]] = cir.alloca ![[VoidTask]], {{.*}}, ["__retval"]
// CHECK: %[[#SavedFrameAddr:]] = cir.alloca !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>, ["__coro_frame_addr"] {alignment = 8 : i64}
// CHECK: %[[#VoidPromisseAddr:]] = cir.alloca ![[VoidPromisse]], {{.*}}, ["__promise"]

// Get coroutine id with __builtin_coro_id.

// CHECK: %[[#NullPtr:]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CHECK: %[[#Align:]] = cir.const #cir.int<16> : !u32i
// CHECK: %[[#CoroId:]] = cir.call @__builtin_coro_id(%[[#Align]], %[[#NullPtr]], %[[#NullPtr]], %[[#NullPtr]])

// Perform allocation calling operator 'new' depending on __builtin_coro_alloc and
// call __builtin_coro_begin for the final coroutine frame address.

// CHECK: %[[#ShouldAlloc:]] = cir.call @__builtin_coro_alloc(%[[#CoroId]]) : (!u32i) -> !cir.bool
// CHECK: cir.store{{.*}} %[[#NullPtr]], %[[#SavedFrameAddr]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CHECK: cir.if %[[#ShouldAlloc]] {
// CHECK:   %[[#CoroSize:]] = cir.call @__builtin_coro_size() : () -> !u64i
// CHECK:   %[[#AllocAddr:]] = cir.call @_Znwm(%[[#CoroSize]]) : (!u64i) -> !cir.ptr<!void>
// CHECK:   cir.store{{.*}} %[[#AllocAddr]], %[[#SavedFrameAddr]] : !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CHECK: }
// CHECK: %[[#Load0:]] = cir.load{{.*}} %[[#SavedFrameAddr]] : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CHECK: %[[#CoroFrameAddr:]] = cir.call @__builtin_coro_begin(%[[#CoroId]], %[[#Load0]])

// Call promise.get_return_object() to retrieve the task object.

// CHECK: %[[#RetObj:]] = cir.call @_ZN5folly4coro4TaskIvE12promise_type17get_return_objectEv(%[[#VoidPromisseAddr]]) : {{.*}} -> ![[VoidTask]]
// CHECK: cir.store{{.*}} %[[#RetObj]], %[[#VoidTaskAddr]] : ![[VoidTask]]

// Start a new scope for the actual codegen for co_await, create temporary allocas for
// holding coroutine handle and the suspend_always struct.

// CHECK: cir.scope {
// CHECK:   %[[#SuspendAlwaysAddr:]] = cir.alloca ![[SuspendAlways]], {{.*}} ["ref.tmp0"] {alignment = 1 : i64}
// CHECK:   %[[#CoroHandleVoidAddr:]] = cir.alloca ![[CoroHandleVoid]], {{.*}} ["agg.tmp0"] {alignment = 1 : i64}
// CHECK:   %[[#CoroHandlePromiseAddr:]] = cir.alloca ![[CoroHandlePromise]], {{.*}} ["agg.tmp1"] {alignment = 1 : i64}

// Effectively execute `coawait promise_type::initial_suspend()` by calling initial_suspend() and getting
// the suspend_always struct to use for cir.await. Note that we return by-value since we defer ABI lowering
// to later passes, same is done elsewhere.

// CHECK:   %[[#Tmp0:]] = cir.call @_ZN5folly4coro4TaskIvE12promise_type15initial_suspendEv(%[[#VoidPromisseAddr]])
// CHECK:   cir.store{{.*}} %[[#Tmp0]], %[[#SuspendAlwaysAddr]]

//
// Here we start mapping co_await to cir.await.
//

// First regions `ready` has a special cir.yield code to veto suspension.

// CHECK:   cir.await(init, ready : {
// CHECK:     %[[#ReadyVeto:]] = cir.scope {
// CHECK:       %[[#TmpCallRes:]] = cir.call @_ZNSt14suspend_always11await_readyEv(%[[#SuspendAlwaysAddr]])
// CHECK:       cir.yield %[[#TmpCallRes]] : !cir.bool
// CHECK:     }
// CHECK:     cir.condition(%[[#ReadyVeto]])

// Second region `suspend` contains the actual suspend logic.
//
// - Start by getting the coroutine handle using from_address().
// - Implicit convert coroutine handle from task specific promisse
//   specialization to a void one.
// - Call suspend_always::await_suspend() passing the handle.
//
// FIXME: add veto support for non-void await_suspends.

// CHECK:   }, suspend : {
// CHECK:     %[[#FromAddrRes:]] = cir.call @_ZNSt16coroutine_handleIN5folly4coro4TaskIvE12promise_typeEE12from_addressEPv(%[[#CoroFrameAddr]])
// CHECK:     cir.store{{.*}} %[[#FromAddrRes]], %[[#CoroHandlePromiseAddr]] : ![[CoroHandlePromise]]
// CHECK:     %[[#CoroHandlePromiseReload:]] = cir.load{{.*}} %[[#CoroHandlePromiseAddr]]
// CHECK:     cir.call @_ZNSt16coroutine_handleIvEC1IN5folly4coro4TaskIvE12promise_typeEEES_IT_E(%[[#CoroHandleVoidAddr]], %[[#CoroHandlePromiseReload]])
// CHECK:     %[[#CoroHandleVoidReload:]] = cir.load{{.*}} %[[#CoroHandleVoidAddr]] : !cir.ptr<![[CoroHandleVoid]]>, ![[CoroHandleVoid]]
// CHECK:     cir.call @_ZNSt14suspend_always13await_suspendESt16coroutine_handleIvE(%[[#SuspendAlwaysAddr]], %[[#CoroHandleVoidReload]])
// CHECK:     cir.yield

// Third region `resume` handles coroutine resuming logic.

// CHECK:   }, resume : {
// CHECK:     cir.call @_ZNSt14suspend_always12await_resumeEv(%[[#SuspendAlwaysAddr]])
// CHECK:     cir.yield
// CHECK:   },)
// CHECK: }

// Since we already tested cir.await guts above, the remaining checks for:
// - The actual user written co_await
// - The promise call
// - The final suspend co_await
// - Return

// The actual user written co_await
// CHECK: cir.scope {
// CHECK:   cir.await(user, ready : {
// CHECK:   }, suspend : {
// CHECK:   }, resume : {
// CHECK:   },)
// CHECK: }

// The promise call
// CHECK: cir.call @_ZN5folly4coro4TaskIvE12promise_type11return_voidEv(%[[#VoidPromisseAddr]])

// The final suspend co_await
// CHECK: cir.scope {
// CHECK:   cir.await(final, ready : {
// CHECK:   }, suspend : {
// CHECK:   }, resume : {
// CHECK:   },)
// CHECK: }

// Call builtin coro end and return

// CHECK-NEXT: %[[#CoroEndArg0:]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CHECK-NEXT: %[[#CoroEndArg1:]] = cir.const #false
// CHECK-NEXT: = cir.call @__builtin_coro_end(%[[#CoroEndArg0]], %[[#CoroEndArg1]])

// CHECK: %[[#Tmp1:]] = cir.load{{.*}} %[[#VoidTaskAddr]]
// CHECK-NEXT: cir.return %[[#Tmp1]]
// CHECK-NEXT: }

folly::coro::Task<int> byRef(const std::string& s) {
  co_return s.size();
}

// FIXME: this could be less redundant than two allocas + reloads
// CHECK: cir.func coroutine dso_local @_Z5byRefRKSt6string(%arg0: !cir.ptr<![[StdString]]> {{.*}} ![[IntTask]] extra{{.*}}{
// CHECK: %[[#AllocaParam:]] = cir.alloca !cir.ptr<![[StdString]]>, {{.*}} ["s", init, const]
// CHECK: %[[#AllocaFnUse:]] = cir.alloca !cir.ptr<![[StdString]]>, {{.*}} ["s", init, const]

folly::coro::Task<void> silly_coro() {
  std::optional<folly::coro::Task<int>> task;
  {
    std::string s = "yolo";
    task = byRef(s);
  }
  folly::coro::blockingWait(std::move(task.value()));
  co_return;
}

// Make sure we properly handle OnFallthrough coro body sub stmt and
// check there are not multiple co_returns emitted.

// CHECK: cir.func coroutine dso_local @_Z10silly_corov() {{.*}} ![[VoidTask]] extra{{.*}}{
// CHECK: cir.await(init, ready : {
// CHECK: cir.call @_ZN5folly4coro4TaskIvE12promise_type11return_voidEv
// CHECK-NOT: cir.call @_ZN5folly4coro4TaskIvE12promise_type11return_voidEv
// CHECK: cir.await(final, ready : {

folly::coro::Task<int> go(int const& val);
folly::coro::Task<int> go1() {
  auto task = go(1);
  co_return co_await task;
}

// CHECK: cir.func coroutine dso_local @_Z3go1v() {{.*}} ![[IntTask]] extra{{.*}}{
// CHECK: %[[#IntTaskAddr:]] = cir.alloca ![[IntTask]], !cir.ptr<![[IntTask]]>, ["task", init]

// CHECK:   cir.await(init, ready : {
// CHECK:   }, suspend : {
// CHECK:   }, resume : {
// CHECK:   },)
// CHECK: }

// The call to go(1) has its own scope due to full-expression rules.
// CHECK: cir.scope {
// CHECK:   %[[#OneAddr:]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["ref.tmp1", init] {alignment = 4 : i64}
// CHECK:   %[[#One:]] = cir.const #cir.int<1> : !s32i
// CHECK:   cir.store{{.*}} %[[#One]], %[[#OneAddr]] : !s32i, !cir.ptr<!s32i>
// CHECK:   %[[#IntTaskTmp:]] = cir.call @_Z2goRKi(%[[#OneAddr]]) : (!cir.ptr<!s32i>) -> ![[IntTask]]
// CHECK:   cir.store{{.*}} %[[#IntTaskTmp]], %[[#IntTaskAddr]] : ![[IntTask]], !cir.ptr<![[IntTask]]>
// CHECK: }

// CHECK: %[[#CoReturnValAddr:]] = cir.alloca !s32i, !cir.ptr<!s32i>, ["__coawait_resume_rval"] {alignment = 1 : i64}
// CHECK: cir.await(user, ready : {
// CHECK: }, suspend : {
// CHECK: }, resume : {
// CHECK:   %[[#ResumeVal:]] = cir.call @_ZN5folly4coro4TaskIiE12await_resumeEv(%3)
// CHECK:   cir.store{{.*}} %[[#ResumeVal]], %[[#CoReturnValAddr]] : !s32i, !cir.ptr<!s32i>
// CHECK: },)
// CHECK: %[[#V:]] = cir.load{{.*}} %[[#CoReturnValAddr]] : !cir.ptr<!s32i>, !s32i
// CHECK: cir.call @_ZN5folly4coro4TaskIiE12promise_type12return_valueEi({{.*}}, %[[#V]])

folly::coro::Task<int> go1_lambda() {
  auto task = []() -> folly::coro::Task<int> {
    co_return 1;
  }();
  co_return co_await task;
}

// CHECK: cir.func coroutine lambda internal private dso_local @_ZZ10go1_lambdavENK3$_0clEv{{.*}} ![[IntTask]] extra{{.*}}{
// CHECK: cir.func coroutine dso_local @_Z10go1_lambdav() {{.*}} ![[IntTask]] extra{{.*}}{

folly::coro::Task<int> go4() {
  auto* fn = +[](int const& i) -> folly::coro::Task<int> { co_return i; };
  auto task = fn(3);
  co_return co_await std::move(task);
}

// CHECK: cir.func coroutine dso_local @_Z3go4v() {{.*}} ![[IntTask]] extra{{.*}}{

// CHECK:   cir.await(init, ready : {
// CHECK:   }, suspend : {
// CHECK:   }, resume : {
// CHECK:   },)
// CHECK: }

// CHECK: %12 = cir.scope {
// CHECK:   %17 = cir.alloca !rec_anon2E2, !cir.ptr<!rec_anon2E2>, ["ref.tmp1"] {alignment = 1 : i64}

// Get the lambda invoker ptr via `lambda operator folly::coro::Task<int> (*)(int const&)()`
// CHECK:   %18 = cir.call @_ZZ3go4vENK3$_0cvPFN5folly4coro4TaskIiEERKiEEv(%17) : (!cir.ptr<!rec_anon2E2>) -> !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>
// CHECK:   %19 = cir.unary(plus, %18) : !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>, !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>
// CHECK:   cir.yield %19 : !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>
// CHECK: }
// CHECK: cir.store{{.*}} %12, %3 : !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>, !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>>
// CHECK: cir.scope {
// CHECK:   %17 = cir.alloca !s32i, !cir.ptr<!s32i>, ["ref.tmp2", init] {alignment = 4 : i64}
// CHECK:   %18 = cir.load{{.*}} %3 : !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>>, !cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>
// CHECK:   %19 = cir.const #cir.int<3> : !s32i
// CHECK:   cir.store{{.*}} %19, %17 : !s32i, !cir.ptr<!s32i>

// Call invoker, which calls operator() indirectly.
// CHECK:   %20 = cir.call %18(%17) : (!cir.ptr<!cir.func<(!cir.ptr<!s32i>) -> ![[IntTask]]>>, !cir.ptr<!s32i>) -> ![[IntTask]]
// CHECK:   cir.store{{.*}} %20, %4 : ![[IntTask]], !cir.ptr<![[IntTask]]>
// CHECK: }

// CHECK:   cir.await(user, ready : {
// CHECK:   }, suspend : {
// CHECK:   }, resume : {
// CHECK:   },)
// CHECK: }

folly::coro::Task<void> yield();
folly::coro::Task<void> yield1() {
  auto t = yield();
  co_yield t;
}

// CHECK: cir.func coroutine dso_local @_Z6yield1v() -> !rec_folly3A3Acoro3A3ATask3Cvoid3E

// CHECK: cir.await(init, ready : {
// CHECK: }, suspend : {
// CHECK: }, resume : {
// CHECK: },)

//      CHECK: cir.scope {
// CHECK-NEXT:   %[[#SUSPEND_PTR:]] = cir.alloca !rec_std3A3Asuspend_always, !cir.ptr<!rec_std3A3Asuspend_always>
// CHECK-NEXT:   %[[#AWAITER_PTR:]] = cir.alloca !rec_folly3A3Acoro3A3ATask3Cvoid3E, !cir.ptr<!rec_folly3A3Acoro3A3ATask3Cvoid3E>
// CHECK-NEXT:   %[[#CORO_PTR:]] = cir.alloca !rec_std3A3Acoroutine_handle3Cvoid3E, !cir.ptr<!rec_std3A3Acoroutine_handle3Cvoid3E>
// CHECK-NEXT:   %[[#CORO2_PTR:]] = cir.alloca !rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E, !cir.ptr<!rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E>
// CHECK-NEXT:   cir.copy {{.*}} to %[[#AWAITER_PTR:]] : !cir.ptr<!rec_folly3A3Acoro3A3ATask3Cvoid3E>
// CHECK-NEXT:   %[[#AWAITER:]] = cir.load{{.*}} %[[#AWAITER_PTR]] : !cir.ptr<!rec_folly3A3Acoro3A3ATask3Cvoid3E>, !rec_folly3A3Acoro3A3ATask3Cvoid3E
// CHECK-NEXT:   %[[#SUSPEND:]] = cir.call @_ZN5folly4coro4TaskIvE12promise_type11yield_valueES2_(%{{.+}}, %[[#AWAITER]]) : (!cir.ptr<!rec_folly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type>, !rec_folly3A3Acoro3A3ATask3Cvoid3E) -> !rec_std3A3Asuspend_always
// CHECK-NEXT:   cir.store{{.*}} %[[#SUSPEND]], %[[#SUSPEND_PTR]] : !rec_std3A3Asuspend_always, !cir.ptr<!rec_std3A3Asuspend_always>
// CHECK-NEXT:   cir.await(yield, ready : {
// CHECK-NEXT:     %[[#READY:]] = cir.scope {
// CHECK-NEXT:       %[[#A:]] = cir.call @_ZNSt14suspend_always11await_readyEv(%[[#SUSPEND_PTR]]) : (!cir.ptr<!rec_std3A3Asuspend_always>) -> !cir.bool
// CHECK-NEXT:       cir.yield %[[#A]] : !cir.bool
// CHECK-NEXT:     } : !cir.bool
// CHECK-NEXT:     cir.condition(%[[#READY]])
// CHECK-NEXT:   }, suspend : {
// CHECK-NEXT:     %[[#CORO2:]] = cir.call @_ZNSt16coroutine_handleIN5folly4coro4TaskIvE12promise_typeEE12from_addressEPv(%9) : (!cir.ptr<!void>) -> !rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E
// CHECK-NEXT:     cir.store{{.*}} %[[#CORO2]], %[[#CORO2_PTR]] : !rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E, !cir.ptr<!rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E>
// CHECK-NEXT:     %[[#B:]] = cir.load{{.*}} %[[#CORO2_PTR]] : !cir.ptr<!rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E>, !rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E
// CHECK-NEXT:     cir.call @_ZNSt16coroutine_handleIvEC1IN5folly4coro4TaskIvE12promise_typeEEES_IT_E(%[[#CORO_PTR]], %[[#B]]) : (!cir.ptr<!rec_std3A3Acoroutine_handle3Cvoid3E>, !rec_std3A3Acoroutine_handle3Cfolly3A3Acoro3A3ATask3Cvoid3E3A3Apromise_type3E) -> ()
// CHECK-NEXT:     %[[#C:]] = cir.load{{.*}} %[[#CORO_PTR]] : !cir.ptr<!rec_std3A3Acoroutine_handle3Cvoid3E>, !rec_std3A3Acoroutine_handle3Cvoid3E
// CHECK-NEXT:     cir.call @_ZNSt14suspend_always13await_suspendESt16coroutine_handleIvE(%[[#SUSPEND_PTR]], %[[#C]]) : (!cir.ptr<!rec_std3A3Asuspend_always>, !rec_std3A3Acoroutine_handle3Cvoid3E) -> ()
// CHECK-NEXT:     cir.yield
// CHECK-NEXT:   }, resume : {
// CHECK-NEXT:     cir.call @_ZNSt14suspend_always12await_resumeEv(%[[#SUSPEND_PTR]]) : (!cir.ptr<!rec_std3A3Asuspend_always>) -> ()
// CHECK-NEXT:     cir.yield
// CHECK-NEXT:   },)
// CHECK-NEXT: }

// CHECK: cir.await(final, ready : {
// CHECK: }, suspend : {
// CHECK: }, resume : {
// CHECK: },)

// CHECK: }
