#ifndef ANYDSL_RUNTIME_HPP
#define ANYDSL_RUNTIME_HPP

#ifndef ANYDSL_RUNTIME_H
#include "anydsl_runtime.h"
#endif

namespace anydsl {

enum class Platform : int32_t {
    Host = ANYDSL_HOST,
    Cuda = ANYDSL_CUDA,
    OpenCL = ANYDSL_OPENCL,
    HSA = ANYDSL_HSA,
    PAL = ANYDSL_PAL,
    LevelZero = ANYDSL_LEVELZERO
};

struct Device {
    Device(int32_t id) : id(id) {}
    int32_t id;
};

inline int32_t make_device(Platform p, Device d) {
    return ANYDSL_DEVICE((int32_t)p, d.id);
}

template <typename T>
class Array {
public:
    Array()
        : data_(nullptr), size_(0), dev_(0)
    {}

    Array(int64_t size)
        : Array(Platform::Host, Device(0), size)
    {}

    Array(int32_t dev, T* ptr, int64_t size)
        : data_(ptr), size_(size), dev_(dev)
    {}

    Array(Platform p, Device d, int64_t size)
        : dev_(make_device(p, d)) {
        allocate(size);
    }

    Array(Array&& other)
        : data_(other.data_),
          size_(other.size_),
          dev_(other.dev_) {
        other.data_ = nullptr;
    }

    Array& operator = (Array&& other) {
        deallocate();
        dev_ = other.dev_;
        size_ = other.size_;
        data_ = other.data_;
        other.data_ = nullptr;
        return *this;
    }

    Array(const Array&) = delete;
    Array& operator = (const Array&) = delete;

    ~Array() { deallocate(); }

    T* begin() { return data_; }
    const T* begin() const { return data_; }

    T* end() { return data_ + size_; }
    const T* end() const { return data_ + size_; }

    T* data() { return data_; }
    const T* data() const { return data_; }

    int64_t size() const { return size_; }
    int32_t device() const { return dev_; }

    const T& operator [] (int i) const { return data_[i]; }
    T& operator [] (int i) { return data_[i]; }

    T* release() {
        T* ptr = data_;
        data_ = nullptr;
        size_ = 0;
        dev_ = 0;
        return ptr;
    }

protected:
    void allocate(int64_t size) {
        size_ = size;
        data_ = (T*)anydsl_alloc(dev_, sizeof(T) * size);
    }

    void deallocate() {
        if (data_) anydsl_release(dev_, (void*)data_);
    }

    T* data_;
    int64_t size_;
    int32_t dev_;
};

template <typename T>
void copy(const Array<T>& a, Array<T>& b) {
    anydsl_copy(a.device(), (const void*)a.data(), 0,
                b.device(), (void*)b.data(), 0,
                a.size() * sizeof(T));
}

template <typename T>
void copy(const Array<T>& a, Array<T>& b, int64_t size) {
    anydsl_copy(a.device(), (const void*)a.data(), 0,
                b.device(), (void*)b.data(), 0,
                size * sizeof(T));
}

template <typename T>
void copy(const Array<T>& a, int64_t offset_a, Array<T>& b, int64_t offset_b, int64_t size) {
    anydsl_copy(a.device(), (const void*)a.data(), offset_a * sizeof(T),
                b.device(), (void*)b.data(), offset_b * sizeof(T),
                size * sizeof(T));
}

} // namespace anydsl

#endif
