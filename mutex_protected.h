#ifndef MUTEX_PROTECTED_H_
#define MUTEX_PROTECTED_H_

#include <mutex>

template<typename T, typename Lockable = std::mutex>
class mutex_protected
{
public:
    template<typename... Args>
    mutex_protected(Args&&... args) : m_value(std::forward<Args>(args)...) {}

    template<typename U, typename Lock = std::unique_lock<Lockable>>
    class locked_access : Lock
    {
    public:
        U* operator->() const { return m_ptr; }
        U& operator*() const { return *m_ptr; }
        template<typename... Args>
        locked_access(U* ptr, Args&&... args) : Lock(std::forward<Args>(args)...), m_ptr(ptr) {}
    private:
        U* m_ptr;
    };

    auto lock()      & { return locked_access<      T>{&m_value, m_mtx}; }
    auto lock() const& { return locked_access<const T>{&m_value, m_mtx}; }

private:
    T m_value;
    mutable Lockable m_mtx;
};

#endif /* MUTEX_PROTECTED_H_ */
